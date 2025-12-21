// include/lilia/model/tt5.hpp
#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>

#include "move.hpp"  // expects lilia::model::Move etc.

namespace lilia::model {

// -----------------------------------------------------------------------------
// Public entry (for callers)
// -----------------------------------------------------------------------------
enum class Bound : std::uint8_t { Exact = 0, Lower = 1, Upper = 2 };

struct TTEntry5 {
  std::uint64_t key = 0;
  int32_t value = 0;  // score (cp), i16 stored, sign-extended on read
  int16_t depth = 0;  // plies (0..255 stored)
  Bound bound = Bound::Exact;
  Move best{};                                               // move16 packed internally
  std::uint8_t age = 0;                                      // generation (mod 256)
  int16_t staticEval = std::numeric_limits<int16_t>::min();  // INT16_MIN == "unset"
};

// -----------------------------------------------------------------------------
// Tunables / platform helpers
// -----------------------------------------------------------------------------
#if defined(__GNUC__) || defined(__clang__)
#define LILIA_LIKELY(x) (__builtin_expect(!!(x), 1))
#define LILIA_UNLIKELY(x) (__builtin_expect(!!(x), 0))
#define LILIA_PREFETCH_L1(ptr) __builtin_prefetch((ptr), 0, 3)
#define LILIA_PREFETCHW_L1(ptr) __builtin_prefetch((ptr), 1, 3)
#else
#define LILIA_LIKELY(x) (x)
#define LILIA_UNLIKELY(x) (x)
#define LILIA_PREFETCH_L1(ptr) ((void)0)
#define LILIA_PREFETCHW_L1(ptr) ((void)0)
#endif

#ifndef TT5_INDEX_MIX
// 0: use low bits of Zobrist (klassisch), 1: simple mixer (xor-shift)
#define TT5_INDEX_MIX 1
#endif

// -----------------------------------------------------------------------------
// Internal packed entry & cluster
// info bit layout (low -> high):
//  [ 0..15] keyLow16
//  [16..23] age8
//  [24..31] depth8 (plies, clipped 0..255)
//  [32..33] bound2 (0..2)
//  [34..49] keyHigh16
//  [50..62] reserved
//  [63   ] VALID bit (1 = occupied)
// data layout:
//  [0..15] move16 (from6|to6|promo3|cap1)
//  [16..31] value16 (signed)
//  [32..47] staticEval16 (signed)
//  [48..63] keyHigh16 (redundant; left for compat/diagnostics)
// -----------------------------------------------------------------------------
struct TTEntryPacked {
  std::atomic<std::uint64_t> info{0};
  std::atomic<std::uint64_t> data{0};
  TTEntryPacked() = default;
  TTEntryPacked(const TTEntryPacked&) = delete;
  TTEntryPacked& operator=(const TTEntryPacked&) = delete;
};

struct alignas(64) Cluster {
  std::array<TTEntryPacked, 4> e{};
  Cluster() = default;
  Cluster(const Cluster&) = delete;
  Cluster& operator=(const Cluster&) = delete;
};

// -----------------------------------------------------------------------------
// TT5
// -----------------------------------------------------------------------------
class TT5 {
 public:
  explicit TT5(std::size_t mb = 16) { resize(mb); }

  void resize(std::size_t mb) {
    const auto bytes = std::max<std::size_t>(mb, 1) * 1024ull * 1024ull;
    const auto want = bytes / sizeof(Cluster);
    // round down to power of two to keep mask indexing
    slots_ = highest_pow2(want ? want : 1);
    table_.reset(new Cluster[slots_]);
    generation_.store(1u, std::memory_order_relaxed);
  }

  void clear() {
    const auto n = slots_;
    table_.reset();
    table_ = std::unique_ptr<Cluster[]>(new Cluster[n]);
    generation_.store(1u, std::memory_order_relaxed);
  }

  inline void new_generation() noexcept {
    auto g = generation_.fetch_add(1u, std::memory_order_relaxed) + 1u;
    if (g == 0u) generation_.store(1u, std::memory_order_relaxed);
  }

  inline void prefetch(std::uint64_t key) const noexcept { LILIA_PREFETCH_L1(&table_[index(key)]); }

