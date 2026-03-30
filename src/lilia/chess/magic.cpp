#include "lilia/chess/core/magic.hpp"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <functional>
#include <iostream>
#include <limits>
#include <random>
#include <vector>

#include "lilia/chess/core/random.hpp"
#include "lilia/chess/generated/magic_constants.hpp"
#include "lilia/chess/magic_serializer.hpp"

#if defined(__GNUC__) || defined(__clang__)
#define LILIA_LIKELY(x) __builtin_expect(!!(x), 1)
#define LILIA_UNLIKELY(x) __builtin_expect(!!(x), 0)
#else
#define LILIA_LIKELY(x) (x)
#define LILIA_UNLIKELY(x) (x)
#endif

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386) || defined(_M_IX86)
#if defined(__BMI2__) || defined(_MSC_VER)
#include <immintrin.h> // _pext_u64
#define LILIA_HAVE_PEXT_INTRINSIC 1
#endif
#if !defined(_MSC_VER) && !defined(__BMI2__)
#include <cpuid.h>
#endif
#endif

namespace lilia::chess::magic
{

  static std::array<core::Bitboard, 64> g_rook_mask{};
  static std::array<core::Bitboard, 64> g_bishop_mask{};

  static std::array<Magic, 64> g_rook_magic{};
  static std::array<Magic, 64> g_bishop_magic{};

  // Compatibility vector tables (can be dropped after packing; lazily reconstructable)
  static std::array<std::vector<core::Bitboard>, 64> g_rook_table;
  static std::array<std::vector<core::Bitboard>, 64> g_bishop_table;

  // Flat arenas + offsets/lengths
  static std::array<std::uint32_t, 64> g_r_off_magic{}, g_b_off_magic{};
  static std::array<std::uint16_t, 64> g_r_len_magic{}, g_b_len_magic{};
  static std::vector<core::Bitboard> g_r_arena_magic, g_b_arena_magic;

  static std::array<std::uint32_t, 64> g_r_off_pext{}, g_b_off_pext{};
  static std::array<std::uint16_t, 64> g_r_len_pext{}, g_b_len_pext{};
  static std::vector<core::Bitboard> g_r_arena_pext, g_b_arena_pext;

  static bool g_use_pext = false;

  // -------------------- helpers --------------------

  template <class F>
  static inline void foreach_subset(core::Bitboard mask, F &&f)
  {
    core::Bitboard sub = mask;
    while (true)
    {
      f(sub);
      if (sub == 0)
        break;
      sub = (sub - 1) & mask;
    }
  }

  static inline core::Bitboard brute_rook(Square sq, core::Bitboard occ)
  {
    return core::rook_attacks(sq, occ);
  }
  static inline core::Bitboard brute_bishop(Square sq, core::Bitboard occ)
  {
    return core::bishop_attacks(sq, occ);
  }
  static inline core::Bitboard brute_attacks(Slider s, Square sq, core::Bitboard occ)
  {
    return (s == Slider::Rook) ? brute_rook(sq, occ) : brute_bishop(sq, occ);
  }

  // Build-time / validation index (kept for generator paths)
  static inline std::uint64_t index_for_occ_checked(core::Bitboard occ, core::Bitboard mask,
                                                    core::Bitboard magic, std::uint8_t shift)
  {
    const int bits = core::popcount(mask);
    if (bits == 0)
      return 0ULL;
    const core::Bitboard subset = occ & mask;
    return static_cast<std::uint64_t>((subset * magic) >> shift);
  }

  // Hot-path index: NO popcount, NO additional masking.
  // Safe for shift==64 via guard (avoids UB shift-by-64).
  static inline std::uint32_t index_for_occ_fast(core::Bitboard occ, core::Bitboard mask,
                                                 core::Bitboard magic, std::uint8_t shift) noexcept
  {
    if (LILIA_UNLIKELY(shift >= 64))
      return 0u;
    const core::Bitboard subset = occ & mask;
    return static_cast<std::uint32_t>((subset * magic) >> shift);
  }

