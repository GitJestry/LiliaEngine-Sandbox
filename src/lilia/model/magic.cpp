#include "lilia/model/core/magic.hpp"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <functional>
#include <iostream>
#include <limits>
#include <random>
#include <vector>

#include "lilia/model/core/random.hpp"
#include "lilia/model/generated/magic_constants.hpp"
#include "lilia/model/magic_serializer.hpp"

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386) || defined(_M_IX86)
#if defined(__BMI2__) || defined(_MSC_VER)
#include <immintrin.h>  // _pext_u64
#define LILIA_HAVE_PEXT_INTRINSIC 1
#endif
#if !defined(_MSC_VER) && !defined(__BMI2__)
#include <cpuid.h>
#endif
#endif

namespace lilia::model::magic {

static std::array<bb::Bitboard, 64> g_rook_mask{};
static std::array<bb::Bitboard, 64> g_bishop_mask{};

static std::array<Magic, 64> g_rook_magic{};
static std::array<Magic, 64> g_bishop_magic{};

// -------------------- alte (kompatible) Vektor-Tabellen --------------------
static std::array<std::vector<bb::Bitboard>, 64> g_rook_table;
static std::array<std::vector<bb::Bitboard>, 64> g_bishop_table;

// -------------------- NEU: flache Arenen + Offsets/Längen ------------------
// Magic-indiziert
static std::array<std::uint32_t, 64> g_r_off_magic{}, g_b_off_magic{};
static std::array<std::uint16_t, 64> g_r_len_magic{}, g_b_len_magic{};
static std::vector<bb::Bitboard> g_r_arena_magic, g_b_arena_magic;

// PEXT-indiziert
static std::array<std::uint32_t, 64> g_r_off_pext{}, g_b_off_pext{};
static std::array<std::uint16_t, 64> g_r_len_pext{}, g_b_len_pext{};
static std::vector<bb::Bitboard> g_r_arena_pext, g_b_arena_pext;

static bool g_use_pext = false;  // CPU-Dispatch

// ----------------------------------------------------------------------------

template <class F>
static inline void foreach_subset(bb::Bitboard mask, F&& f) {
  bb::Bitboard sub = mask;
  while (true) {
    f(sub);
    if (sub == 0) break;
    sub = (sub - 1) & mask;
  }
}

static inline bb::Bitboard brute_rook(core::Square sq, bb::Bitboard occ) {
  return bb::rook_attacks(sq, occ);
}
static inline bb::Bitboard brute_bishop(core::Square sq, bb::Bitboard occ) {
  return bb::bishop_attacks(sq, occ);
}
static inline bb::Bitboard brute_attacks(Slider s, core::Square sq, bb::Bitboard occ) {
  return (s == Slider::Rook) ? brute_rook(sq, occ) : brute_bishop(sq, occ);
}

static inline uint64_t index_for_occ(bb::Bitboard occ, bb::Bitboard mask, bb::Bitboard magic,
                                     uint8_t shift) {
  const int bits = bb::popcount(mask);
  if (bits == 0) return 0ULL;
  const bb::Bitboard subset = occ & mask;
  const uint64_t raw = static_cast<uint64_t>((subset * magic) >> shift);
  const uint64_t mask_idx =
      (bits >= 64) ? std::numeric_limits<uint64_t>::max() : ((1ULL << bits) - 1ULL);
  return raw & mask_idx;
}

static inline void build_table_for_square(Slider s, int sq, bb::Bitboard mask, bb::Bitboard magic,
                                          uint8_t shift, std::vector<bb::Bitboard>& outTable) {
  const int bits = bb::popcount(mask);
  const size_t tableSize = (bits >= 64) ? 0 : (1ULL << bits);
  const size_t allocSize = (tableSize == 0) ? 1 : tableSize;
  outTable.assign(allocSize, 0ULL);

  foreach_subset(mask, [&](bb::Bitboard occSubset) {
    const uint64_t index = index_for_occ(occSubset, mask, magic, shift);
    outTable[index] = brute_attacks(s, static_cast<core::Square>(sq), occSubset);
  });
}

static inline bool try_magic_for_square(Slider s, int sq, bb::Bitboard mask, bb::Bitboard magic,
                                        uint8_t shift, std::vector<bb::Bitboard>& outTable) {
  const int bits = bb::popcount(mask);
  const size_t tableSize = (bits >= 64) ? 0 : (1ULL << bits);
  const size_t allocSize = (tableSize == 0) ? 1 : tableSize;

  std::vector<char> used(allocSize);
  std::vector<bb::Bitboard> table(allocSize);

  bb::Bitboard occSubset = mask;
  while (true) {
    const uint64_t idx = index_for_occ(occSubset, mask, magic, shift);
    const bb::Bitboard atk = brute_attacks(s, static_cast<core::Square>(sq), occSubset);

    if (!used[idx]) {
      used[idx] = 1;
      table[idx] = atk;
    } else if (table[idx] != atk) {
      return false;
    }

    if (occSubset == 0) break;
    occSubset = (occSubset - 1) & mask;
  }

  outTable = std::move(table);
  return true;
}

static inline bool find_magic_for_square(Slider s, int sq, bb::Bitboard mask,
                                         bb::Bitboard& out_magic, uint8_t& out_shift,
                                         std::vector<bb::Bitboard>& outTable) {
  const int bits = bb::popcount(mask);
  const uint8_t shift =
      static_cast<uint8_t>((bits == 0) ? 64u : (64u - static_cast<uint8_t>(bits)));
  out_shift = shift;

  bb::Bitboard seed = 0xC0FFEE123456789ULL ^ (static_cast<bb::Bitboard>(sq) << 32) ^
                      (s == Slider::Rook ? 0xF0F0F0F0ULL : 0x0F0F0F0FULL);

  constexpr int MAX_ATTEMPTS = 2'000'000;
  random::SplitMix64 splitmix(seed);

  auto gen_candidate = [&](int strategy) -> bb::Bitboard {
    switch (strategy) {
      case 0:
        return splitmix.next() & splitmix.next() & splitmix.next();
      case 1:
        return splitmix.next() & splitmix.next();
      case 2:
        return splitmix.next() ^ (splitmix.next() << 1);
      case 3: {
        bb::Bitboard v = splitmix.next() & splitmix.next();
        bb::Bitboard hi = (splitmix.next() & 0xFFULL) << 56;
        return v | hi;
      }
      default:
        return splitmix.next();
    }
  };

  for (int attempt = 0; attempt < MAX_ATTEMPTS; ++attempt) {
    for (int strat = 0; strat < 4; ++strat) {
      bb::Bitboard cand = gen_candidate(strat);
      if (bits > 0) {
        const int highpop = bb::popcount((cand * mask) & 0xFF00000000000000ULL);
        if (highpop < 2) continue;
      }
      if (try_magic_for_square(s, sq, mask, cand, shift, outTable)) {
        out_magic = cand;
        return true;
      }
    }
#ifndef NDEBUG
    if ((attempt & 0xFFF) == 0) {
      std::cerr << "find_magic for sq=" << sq << " attempt=" << attempt << "\n";
    }
#endif
  }
#ifndef NDEBUG
  std::cerr << "find_magic_for_square FAILED (sq=" << sq << ", bits=" << bits << ")\n";
#endif
  return false;
}

static inline bb::Bitboard rook_relevant_mask(core::Square sq) {
  bb::Bitboard mask = 0ULL;
  int r = bb::rank_of(sq), f = bb::file_of(sq);
  for (int rr = r + 1; rr <= 6; ++rr) mask |= bb::sq_bb(static_cast<core::Square>(rr * 8 + f));
  for (int rr = r - 1; rr >= 1; --rr) mask |= bb::sq_bb(static_cast<core::Square>(rr * 8 + f));
  for (int ff = f + 1; ff <= 6; ++ff) mask |= bb::sq_bb(static_cast<core::Square>(r * 8 + ff));
  for (int ff = f - 1; ff >= 1; --ff) mask |= bb::sq_bb(static_cast<core::Square>(r * 8 + ff));
  return mask;
}
static inline bb::Bitboard bishop_relevant_mask(core::Square sq) {
  bb::Bitboard mask = 0ULL;
  int r = bb::rank_of(sq), f = bb::file_of(sq);
  for (int rr = r + 1, ff = f + 1; rr <= 6 && ff <= 6; ++rr, ++ff)
    mask |= bb::sq_bb(static_cast<core::Square>(rr * 8 + ff));
  for (int rr = r + 1, ff = f - 1; rr <= 6 && ff >= 1; ++rr, --ff)
    mask |= bb::sq_bb(static_cast<core::Square>(rr * 8 + ff));
  for (int rr = r - 1, ff = f + 1; rr >= 1 && ff <= 6; --rr, ++ff)
    mask |= bb::sq_bb(static_cast<core::Square>(rr * 8 + ff));
  for (int rr = r - 1, ff = f - 1; rr >= 1 && ff >= 1; --rr, --ff)
    mask |= bb::sq_bb(static_cast<core::Square>(rr * 8 + ff));
  return mask;
}

static inline void build_masks() {
  for (int sq = 0; sq < 64; ++sq) {
    g_rook_mask[sq] = rook_relevant_mask(static_cast<core::Square>(sq));
    g_bishop_mask[sq] = bishop_relevant_mask(static_cast<core::Square>(sq));
  }
}

// ------------------------- Magics & Vektor-Tabellen -------------------------

static inline void generate_all_magics_and_tables() {
  build_masks();

  for (int sq = 0; sq < 64; ++sq) {
    std::vector<bb::Bitboard> table;
    bb::Bitboard magic = 0ULL;
    uint8_t shift = 0;
    const bb::Bitboard mask = g_rook_mask[sq];
    (void)find_magic_for_square(Slider::Rook, sq, mask, magic, shift, table);
    g_rook_magic[sq] = Magic{magic, shift};
    g_rook_table[sq] = std::move(table);
  }
  for (int sq = 0; sq < 64; ++sq) {
    std::vector<bb::Bitboard> table;
    bb::Bitboard magic = 0ULL;
    uint8_t shift = 0;
    const bb::Bitboard mask = g_bishop_mask[sq];
    (void)find_magic_for_square(Slider::Bishop, sq, mask, magic, shift, table);
    g_bishop_magic[sq] = Magic{magic, shift};
    g_bishop_table[sq] = std::move(table);
  }
}

// ---------------------- Flat-Pack (Magic-indiziert) -------------------------

static inline void pack_magic_vectors_to_flat(const std::array<std::vector<bb::Bitboard>, 64>& src,
                                              std::array<std::uint32_t, 64>& off,
                                              std::array<std::uint16_t, 64>& len,
                                              std::vector<bb::Bitboard>& arena) {
  std::uint32_t cur = 0;
  arena.clear();
  arena.reserve(1 << 16);

  for (int i = 0; i < 64; ++i) {
    off[i] = cur;
    if (src[i].empty()) {
      len[i] = 1;
      arena.push_back(0);
      ++cur;
    } else {
      len[i] = static_cast<std::uint16_t>(src[i].size());
      arena.insert(arena.end(), src[i].begin(), src[i].end());
      cur += len[i];
    }
  }
}

// ---------------------- PEXT-Tabellen (flat) --------------------------------

static inline void build_table_for_square_pext(Slider s, int sq, bb::Bitboard mask,
                                               std::vector<bb::Bitboard>& out) {
  const int bits = bb::popcount(mask);
  const size_t size = (bits == 0 ? 1 : (1ULL << bits));
  out.assign(size, 0ULL);
#if LILIA_HAVE_PEXT_INTRINSIC
  foreach_subset(mask, [&](bb::Bitboard sub) {
    const uint64_t idx = _pext_u64(sub, mask);
    out[idx] = brute_attacks(s, static_cast<core::Square>(sq), sub);
  });
#else
  // nicht aufgerufen ohne BMI2
  out[0] = 0;
#endif
}

static inline void build_all_pext_flat(std::array<std::uint32_t, 64>& off,
                                       std::array<std::uint16_t, 64>& len,
                                       std::vector<bb::Bitboard>& arena, Slider s) {
  std::uint32_t cur = 0;
  arena.clear();
  arena.reserve(1 << 16);

  for (int i = 0; i < 64; ++i) {
    const bb::Bitboard mask = (s == Slider::Rook) ? g_rook_mask[i] : g_bishop_mask[i];
    const int bits = bb::popcount(mask);
    const std::uint16_t L = static_cast<std::uint16_t>(bits == 0 ? 1 : (1u << bits));
    off[i] = cur;
    len[i] = L;

    std::vector<bb::Bitboard> tmp;
    build_table_for_square_pext(s, i, mask, tmp);
    arena.insert(arena.end(), tmp.begin(), tmp.end());
    cur += L;
  }
}

// ---------------------- CPU Feature -----------------------------------------

static bool cpu_has_bmi2() {
#if defined(LILIA_HAVE_PEXT_INTRINSIC)
#if defined(__BMI2__)
  return true;
#elif defined(_MSC_VER)
  int regs[4]{};
  __cpuidex(regs, 7, 0);
  return (regs[1] & (1 << 8)) != 0;  // EBX bit 8 = BMI2
#else
  unsigned int a = 0, b = 0, c = 0, d = 0;
  if (!__get_cpuid_count(7, 0, &a, &b, &c, &d)) return false;
  return (b & (1u << 8)) != 0;
#endif
#else
  return false;
#endif
}

// ---------------------- Init -------------------------------------------------

void init_magics() {
#ifdef LILIA_MAGIC_HAVE_CONSTANTS
  using namespace lilia::model::magic::constants;

  build_masks();

  // Magic Konstanten
  for (int i = 0; i < 64; ++i) {
    g_rook_magic[i] = {srook_magic[i].magic, srook_magic[i].shift};
    g_bishop_magic[i] = {sbishop_magic[i].magic, sbishop_magic[i].shift};
  }

#ifdef LILIA_MAGIC_FLAT_CONSTANTS
  for (int i = 0; i < 64; ++i) {
    g_r_off_magic[i] = srook_off[i];
    g_r_len_magic[i] = srook_len[i];
    g_b_off_magic[i] = sbishop_off[i];
    g_b_len_magic[i] = sbishop_len[i];
  }
  g_r_arena_magic.assign(srook_arena, srook_arena + srook_arena_size);
  g_b_arena_magic.assign(sbishop_arena, sbishop_arena + sbishop_arena_size);
#else
  for (int i = 0; i < 64; ++i) {
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

  serialize_magics_to_header("include/lilia/model/generated/magic_constants.hpp");
#endif

  // PEXT-Fast-Path opportunistisch
  g_use_pext = cpu_has_bmi2();
#if defined(LILIA_HAVE_PEXT_INTRINSIC)
  if (g_use_pext) {
    build_all_pext_flat(g_r_off_pext, g_r_len_pext, g_r_arena_pext, Slider::Rook);
    build_all_pext_flat(g_b_off_pext, g_b_len_pext, g_b_arena_pext, Slider::Bishop);
  }
#else
  g_use_pext = false;
#endif
}

// ---------------------- Query -----------------------------------------------

bb::Bitboard sliding_attacks(Slider s, core::Square sq, bb::Bitboard occ) noexcept {
  const int i = static_cast<int>(sq);

#if defined(LILIA_HAVE_PEXT_INTRINSIC)
  if (g_use_pext) {
    if (s == Slider::Rook) {
      const std::uint32_t off = g_r_off_pext[i];
      const std::uint64_t idx = _pext_u64(occ, g_rook_mask[i]);
      return g_r_arena_pext[off + static_cast<std::uint32_t>(idx)];
    } else {
      const std::uint32_t off = g_b_off_pext[i];
      const std::uint64_t idx = _pext_u64(occ, g_bishop_mask[i]);
      return g_b_arena_pext[off + static_cast<std::uint32_t>(idx)];
    }
  }
#endif

  if (s == Slider::Rook) {
    const std::uint32_t off = g_r_off_magic[i];
    const std::uint64_t idx =
        index_for_occ(occ, g_rook_mask[i], g_rook_magic[i].magic, g_rook_magic[i].shift);
    return g_r_arena_magic[off + static_cast<std::uint32_t>(idx)];
  } else {
    const std::uint32_t off = g_b_off_magic[i];
    const std::uint64_t idx =
        index_for_occ(occ, g_bishop_mask[i], g_bishop_magic[i].magic, g_bishop_magic[i].shift);
    return g_b_arena_magic[off + static_cast<std::uint32_t>(idx)];
  }
}

// (optionale Getter – unverändert)
const std::array<bb::Bitboard, 64>& rook_masks() {
  return g_rook_mask;
}
const std::array<bb::Bitboard, 64>& bishop_masks() {
  return g_bishop_mask;
}
const std::array<Magic, 64>& rook_magics() {
  return g_rook_magic;
}
const std::array<Magic, 64>& bishop_magics() {
  return g_bishop_magic;
}
// Die alten vector<...>-Getter bleiben zur Kompatibilität bestehen:
const std::array<std::vector<bb::Bitboard>, 64>& rook_tables() {
  return g_rook_table;
}
const std::array<std::vector<bb::Bitboard>, 64>& bishop_tables() {
  return g_bishop_table;
}

}  // namespace lilia::model::magic