  // --- Probe into user entry ---
  bool probe_into(std::uint64_t key, TTEntry5& out) const noexcept {
    const Cluster& c = table_[index(key)];
    LILIA_PREFETCH_L1(&c);
    for (int i = 0; i < 4; i++) {
      LILIA_PREFETCH_L1(&c.e[i].info);
      LILIA_PREFETCH_L1(&c.e[i].data);
    }

    const std::uint16_t keyLo = static_cast<std::uint16_t>(key);
    const std::uint16_t keyHi = static_cast<std::uint16_t>(key >> 48);

    for (const auto& ent : c.e) {
      std::uint64_t info1 = ent.info.load(std::memory_order_acquire);

      // empty?
      if (LILIA_UNLIKELY((info1 & INFO_VALID_MASK) == 0ull)) continue;

      // key low fast-reject
      if (LILIA_UNLIKELY((info1 & INFO_KEYLO_MASK) != keyLo)) continue;

      // key high fast-reject (from info)
      const std::uint16_t infoKeyHi =
          static_cast<std::uint16_t>((info1 >> INFO_KEYHI_SHIFT) & 0xFFFFu);
      if (LILIA_UNLIKELY(infoKeyHi != keyHi)) continue;

      // read data relaxed
      const std::uint64_t d = ent.data.load(std::memory_order_relaxed);
      // Torn-read/ABA-protection: verify KeyHigh out of the data
      const std::uint16_t dKeyHi = static_cast<std::uint16_t>(d >> 48);
      if (LILIA_UNLIKELY(dKeyHi != keyHi)) continue;

      TTEntry5 tmp{};
      tmp.key = key;
      tmp.age = static_cast<std::uint8_t>((info1 >> INFO_AGE_SHIFT) & 0xFFu);
      tmp.depth = static_cast<int16_t>((info1 >> INFO_DEPTH_SHIFT) & 0xFFu);
      tmp.bound = static_cast<Bound>((info1 >> INFO_BOUND_SHIFT) & 0x3u);

      const std::uint16_t mv16 = static_cast<std::uint16_t>(d & 0xFFFFu);
      tmp.best = unpack_move16(mv16);
      tmp.value = static_cast<int16_t>((d >> 16) & 0xFFFFu);
      tmp.staticEval = static_cast<int16_t>((d >> 32) & 0xFFFFu);

      out = tmp;

      // refresh age if stale (best-effort)
      uint8_t cur = (uint8_t)generation_.load(std::memory_order_relaxed);
      uint8_t entAge = (uint8_t)((info1 >> INFO_AGE_SHIFT) & 0xFFu);
      if ((uint8_t)(cur - entAge) > 8) {
        uint64_t newInfo =
            (info1 & ~(0xFFull << INFO_AGE_SHIFT)) | ((uint64_t)cur << INFO_AGE_SHIFT);
        auto& infoAtomic = const_cast<std::atomic<uint64_t>&>(ent.info);
        (void)infoAtomic.compare_exchange_strong(info1, newInfo, std::memory_order_relaxed);
      }
      return true;
    }

    return false;
  }