  static inline void build_table_for_square(Slider s, int sq, core::Bitboard mask, core::Bitboard magic,
                                            std::uint8_t shift, std::vector<core::Bitboard> &outTable)
  {
    const int bits = core::popcount(mask);
    const std::size_t tableSize = (bits == 0) ? 1ULL : (1ULL << bits);
    outTable.assign(tableSize, 0ULL);

    foreach_subset(mask, [&](core::Bitboard occSubset)
                   {
    const std::uint64_t index = index_for_occ_checked(occSubset, mask, magic, shift);
    outTable[static_cast<std::size_t>(index)] =
        brute_attacks(s, static_cast<Square>(sq), occSubset); });
  }

  static inline bool try_magic_for_square(Slider s, int sq, core::Bitboard mask, core::Bitboard magic,
                                          std::uint8_t shift, std::vector<core::Bitboard> &outTable)
  {
    const int bits = core::popcount(mask);
    const std::size_t tableSize = (bits == 0) ? 1ULL : (1ULL << bits);

    std::vector<std::uint8_t> used(tableSize, 0);
    std::vector<core::Bitboard> table(tableSize);

    core::Bitboard occSubset = mask;
    while (true)
    {
      const std::uint64_t idx = index_for_occ_checked(occSubset, mask, magic, shift);
      const core::Bitboard atk = brute_attacks(s, static_cast<Square>(sq), occSubset);

      const std::size_t j = static_cast<std::size_t>(idx);
      if (!used[j])
      {
        used[j] = 1;
        table[j] = atk;
      }
      else if (table[j] != atk)
      {
        return false;
      }

      if (occSubset == 0)
        break;
      occSubset = (occSubset - 1) & mask;
    }

    outTable = std::move(table);
    return true;
  }

  static inline bool find_magic_for_square(Slider s, int sq, core::Bitboard mask,
                                           core::Bitboard &out_magic, std::uint8_t &out_shift,
                                           std::vector<core::Bitboard> &outTable)
  {
    const int bits = core::popcount(mask);
    const std::uint8_t shift =
        static_cast<std::uint8_t>((bits == 0) ? 64u : (64u - static_cast<std::uint8_t>(bits)));
    out_shift = shift;

    core::Bitboard seed = 0xC0FFEE123456789ULL ^ (static_cast<core::Bitboard>(sq) << 32) ^
                          (s == Slider::Rook ? 0xF0F0F0F0ULL : 0x0F0F0F0FULL);

    constexpr int MAX_ATTEMPTS = 2'000'000;
    core::SplitMix64 splitmix(seed);

    auto gen_candidate = [&](int strategy) -> core::Bitboard
    {
      switch (strategy)
      {
      case 0:
        return splitmix.next() & splitmix.next() & splitmix.next();
      case 1:
        return splitmix.next() & splitmix.next();
      case 2:
        return splitmix.next() ^ (splitmix.next() << 1);
      case 3:
      {
        core::Bitboard v = splitmix.next() & splitmix.next();
        core::Bitboard hi = (splitmix.next() & 0xFFULL) << 56;
        return v | hi;
      }
      default:
        return splitmix.next();
      }
    };

    for (int attempt = 0; attempt < MAX_ATTEMPTS; ++attempt)
    {
      for (int strat = 0; strat < 4; ++strat)
      {
        core::Bitboard cand = gen_candidate(strat);
        if (bits > 0)
        {
          const int highpop = core::popcount((cand * mask) & 0xFF00000000000000ULL);
          if (highpop < 2)
            continue;
        }
        if (try_magic_for_square(s, sq, mask, cand, shift, outTable))
        {
          out_magic = cand;
          return true;
        }
      }
#ifndef NDEBUG
      if ((attempt & 0xFFF) == 0)
      {
        std::cerr << "find_magic for sq=" << sq << " attempt=" << attempt << "\n";
      }
#endif
    }
#ifndef NDEBUG
    std::cerr << "find_magic_for_square FAILED (sq=" << sq << ", bits=" << bits << ")\n";
#endif
    return false;
  }

