// include/lilia/model/tt5.hpp
#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
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
  Move best{};
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
// 0: use low bits of Zobrist (fastest; Zobrist already random)
// 1: simple xorshift mix (cheap; can help if keys are not perfectly random)
#define TT5_INDEX_MIX 0
#endif

// -----------------------------------------------------------------------------
// Internal packed entry & cluster
// info bit layout (low -> high):
//  [ 0..15] keyLow16
//  [16..23] age8
//  [24..31] depth8
//  [32..33] bound2
//  [34..49] keyHigh16
//  [50..61] reserved
//  [62   ] BUSY bit (in-progress write; VALID=0 while BUSY=1)
//  [63   ] VALID bit (1 = occupied)
// data layout:
//  [0..15]  move16 (from6|to6|promo3|cap1)
//  [16..31] value16 (signed)
//  [32..47] staticEval16 (signed)
//  [48..63] keyHigh16 (redundant; ABA/torn-read guard)
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
    slots_ = highest_pow2(want ? want : 1);
    mask_ = slots_ - 1;
    table_.reset(new Cluster[slots_]);
    generation_.store(1u, std::memory_order_relaxed);
  }

  // Clear in-place (avoid dealloc/realloc; preserves virtual memory mapping)
  void clear() {
    if (!table_) return;
    for (std::size_t i = 0; i < slots_; ++i) {
      auto& c = table_[i];
      for (auto& ent : c.e) {
        ent.info.store(0ull, std::memory_order_relaxed);
        ent.data.store(0ull, std::memory_order_relaxed);
      }
    }
    generation_.store(1u, std::memory_order_relaxed);
  }

  inline void new_generation() noexcept {
    std::uint32_t g = generation_.fetch_add(1u, std::memory_order_relaxed) + 1u;
    if (g == 0u) generation_.store(1u, std::memory_order_relaxed);
  }

  inline void prefetch(std::uint64_t key) const noexcept { LILIA_PREFETCH_L1(&table_[index(key)]); }

  // --- Probe into user entry ---
  bool probe_into(std::uint64_t key, TTEntry5& out) const noexcept {
    const Cluster& c = table_[index(key)];
    LILIA_PREFETCH_L1(&c);

    const std::uint16_t keyLo = static_cast<std::uint16_t>(key);
    const std::uint16_t keyHi = static_cast<std::uint16_t>(key >> 48);

    for (const auto& ent : c.e) {
      const std::uint64_t info1 = ent.info.load(std::memory_order_acquire);

      // occupied?
      if (LILIA_UNLIKELY((info1 & INFO_VALID_MASK) == 0ull)) continue;

      // key low fast-reject
      if (LILIA_UNLIKELY((info1 & INFO_KEYLO_MASK) != keyLo)) continue;

      // key high fast-reject (from info)
      const std::uint16_t infoKeyHi =
          static_cast<std::uint16_t>((info1 >> INFO_KEYHI_SHIFT) & 0xFFFFu);
      if (LILIA_UNLIKELY(infoKeyHi != keyHi)) continue;

      // read data
      const std::uint64_t d = ent.data.load(std::memory_order_relaxed);

      // ABA/torn-read protection: verify KeyHigh out of the data
      const std::uint16_t dKeyHi = static_cast<std::uint16_t>(d >> 48);
      if (LILIA_UNLIKELY(dKeyHi != keyHi)) continue;

      // Fill output directly (avoid tmp+copy)
      out.key = key;
      out.age = static_cast<std::uint8_t>((info1 >> INFO_AGE_SHIFT) & 0xFFu);
      out.depth = static_cast<int16_t>((info1 >> INFO_DEPTH_SHIFT) & 0xFFu);
      out.bound = static_cast<Bound>((info1 >> INFO_BOUND_SHIFT) & 0x3u);

      const std::uint16_t mv16 = static_cast<std::uint16_t>(d & 0xFFFFu);
      out.best = unpack_move16(mv16);
      out.value = static_cast<int16_t>((d >> 16) & 0xFFFFu);
      out.staticEval = static_cast<int16_t>((d >> 32) & 0xFFFFu);

      // refresh age if stale (best-effort)
      const std::uint8_t cur =
          static_cast<std::uint8_t>(generation_.load(std::memory_order_relaxed));
      const std::uint8_t entAge = static_cast<std::uint8_t>((info1 >> INFO_AGE_SHIFT) & 0xFFu);
      if (static_cast<std::uint8_t>(cur - entAge) > 8) {
        std::uint64_t expected = info1;
        const std::uint64_t newInfo = (info1 & ~(0xFFull << INFO_AGE_SHIFT)) |
                                      (static_cast<std::uint64_t>(cur) << INFO_AGE_SHIFT);
        auto& infoAtomic = const_cast<std::atomic<std::uint64_t>&>(ent.info);
        (void)infoAtomic.compare_exchange_strong(expected, newInfo, std::memory_order_relaxed);
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

    // Manual clamp (faster/clearer than std::clamp in hot code)
    const int32_t vClamped = value<(int32_t)std::numeric_limits<int16_t>::min()
                                       ? (int32_t)std::numeric_limits<int16_t>::min()
                                       : value>(int32_t) std::numeric_limits<int16_t>::max()
                                 ? (int32_t)std::numeric_limits<int16_t>::max()
                                 : value;
    const int32_t seClamped = staticEval < std::numeric_limits<int16_t>::min()
                                  ? (int32_t)std::numeric_limits<int16_t>::min()
                              : staticEval > std::numeric_limits<int16_t>::max()
                                  ? (int32_t)std::numeric_limits<int16_t>::max()
                                  : (int32_t)staticEval;

    const std::int16_t v16 = static_cast<std::int16_t>(vClamped);
    const std::int16_t se16 = static_cast<std::int16_t>(seClamped);
    const std::uint16_t mv16 = pack_move16(best);

    const std::uint64_t newData =
        (static_cast<std::uint64_t>(mv16) & 0xFFFFull) |
        (static_cast<std::uint64_t>(static_cast<std::uint16_t>(v16)) << 16) |
        (static_cast<std::uint64_t>(static_cast<std::uint16_t>(se16)) << 32) |
        (static_cast<std::uint64_t>(keyHi) << 48);

    const std::uint64_t newInfoFinal =
        INFO_VALID_MASK | (static_cast<std::uint64_t>(keyLo)) |
        (static_cast<std::uint64_t>(age) << INFO_AGE_SHIFT) |
        (static_cast<std::uint64_t>(depth8) << INFO_DEPTH_SHIFT) |
        (static_cast<std::uint64_t>(static_cast<std::uint8_t>(bound)) << INFO_BOUND_SHIFT) |
        (static_cast<std::uint64_t>(keyHi) << INFO_KEYHI_SHIFT);

    const std::uint64_t newInfoBusy = (newInfoFinal & ~INFO_VALID_MASK) | INFO_BUSY_MASK;

    auto bound_strength = [](Bound b) -> int {
      return b == Bound::Exact ? 2 : (b == Bound::Lower ? 1 : 0);
    };

    auto strictly_better = [&](std::uint64_t oldInfo) -> bool {
      const unsigned od = static_cast<unsigned>((oldInfo >> INFO_DEPTH_SHIFT) & 0xFFu);
      const Bound ob = static_cast<Bound>((oldInfo >> INFO_BOUND_SHIFT) & 0x3u);

      const int obS = bound_strength(ob);
      const int nbS = bound_strength(bound);

      if (depth8 != od) return depth8 > od;
      if (nbS != obS) return nbS > obS;

      const std::uint8_t oldAge = static_cast<std::uint8_t>((oldInfo >> INFO_AGE_SHIFT) & 0xFFu);
      const int freshOld = 255 - static_cast<std::uint8_t>(age - oldAge);
      if (freshOld != 255) return true;  // prefer fresh

      // deterministic tie-break: replace when old parity is 0
      const std::uint16_t oldKeyHi =
          static_cast<std::uint16_t>((oldInfo >> INFO_KEYHI_SHIFT) & 0xFFFFu);
      return (oldKeyHi & 1u) == 0u;
    };

    // 1) Same-key update (safe, no collateral damage): lock via BUSY, then publish
    for (auto& ent : c.e) {
      std::uint64_t oldInfo = ent.info.load(std::memory_order_acquire);
      if ((oldInfo & INFO_VALID_MASK) == 0ull) continue;
      if (LILIA_UNLIKELY(oldInfo & INFO_BUSY_MASK)) continue;

      if ((oldInfo & INFO_KEYLO_MASK) != keyLo) continue;
      const std::uint16_t oldKeyHi =
          static_cast<std::uint16_t>((oldInfo >> INFO_KEYHI_SHIFT) & 0xFFFFu);
      if (oldKeyHi != keyHi) continue;

      if (!strictly_better(oldInfo)) {
        // inject missing move (non-blocking) if old move is 0
        std::uint64_t oldData = ent.data.load(std::memory_order_relaxed);
        const std::uint16_t oldMv = static_cast<std::uint16_t>(oldData & 0xFFFFu);
        if (oldMv == 0 && mv16 != 0) {
          const std::uint64_t patched = (oldData & ~0xFFFFull) | mv16;
          (void)ent.data.compare_exchange_strong(oldData, patched, std::memory_order_relaxed,
                                                 std::memory_order_relaxed);
        }
        return;
      }

      // Claim (acquire prevents the following data store from moving before the claim)
      const std::uint64_t lockDesired = (oldInfo & ~INFO_VALID_MASK) | INFO_BUSY_MASK;
      if (!ent.info.compare_exchange_strong(oldInfo, lockDesired, std::memory_order_acquire,
                                            std::memory_order_relaxed)) {
        return;  // avoid stalls; best-effort
      }

      ent.data.store(newData, std::memory_order_relaxed);
      ent.info.store(newInfoFinal, std::memory_order_release);
      return;
    }

    // 2) Free slot: claim empty by CAS to BUSY first, then write data, then publish VALID
    for (auto& ent : c.e) {
      std::uint64_t expected = 0ull;
      if (!ent.info.compare_exchange_strong(expected, newInfoBusy, std::memory_order_acquire,
                                            std::memory_order_relaxed)) {
        continue;
      }
      ent.data.store(newData, std::memory_order_relaxed);
      ent.info.store(newInfoFinal, std::memory_order_release);
      return;
    }

    // 3) Replacement: choose victim; lock victim via BUSY to avoid collateral damage
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
    if ((oldInfo & INFO_BUSY_MASK) || (oldInfo & INFO_VALID_MASK) == 0ull) return;
    if (!strictly_better(oldInfo)) return;

    if (!ent.info.compare_exchange_strong(oldInfo, newInfoBusy, std::memory_order_acquire,
                                          std::memory_order_relaxed)) {
      return;
    }
    ent.data.store(newData, std::memory_order_relaxed);
    ent.info.store(newInfoFinal, std::memory_order_release);
  }

#else
  // Deterministic store: apply the same BUSY protocol (still best-effort under contention)
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

    const int32_t vClamped = value<(int32_t)std::numeric_limits<int16_t>::min()
                                       ? (int32_t)std::numeric_limits<int16_t>::min()
                                       : value>(int32_t) std::numeric_limits<int16_t>::max()
                                 ? (int32_t)std::numeric_limits<int16_t>::max()
                                 : value;

    const int32_t seClamped = staticEval < std::numeric_limits<int16_t>::min()
                                  ? (int32_t)std::numeric_limits<int16_t>::min()
                              : staticEval > std::numeric_limits<int16_t>::max()
                                  ? (int32_t)std::numeric_limits<int16_t>::max()
                                  : (int32_t)staticEval;

    const std::int16_t v16 = static_cast<std::int16_t>(vClamped);
    const std::int16_t se16 = static_cast<std::int16_t>(seClamped);
    const std::uint16_t mv16 = pack_move16(best);

    const std::uint64_t newData =
        (static_cast<std::uint64_t>(mv16) & 0xFFFFull) |
        (static_cast<std::uint64_t>(static_cast<std::uint16_t>(v16)) << 16) |
        (static_cast<std::uint64_t>(static_cast<std::uint16_t>(se16)) << 32) |
        (static_cast<std::uint64_t>(keyHi) << 48);

    const std::uint64_t newInfoFinal =
        INFO_VALID_MASK | (static_cast<std::uint64_t>(keyLo)) |
        (static_cast<std::uint64_t>(curAge) << INFO_AGE_SHIFT) |
        (static_cast<std::uint64_t>(depth8) << INFO_DEPTH_SHIFT) |
        (static_cast<std::uint64_t>(static_cast<std::uint8_t>(bound)) << INFO_BOUND_SHIFT) |
        (static_cast<std::uint64_t>(keyHi) << INFO_KEYHI_SHIFT);

    const std::uint64_t newInfoBusy = (newInfoFinal & ~INFO_VALID_MASK) | INFO_BUSY_MASK;

    auto bound_strength = [](Bound b) constexpr -> int {
      return b == Bound::Exact ? 2 : (b == Bound::Lower ? 1 : 0);
    };

    auto info_quality = [&](std::uint64_t info) -> uint32_t {
      if ((info & INFO_VALID_MASK) == 0ull) return 0;
      if (info & INFO_BUSY_MASK) return 0;

      const std::uint8_t age = static_cast<std::uint8_t>((info >> INFO_AGE_SHIFT) & 0xFFu);
      const std::uint8_t dep = static_cast<std::uint8_t>((info >> INFO_DEPTH_SHIFT) & 0xFFu);
      const Bound bnd = static_cast<Bound>((info >> INFO_BOUND_SHIFT) & 0x3u);
      const std::uint16_t kHi = static_cast<std::uint16_t>((info >> INFO_KEYHI_SHIFT) & 0xFFFFu);
      const int fresh = 255 - static_cast<std::uint8_t>(curAge - age);
      return (static_cast<uint32_t>(dep) << 16) |
             (static_cast<uint32_t>(bound_strength(bnd)) << 12) |
             (static_cast<uint32_t>(fresh) << 4) | (kHi & 0x1u);
    };

    const uint32_t newQ = (static_cast<uint32_t>(depth8) << 16) |
                          (static_cast<uint32_t>(bound_strength(bound)) << 12) |
                          (static_cast<uint32_t>(255) << 4) | (keyHi & 0x1u);

    // 1) Same-key update
    for (auto& ent : c.e) {
      std::uint64_t oldInfo = ent.info.load(std::memory_order_acquire);
      if ((oldInfo & INFO_VALID_MASK) == 0ull) continue;
      if (oldInfo & INFO_BUSY_MASK) continue;
      if ((oldInfo & INFO_KEYLO_MASK) != keyLo) continue;
      const std::uint16_t oldKeyHi =
          static_cast<std::uint16_t>((oldInfo >> INFO_KEYHI_SHIFT) & 0xFFFFu);
      if (oldKeyHi != keyHi) continue;

      if (info_quality(oldInfo) > newQ) return;

      const std::uint64_t lockDesired = (oldInfo & ~INFO_VALID_MASK) | INFO_BUSY_MASK;
      if (!ent.info.compare_exchange_strong(oldInfo, lockDesired, std::memory_order_acquire,
                                            std::memory_order_relaxed)) {
        return;
      }
      ent.data.store(newData, std::memory_order_relaxed);
      ent.info.store(newInfoFinal, std::memory_order_release);
      return;
    }

    // 2) Free slot
    for (auto& ent : c.e) {
      std::uint64_t expected = 0ull;
      if (!ent.info.compare_exchange_strong(expected, newInfoBusy, std::memory_order_acquire,
                                            std::memory_order_relaxed)) {
        continue;
      }
      ent.data.store(newData, std::memory_order_relaxed);
      ent.info.store(newInfoFinal, std::memory_order_release);
      return;
    }

    // 3) Replacement
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
    std::uint64_t oldInfo = ent.info.load(std::memory_order_acquire);
    if (oldInfo & INFO_BUSY_MASK) return;

    if (info_quality(oldInfo) > newQ) return;

    if (!ent.info.compare_exchange_strong(oldInfo, newInfoBusy, std::memory_order_acquire,
                                          std::memory_order_relaxed)) {
      return;
    }
    ent.data.store(newData, std::memory_order_relaxed);
    ent.info.store(newInfoFinal, std::memory_order_release);
  }
#endif

 private:
  // --- bitfield constants ---
  static constexpr std::uint64_t INFO_KEYLO_MASK = 0xFFFFull;
  static constexpr unsigned INFO_AGE_SHIFT = 16;
  static constexpr unsigned INFO_DEPTH_SHIFT = 24;
  static constexpr unsigned INFO_BOUND_SHIFT = 32;
  static constexpr unsigned INFO_KEYHI_SHIFT = 34;
  static constexpr std::uint64_t INFO_BUSY_MASK = (1ull << 62);
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
    const std::uint16_t from = static_cast<std::uint16_t>(static_cast<unsigned>(m.from()) & 0x3F);
    const std::uint16_t to = static_cast<std::uint16_t>(static_cast<unsigned>(m.to()) & 0x3F);
    const std::uint16_t pr3 = static_cast<std::uint16_t>(promo_to3(m.promotion()) & 0x7);
    const std::uint16_t cap = m.isCapture() ? 1u : 0u;
    return static_cast<std::uint16_t>(from | (to << 6) | (pr3 << 12) | (cap << 15));
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

    if (info & INFO_BUSY_MASK) {
      // In-progress: avoid selecting as victim (prevents writer-writer thrash)
      return std::numeric_limits<int>::max();
    }
    if ((info & INFO_VALID_MASK) == 0ull) {
      return std::numeric_limits<int>::min();  // empty → best victim
    }

    const std::uint8_t age = static_cast<std::uint8_t>((info >> INFO_AGE_SHIFT) & 0xFFu);
    const std::uint8_t dep = static_cast<std::uint8_t>((info >> INFO_DEPTH_SHIFT) & 0xFFu);
    const Bound bnd = static_cast<Bound>((info >> INFO_BOUND_SHIFT) & 0x3u);

    const int boundBias = (bnd == Bound::Exact ? 12 : (bnd == Bound::Lower ? 4 : 0));
    const int ageDelta = static_cast<std::uint8_t>(curAge - age);
    return (int)dep * 512 + boundBias - (ageDelta * 2);
  }

  inline std::size_t index(std::uint64_t key) const noexcept {
#if TT5_INDEX_MIX
    // Cheap mix (Zobrist is already random; this is just insurance)
    std::uint64_t h = key;
    h ^= h >> 32;
    h ^= h >> 16;
    return static_cast<std::size_t>(h) & mask_;
#else
    return static_cast<std::size_t>(key) & mask_;
#endif
  }

  static inline std::size_t highest_pow2(std::size_t x) noexcept {
    if (x == 0) return 1;
    // round down to power of two
    const std::size_t p = std::bit_floor(x);
    return p ? p : 1;
  }

  std::unique_ptr<Cluster[]> table_;
  std::size_t slots_ = 1;
  std::size_t mask_ = 0;
  std::atomic<std::uint32_t> generation_{1u};
};

}  // namespace lilia::model