  std::optional<TTEntry5> probe(std::uint64_t key) const {
    TTEntry5 tmp{};
    if (probe_into(key, tmp)) return tmp;
    return std::nullopt;
  }
#ifndef TT_DETERMINISTIC
  // --- LIGHT DETERMINISTIC, LOW-OVERHEAD STORE ---
  void store(std::uint64_t key, int32_t value, int16_t depth, Bound bound, const Move& best,
             int16_t staticEval = std::numeric_limits<int16_t>::min()) noexcept {
    Cluster& c = table_[index(key)];
    LILIA_PREFETCHW_L1(&c);

    const std::uint8_t age = static_cast<std::uint8_t>(generation_.load(std::memory_order_relaxed));
    const std::uint16_t keyLo = static_cast<std::uint16_t>(key);
    const std::uint16_t keyHi = static_cast<std::uint16_t>(key >> 48);

    const std::uint8_t depth8 =
        static_cast<std::uint8_t>(depth < 0 ? 0 : (depth > 255 ? 255 : depth));
    const std::int16_t v16 = static_cast<std::int16_t>(std::clamp(
        value, (int)std::numeric_limits<int16_t>::min(), (int)std::numeric_limits<int16_t>::max()));
    const std::int16_t se16 = static_cast<std::int16_t>(std::clamp(
        staticEval, std::numeric_limits<int16_t>::min(), std::numeric_limits<int16_t>::max()));
    const std::uint16_t mv16 = pack_move16(best);

    auto bound_strength = [](Bound b) -> int {
      return b == Bound::Exact ? 2 : (b == Bound::Lower ? 1 : 0);
    };

    auto info_depth = [](std::uint64_t info) -> unsigned {
      return (unsigned)((info >> INFO_DEPTH_SHIFT) & 0xFFu);
    };
    auto info_bound = [](std::uint64_t info) -> Bound {
      return (Bound)((info >> INFO_BOUND_SHIFT) & 0x3u);
    };
    auto info_keyhi = [](std::uint64_t info) -> std::uint16_t {
      return (std::uint16_t)((info >> INFO_KEYHI_SHIFT) & 0xFFFFu);
    };
    auto info_age = [](std::uint64_t info) -> std::uint8_t {
      return (std::uint8_t)((info >> INFO_AGE_SHIFT) & 0xFFu);
    };

    const std::uint64_t newData = (std::uint64_t)mv16 | ((std::uint64_t)(std::uint16_t)v16 << 16) |
                                  ((std::uint64_t)(std::uint16_t)se16 << 32) |
                                  ((std::uint64_t)keyHi << 48);

    const std::uint64_t newInfo = INFO_VALID_MASK | (std::uint64_t)keyLo |
                                  ((std::uint64_t)age << INFO_AGE_SHIFT) |
                                  ((std::uint64_t)depth8 << INFO_DEPTH_SHIFT) |
                                  ((std::uint64_t)(std::uint8_t)bound << INFO_BOUND_SHIFT) |
                                  ((std::uint64_t)keyHi << INFO_KEYHI_SHIFT);

    auto strictly_better = [&](std::uint64_t oldInfo) -> bool {
      // Depth first, then bound strength, then freshness (age), finally key parity.
      const unsigned od = info_depth(oldInfo);
      const Bound ob = info_bound(oldInfo);
      const int obS = bound_strength(ob);
      const int nbS = bound_strength(bound);

      if (depth8 != od) return depth8 > od;
      if (nbS != obS) return nbS > obS;
      const int freshOld = 255 - (uint8_t)(age - info_age(oldInfo));
      const int freshNew = 255;  // new is current generation
      if (freshNew != freshOld) return freshNew > freshOld;
      // final deterministic tie-break:
      return (info_keyhi(oldInfo) & 1u) == 0u;  // replace when old parity is 0
    };

    // 1) Same-key update: single CAS if we are better/equal
    for (auto& ent : c.e) {
      std::uint64_t oldInfo = ent.info.load(std::memory_order_acquire);
      if ((oldInfo & INFO_VALID_MASK) == 0ull) continue;
      if ((oldInfo & INFO_KEYLO_MASK) != keyLo) continue;
      if (info_keyhi(oldInfo) != keyHi) continue;

      if (!strictly_better(oldInfo)) {
        // try to inject a missing move to aid ordering (non-blocking)
        std::uint64_t oldData = ent.data.load(std::memory_order_relaxed);
        uint16_t oldMv = (uint16_t)(oldData & 0xFFFFu);
        uint16_t newMv = mv16;
        if (oldMv == 0 && newMv != 0) {
          std::uint64_t patched = (oldData & ~0xFFFFull) | newMv;
          (void)ent.data.compare_exchange_strong(oldData, patched, std::memory_order_relaxed,
                                                 std::memory_order_relaxed);
        }
        return;
      }

      ent.data.store(newData, std::memory_order_relaxed);
      for (int tries = 0; tries < 2; ++tries) {
        if (ent.info.compare_exchange_strong(oldInfo, newInfo, std::memory_order_release,
                                             std::memory_order_acquire))
          break;
      }
      return;  // regardless of CAS success, don’t loop — avoid stalls
    }

    // 2) Free slot: one CAS
    for (auto& ent : c.e) {
      std::uint64_t expected = 0ull;
      ent.data.store(newData, std::memory_order_relaxed);
      if (ent.info.compare_exchange_strong(expected, newInfo, std::memory_order_release,
                                           std::memory_order_relaxed))
        return;
    }

    // 3) Replacement: pick victim by your heuristic; replace only if strictly better; one CAS try
    int victim = 0;
    int bestScore = repl_score(c.e[0], age);
    for (int i = 1; i < 4; ++i) {
      const int sc = repl_score(c.e[i], age);
      if (sc < bestScore) {
        bestScore = sc;
        victim = i;
      }
    }

    auto& ent = c.e[victim];
    std::uint64_t oldInfo = ent.info.load(std::memory_order_acquire);
    if (!strictly_better(oldInfo)) return;

    ent.data.store(newData, std::memory_order_relaxed);
    (void)ent.info.compare_exchange_strong(oldInfo, newInfo, std::memory_order_release,
                                           std::memory_order_acquire);
    // if CAS fails, we just drop it
  }
#else
  void store(std::uint64_t key, int32_t value, int16_t depth, Bound bound, const Move& best,
             int16_t staticEval = std::numeric_limits<int16_t>::min()) noexcept {
    Cluster& c = table_[index(key)];
    LILIA_PREFETCHW_L1(&c);

    const std::uint8_t curAge =
        static_cast<std::uint8_t>(generation_.load(std::memory_order_relaxed));
    const std::uint16_t keyLo = static_cast<std::uint16_t>(key);
    const std::uint16_t keyHi = static_cast<std::uint16_t>(key >> 48);

    const std::uint8_t depth8 =
        static_cast<std::uint8_t>(depth < 0 ? 0 : (depth > 255 ? 255 : depth));
    const std::int16_t v16 = static_cast<std::int16_t>(
        value < std::numeric_limits<int16_t>::min()   ? std::numeric_limits<int16_t>::min()
        : value > std::numeric_limits<int16_t>::max() ? std::numeric_limits<int16_t>::max()
                                                      : value);
    const std::int16_t se16 = static_cast<std::int16_t>(std::clamp(
        staticEval, std::numeric_limits<int16_t>::min(), std::numeric_limits<int16_t>::max()));
    const std::uint16_t mv16 = pack_move16(best);

    auto bound_strength = [](Bound b) constexpr -> int {
      return b == Bound::Exact ? 2 : (b == Bound::Lower ? 1 : 0);
    };

    auto info_quality = [&](std::uint64_t info) -> uint32_t {
      if ((info & INFO_VALID_MASK) == 0ull) return 0;  // empty
      const std::uint8_t age = static_cast<std::uint8_t>((info >> INFO_AGE_SHIFT) & 0xFFu);
      const std::uint8_t dep = static_cast<std::uint8_t>((info >> INFO_DEPTH_SHIFT) & 0xFFu);
      const Bound bnd = static_cast<Bound>((info >> INFO_BOUND_SHIFT) & 0x3u);
      const std::uint16_t kHi = static_cast<std::uint16_t>((info >> INFO_KEYHI_SHIFT) & 0xFFFFu);
      const int fresh = 255 - static_cast<std::uint8_t>(curAge - age);  // higher = fresher
      // depth (most important) | bound | freshness | deterministic tie (key parity)
      return (static_cast<uint32_t>(dep) << 16) |
             (static_cast<uint32_t>(bound_strength(bnd)) << 12) |
             (static_cast<uint32_t>(fresh) << 4) | (kHi & 0x1u);
    };

    auto new_quality = [&](std::uint16_t kHi) -> uint32_t {
      const int fresh = 255;  // brand-new this generation
      return (static_cast<uint32_t>(depth8) << 16) |
             (static_cast<uint32_t>(bound_strength(bound)) << 12) |
             (static_cast<uint32_t>(fresh) << 4) | (kHi & 0x1u);
    };

    const std::uint64_t newData =
        (static_cast<std::uint64_t>(mv16) & 0xFFFFull) |
        (static_cast<std::uint64_t>(static_cast<std::uint16_t>(v16)) << 16) |
        (static_cast<std::uint64_t>(static_cast<std::uint16_t>(se16)) << 32) |
        (static_cast<std::uint64_t>(keyHi) << 48);

    const std::uint64_t newInfoTemplate =
        INFO_VALID_MASK | (static_cast<std::uint64_t>(keyLo)) |
        (static_cast<std::uint64_t>(curAge) << INFO_AGE_SHIFT) |
        (static_cast<std::uint64_t>(depth8) << INFO_DEPTH_SHIFT) |
        (static_cast<std::uint64_t>(static_cast<std::uint8_t>(bound)) << INFO_BOUND_SHIFT) |
        (static_cast<std::uint64_t>(keyHi) << INFO_KEYHI_SHIFT);

    const uint32_t newQ = new_quality(keyHi);

    // 1) Try same-key update with CAS + deterministic predicate
    for (auto& ent : c.e) {
      std::uint64_t oldInfo = ent.info.load(std::memory_order_acquire);
      if ((oldInfo & INFO_VALID_MASK) == 0ull) continue;
      if ((oldInfo & INFO_KEYLO_MASK) != keyLo) continue;
      const std::uint16_t infoKeyHi =
          static_cast<std::uint16_t>((oldInfo >> INFO_KEYHI_SHIFT) & 0xFFFFu);
      if (infoKeyHi != keyHi) continue;

      // If the existing entry is "better or equal", keep it.
      const uint32_t oldQ = info_quality(oldInfo);
      if (oldQ > newQ) return;

      // Otherwise, attempt to replace deterministically.
      ent.data.store(newData, std::memory_order_relaxed);
      if (ent.info.compare_exchange_strong(oldInfo, newInfoTemplate, std::memory_order_release,
                                           std::memory_order_acquire)) {
        return;
      }
      // CAS failed -> someone else updated; restart same-key loop.
      // We could loop a few times, but a single restart of the outer loop suffices.
      // (fall through to fresh-slot / replacement below)
    }

    // 2) Try to claim a free slot (CAS from empty)
    for (auto& ent : c.e) {
      std::uint64_t expected = 0ull;
      ent.data.store(newData, std::memory_order_relaxed);
      if (ent.info.compare_exchange_strong(expected, newInfoTemplate, std::memory_order_release,
                                           std::memory_order_relaxed)) {
        return;
      }
    }

    // 3) Replacement: choose victim by your heuristic, but guard with quality CAS
    int victim = 0;
    int bestScore = repl_score(c.e[0], curAge);
    for (int i = 1; i < 4; ++i) {
      const int sc = repl_score(c.e[i], curAge);
      if (sc < bestScore) {
        bestScore = sc;
        victim = i;
      }
    }

    auto& ent = c.e[victim];

    for (int tries = 0; tries < 4; ++tries) {
      std::uint64_t oldInfo = ent.info.load(std::memory_order_acquire);
      const uint32_t oldQ = info_quality(oldInfo);
      if (oldQ > newQ) return;  // Victim is actually stronger — keep it (deterministic).

      ent.data.store(newData, std::memory_order_relaxed);
      if (ent.info.compare_exchange_strong(oldInfo, newInfoTemplate, std::memory_order_release,
                                           std::memory_order_acquire)) {
        return;
      }
      // someone else changed the victim; retry a couple times
    }
    // Give up silently if contention is extreme.
  }
#endif