  static inline core::Bitboard rook_relevant_mask(Square sq)
  {
    core::Bitboard mask = 0ULL;
    int r = core::rank_of(sq), f = core::file_of(sq);
    for (int rr = r + 1; rr <= 6; ++rr)
      mask |= core::sq_bb(static_cast<Square>(rr * 8 + f));
    for (int rr = r - 1; rr >= 1; --rr)
      mask |= core::sq_bb(static_cast<Square>(rr * 8 + f));
    for (int ff = f + 1; ff <= 6; ++ff)
      mask |= core::sq_bb(static_cast<Square>(r * 8 + ff));
    for (int ff = f - 1; ff >= 1; --ff)
      mask |= core::sq_bb(static_cast<Square>(r * 8 + ff));
    return mask;
  }

  static inline core::Bitboard bishop_relevant_mask(Square sq)
  {
    core::Bitboard mask = 0ULL;
    int r = core::rank_of(sq), f = core::file_of(sq);
    for (int rr = r + 1, ff = f + 1; rr <= 6 && ff <= 6; ++rr, ++ff)
      mask |= core::sq_bb(static_cast<Square>(rr * 8 + ff));
    for (int rr = r + 1, ff = f - 1; rr <= 6 && ff >= 1; ++rr, --ff)
      mask |= core::sq_bb(static_cast<Square>(rr * 8 + ff));
    for (int rr = r - 1, ff = f + 1; rr >= 1 && ff <= 6; --rr, ++ff)
      mask |= core::sq_bb(static_cast<Square>(rr * 8 + ff));
    for (int rr = r - 1, ff = f - 1; rr >= 1 && ff >= 1; --rr, --ff)
      mask |= core::sq_bb(static_cast<Square>(rr * 8 + ff));
    return mask;
  }

  static inline void build_masks()
  {
    for (int sq = 0; sq < 64; ++sq)
    {
      g_rook_mask[sq] = rook_relevant_mask(static_cast<Square>(sq));
      g_bishop_mask[sq] = bishop_relevant_mask(static_cast<Square>(sq));
    }
  }

  // ------------------------- generation -------------------------

  static inline void generate_all_magics_and_tables()
  {
    build_masks();

    for (int sq = 0; sq < 64; ++sq)
    {
      std::vector<core::Bitboard> table;
      core::Bitboard magic = 0ULL;
      std::uint8_t shift = 0;
      const core::Bitboard mask = g_rook_mask[sq];
      (void)find_magic_for_square(Slider::Rook, sq, mask, magic, shift, table);
      g_rook_magic[sq] = Magic{magic, shift};
      g_rook_table[sq] = std::move(table);
    }
    for (int sq = 0; sq < 64; ++sq)
    {
      std::vector<core::Bitboard> table;
      core::Bitboard magic = 0ULL;
      std::uint8_t shift = 0;
      const core::Bitboard mask = g_bishop_mask[sq];
      (void)find_magic_for_square(Slider::Bishop, sq, mask, magic, shift, table);
      g_bishop_magic[sq] = Magic{magic, shift};
      g_bishop_table[sq] = std::move(table);
    }
  }

  // ---------------------- packing to flat -------------------------

  static inline void pack_magic_vectors_to_flat(const std::array<std::vector<core::Bitboard>, 64> &src,
                                                std::array<std::uint32_t, 64> &off,
                                                std::array<std::uint16_t, 64> &len,
                                                std::vector<core::Bitboard> &arena)
  {
    std::uint32_t cur = 0;

    std::size_t total = 0;
    for (int i = 0; i < 64; ++i)
      total += src[i].empty() ? 1u : src[i].size();

    arena.clear();
    arena.resize(total);

    for (int i = 0; i < 64; ++i)
    {
      off[i] = cur;

      if (src[i].empty())
      {
        len[i] = 1;
        arena[cur] = 0ULL;
        ++cur;
      }
      else
      {
        const std::size_t sz = src[i].size();
        len[i] = static_cast<std::uint16_t>(sz);
        std::copy(src[i].begin(), src[i].end(), arena.begin() + cur);
        cur += static_cast<std::uint32_t>(sz);
      }
    }
  }