 private:
  // --- bitfield constants ---
  static constexpr std::uint64_t INFO_KEYLO_MASK = 0xFFFFull;
  static constexpr unsigned INFO_AGE_SHIFT = 16;
  static constexpr unsigned INFO_DEPTH_SHIFT = 24;
  static constexpr unsigned INFO_BOUND_SHIFT = 32;
  static constexpr unsigned INFO_KEYHI_SHIFT = 34;
  static constexpr std::uint64_t INFO_VALID_MASK = (1ull << 63);

  // --- move packing (16 bit) ---
  static inline std::uint16_t promo_to3(core::PieceType p) noexcept {
    switch (p) {
      case core::PieceType::Knight:
        return 1;
      case core::PieceType::Bishop:
        return 2;
      case core::PieceType::Rook:
        return 3;
      case core::PieceType::Queen:
        return 4;
      default:
        return 0;
    }
  }
  static inline core::PieceType promo_from3(uint16_t v) noexcept {
    switch (v & 0x7) {
      case 1:
        return core::PieceType::Knight;
      case 2:
        return core::PieceType::Bishop;
      case 3:
        return core::PieceType::Rook;
      case 4:
        return core::PieceType::Queen;
      default:
        return core::PieceType::None;
    }
  }
  static inline std::uint16_t pack_move16(const Move& m) noexcept {
    const uint16_t from = (unsigned)m.from() & 0x3F;
    const uint16_t to = (unsigned)m.to() & 0x3F;
    const uint16_t pr3 = promo_to3(m.promotion()) & 0x7;
    const uint16_t cap = m.isCapture() ? 1u : 0u;
    return (uint16_t)(from | (to << 6) | (pr3 << 12) | (cap << 15));
  }
  static inline Move unpack_move16(std::uint16_t v) noexcept {
    Move m{};
    m.set_from(static_cast<core::Square>(v & 0x3F));
    m.set_to(static_cast<core::Square>((v >> 6) & 0x3F));
    m.set_promotion(promo_from3((v >> 12) & 0x7));
    m.set_capture(((v >> 15) & 1u) != 0);
    m.set_enpassant(false);
    m.set_castle(CastleSide::None);
    return m;
  }

  // --- replacement score: lower is worse (chosen as victim) ---
  static inline int repl_score(const TTEntryPacked& ent, std::uint8_t curAge) noexcept {
    const std::uint64_t info = ent.info.load(std::memory_order_relaxed);
    if ((info & INFO_VALID_MASK) == 0ull)
      return std::numeric_limits<int>::min();  // empty → best victim

    const std::uint8_t age = static_cast<std::uint8_t>((info >> INFO_AGE_SHIFT) & 0xFFu);
    const std::uint8_t dep = static_cast<std::uint8_t>((info >> INFO_DEPTH_SHIFT) & 0xFFu);
    const Bound bnd = static_cast<Bound>((info >> INFO_BOUND_SHIFT) & 0x3u);

    const int boundBias = (bnd == Bound::Exact ? 12 : (bnd == Bound::Lower ? 4 : 0));
    const int ageDelta = (uint8_t)(curAge - age);
    // heavier depth weight + bound bias; penalize staleness stronger
    // bonus: if data has a mate score, bump strongly
    const int mateBias = 0;  // default 0, see below for optional decode
    // const uint64_t d = ent.data.load(std::memory_order_relaxed);
    // const int16_t v16 = (int16_t)((d >> 16) & 0xFFFFu);
    // const int isMate = (std::abs((int)v16) >= MATE_THR) ? 64 : 0;
    // mateBias = isMate;
    return (int)dep * 512 + boundBias + mateBias - (ageDelta * 2);
  }