  // ---------------------- PEXT build (flat, no per-square tmp allocations) ----

#if defined(LILIA_HAVE_PEXT_INTRINSIC)
  static inline void build_table_for_square_pext_into(core::Bitboard mask, Slider s, int sq,
                                                      core::Bitboard *dst /* size = 1<<bits or 1 */)
  {
    const int bits = core::popcount(mask);
    const std::size_t size = (bits == 0) ? 1ULL : (1ULL << bits);
    std::fill(dst, dst + size, 0ULL);

    foreach_subset(mask, [&](core::Bitboard sub)
                   {
    const std::uint64_t idx = _pext_u64(sub, mask);
    dst[static_cast<std::size_t>(idx)] = brute_attacks(s, static_cast<Square>(sq), sub); });
  }

  static inline void build_all_pext_flat(std::array<std::uint32_t, 64> &off,
                                         std::array<std::uint16_t, 64> &len,
                                         std::vector<core::Bitboard> &arena, Slider s)
  {
    std::uint32_t cur = 0;

    std::size_t total = 0;
    for (int i = 0; i < 64; ++i)
    {
      const core::Bitboard mask = (s == Slider::Rook) ? g_rook_mask[i] : g_bishop_mask[i];
      const int bits = core::popcount(mask);
      total += (bits == 0) ? 1ULL : (1ULL << bits);
    }

    arena.clear();
    arena.resize(total);

    for (int i = 0; i < 64; ++i)
    {
      const core::Bitboard mask = (s == Slider::Rook) ? g_rook_mask[i] : g_bishop_mask[i];
      const int bits = core::popcount(mask);
      const std::uint16_t L = static_cast<std::uint16_t>((bits == 0) ? 1u : (1u << bits));

      off[i] = cur;
      len[i] = L;

      build_table_for_square_pext_into(mask, s, i, arena.data() + cur);
      cur += L;
    }
  }
#endif

  // ---------------------- CPU feature -------------------------

  static bool cpu_has_bmi2()
  {
#if defined(LILIA_HAVE_PEXT_INTRINSIC)
#if defined(__BMI2__)
    return true;
#elif defined(_MSC_VER)
    int regs[4]{};
    __cpuidex(regs, 7, 0);
    return (regs[1] & (1 << 8)) != 0; // EBX bit 8 = BMI2
#else
    unsigned int a = 0, b = 0, c = 0, d = 0;
    if (!__get_cpuid_count(7, 0, &a, &b, &c, &d))
      return false;
    return (b & (1u << 8)) != 0;
#endif
#else
    return false;
#endif
  }

  // ---------------------- init -------------------------

  void init_magics()
  {
#ifdef LILIA_MAGIC_HAVE_CONSTANTS
    using namespace lilia::chess::magic::generated;

    build_masks();

    for (int i = 0; i < 64; ++i)
    {
      g_rook_magic[i] = {srook_magic[i].magic, srook_magic[i].shift};
      g_bishop_magic[i] = {sbishop_magic[i].magic, sbishop_magic[i].shift};
    }

#ifdef LILIA_MAGIC_FLAT_CONSTANTS
    for (int i = 0; i < 64; ++i)
    {
      g_r_off_magic[i] = srook_off[i];
      g_r_len_magic[i] = srook_len[i];
      g_b_off_magic[i] = sbishop_off[i];
      g_b_len_magic[i] = sbishop_len[i];
    }
    g_r_arena_magic.assign(srook_arena, srook_arena + srook_arena_size);
    g_b_arena_magic.assign(sbishop_arena, sbishop_arena + sbishop_arena_size);
#else
    for (int i = 0; i < 64; ++i)
    {
      g_rook_table[i] = srook_table[i];
      g_bishop_table[i] = sbishop_table[i];
    }
    pack_magic_vectors_to_flat(g_rook_table, g_r_off_magic, g_r_len_magic, g_r_arena_magic);
    pack_magic_vectors_to_flat(g_bishop_table, g_b_off_magic, g_b_len_magic, g_b_arena_magic);
#endif

#else
    generate_all_magics_and_tables();
    pack_magic_vectors_to_flat(g_rook_table, g_r_off_magic, g_r_len_magic, g_r_arena_magic);
    pack_magic_vectors_to_flat(g_bishop_table, g_b_off_magic, g_b_len_magic, g_b_arena_magic);

    serialize_magics_to_header("include/lilia/chess/generated/magic_constants.hpp");
#endif

    g_use_pext = cpu_has_bmi2();

#if defined(LILIA_HAVE_PEXT_INTRINSIC)
    if (g_use_pext)
    {
      build_all_pext_flat(g_r_off_pext, g_r_len_pext, g_r_arena_pext, Slider::Rook);
      build_all_pext_flat(g_b_off_pext, g_b_len_pext, g_b_arena_pext, Slider::Bishop);
    }
#else
    g_use_pext = false;
#endif

    // Optional memory trim: keep only flat arenas by default; vectors can be lazily reconstructed if
    // someone calls rook_tables().
#ifndef NDEBUG
    // keep debug introspection memory as-is
#else
    for (auto &v : g_rook_table)
      std::vector<core::Bitboard>().swap(v);
    for (auto &v : g_bishop_table)
      std::vector<core::Bitboard>().swap(v);
#endif
  }