  static inline void write_entry(TTEntryPacked& ent, std::uint16_t keyLo, std::uint16_t keyHi,
                                 std::uint8_t age, std::uint8_t depth8, Bound bound,
                                 std::uint16_t mv16, std::int16_t v16, std::int16_t se16) noexcept {
    const std::uint64_t newData =
        (static_cast<std::uint64_t>(mv16) & 0xFFFFull) |
        (static_cast<std::uint64_t>(static_cast<std::uint16_t>(v16)) << 16) |
        (static_cast<std::uint64_t>(static_cast<std::uint16_t>(se16)) << 32) |
        (static_cast<std::uint64_t>(keyHi) << 48);

    const std::uint64_t newInfo =
        INFO_VALID_MASK | (static_cast<std::uint64_t>(keyLo)) |
        (static_cast<std::uint64_t>(age) << INFO_AGE_SHIFT) |
        (static_cast<std::uint64_t>(depth8) << INFO_DEPTH_SHIFT) |
        (static_cast<std::uint64_t>(static_cast<std::uint8_t>(bound)) << INFO_BOUND_SHIFT) |
        (static_cast<std::uint64_t>(keyHi) << INFO_KEYHI_SHIFT);

    ent.data.store(newData, std::memory_order_relaxed);
    ent.info.store(newInfo, std::memory_order_release);
  }

  inline std::size_t index(std::uint64_t key) const noexcept {
    // slots_ is power-of-two
#if TT5_INDEX_MIX
    uint64_t x = key + 0x9E3779B97F4A7C15ull;
    x ^= x >> 30;
    x *= 0xBF58476D1CE4E5B9ull;
    x ^= x >> 27;
    x *= 0x94D049BB133111EBull;
    x ^= x >> 31;
    uint64_t h = x;
    return static_cast<std::size_t>(h) & (slots_ - 1);
#else
    return static_cast<std::size_t>(key) & (slots_ - 1);
#endif
  }

  static inline std::size_t highest_pow2(std::size_t x) noexcept {
    if (x == 0) return 1;
#if defined(__GNUC__) || defined(__clang__)
    // next lower power of two
    int lz = __builtin_clzll(static_cast<unsigned long long>(x));
    std::size_t p = 1ull << (63 - lz);
    if (p > x) p >>= 1;
    return p ? p : 1;
#else
    // portable fallback
    std::size_t p = 1;
    while ((p << 1) && ((p << 1) <= x)) p <<= 1;
    return p;
#endif
  }

  std::unique_ptr<Cluster[]> table_;
  std::size_t slots_ = 1;
  std::atomic<std::uint32_t> generation_{1u};
};

}  // namespace lilia::model