  // ---------------------- query (HOT) -------------------------

  core::Bitboard sliding_attacks(Slider s, Square sq, core::Bitboard occ) noexcept
  {
    const int i = static_cast<int>(sq);

#if defined(LILIA_HAVE_PEXT_INTRINSIC)
    if (LILIA_LIKELY(g_use_pext))
    {
      if (s == Slider::Rook)
      {
        const std::uint32_t off = g_r_off_pext[i];
        const std::uint64_t idx = _pext_u64(occ, g_rook_mask[i]);
        return g_r_arena_pext[off + static_cast<std::uint32_t>(idx)];
      }
      else
      {
        const std::uint32_t off = g_b_off_pext[i];
        const std::uint64_t idx = _pext_u64(occ, g_bishop_mask[i]);
        return g_b_arena_pext[off + static_cast<std::uint32_t>(idx)];
      }
    }
#endif

    if (s == Slider::Rook)
    {
      const std::uint32_t off = g_r_off_magic[i];
      const std::uint32_t idx =
          index_for_occ_fast(occ, g_rook_mask[i], g_rook_magic[i].magic, g_rook_magic[i].shift);
      return g_r_arena_magic[off + idx];
    }
    else
    {
      const std::uint32_t off = g_b_off_magic[i];
      const std::uint32_t idx =
          index_for_occ_fast(occ, g_bishop_mask[i], g_bishop_magic[i].magic, g_bishop_magic[i].shift);
      return g_b_arena_magic[off + idx];
    }
  }

  // ---------------------- getters -------------------------

  const std::array<core::Bitboard, 64> &rook_masks()
  {
    return g_rook_mask;
  }
  const std::array<core::Bitboard, 64> &bishop_masks()
  {
    return g_bishop_mask;
  }
  const std::array<Magic, 64> &rook_magics()
  {
    return g_rook_magic;
  }
  const std::array<Magic, 64> &bishop_magics()
  {
    return g_bishop_magic;
  }

  // Lazy reconstruction: only if someone asks for vector tables (compat).
  static inline void materialize_rook_vectors_if_needed()
  {
    if (!g_rook_table[0].empty())
      return;
    for (int i = 0; i < 64; ++i)
    {
      const std::uint32_t off = g_r_off_magic[i];
      const std::uint16_t L = g_r_len_magic[i];
      g_rook_table[i].assign(g_r_arena_magic.begin() + off, g_r_arena_magic.begin() + off + L);
    }
  }
  static inline void materialize_bishop_vectors_if_needed()
  {
    if (!g_bishop_table[0].empty())
      return;
    for (int i = 0; i < 64; ++i)
    {
      const std::uint32_t off = g_b_off_magic[i];
      const std::uint16_t L = g_b_len_magic[i];
      g_bishop_table[i].assign(g_b_arena_magic.begin() + off, g_b_arena_magic.begin() + off + L);
    }
  }

  const std::array<std::vector<core::Bitboard>, 64> &rook_tables()
  {
    if (LILIA_UNLIKELY(g_rook_table[0].empty()))
      materialize_rook_vectors_if_needed();
    return g_rook_table;
  }

  const std::array<std::vector<core::Bitboard>, 64> &bishop_tables()
  {
    if (LILIA_UNLIKELY(g_bishop_table[0].empty()))
      materialize_bishop_vectors_if_needed();
    return g_bishop_table;
  }

} // namespace lilia::chess::magic
