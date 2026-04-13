#pragma once

#include <array>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>

#include "lilia/chess/move.hpp"
#include "lilia/chess/compiler.hpp"

namespace lilia::engine
{

  enum class Bound : std::uint8_t
  {
    Exact = 0,
    Lower = 1,
    Upper = 2,
    None = 3
  };

  struct TTEntry
  {
    std::uint64_t key = 0;
    int32_t value = 0; // decoded by caller with mate-distance helpers
    int16_t depth = 0; // plies
    Bound bound = Bound::None;
    chess::Move best{};
    std::uint8_t age = 0;
    int16_t staticEval = std::numeric_limits<int16_t>::min(); // INT16_MIN == unset

    [[nodiscard]] LILIA_ALWAYS_INLINE bool hasValue() const noexcept
    {
      return bound != Bound::None;
    }
  };

  class TT
  {
  public:
    explicit TT(std::size_t mb = 16) { resize(mb); }

    void resize(std::size_t mb)
    {
      const std::size_t bytes = std::max<std::size_t>(mb, 1) * 1024ull * 1024ull;
      clusterCount_ = std::max<std::size_t>(1, bytes / sizeof(Cluster));
      table_.reset(new Cluster[clusterCount_]);
      clear();
    }

    void clear() noexcept
    {
      if (!table_)
        return;

      std::memset(table_.get(), 0, clusterCount_ * sizeof(Cluster));
      generation_ = 0u;
    }

    LILIA_ALWAYS_INLINE void new_generation() noexcept
    {
      ++generation_;
    }

    LILIA_ALWAYS_INLINE void prefetch(std::uint64_t key) const noexcept
    {
      if (table_)
        LILIA_PREFETCH_L1(&table_[index(key)]);
    }

    LILIA_ALWAYS_INLINE bool probe_into(std::uint64_t key, TTEntry &out) const noexcept
    {
      if (!table_)
        return false;

      const Cluster &c = table_[index(key)];
      LILIA_PREFETCH_L1(&c);

      const std::uint32_t sig = signature(key);

      for (const auto &s : c.slot)
      {
        const std::uint64_t m1 = s.meta;

        if ((m1 & META_VALID_MASK) == 0ull)
          continue;

        if (meta_sig(m1) != sig)
          continue;

        const std::uint64_t d = s.data;
        const std::uint64_t m2 = s.meta;

        // Cheap race filter. If another thread touched this slot while we read it,
        // ignore and keep scanning.
        if (LILIA_UNLIKELY(m1 != m2))
          continue;

        out.key = key;
        out.age = meta_age(m2);
        out.depth = static_cast<int16_t>(meta_depth(m2));
        out.bound = meta_bound(m2);
        out.best = unpack_move32(data_move(d));
        out.value = static_cast<int16_t>((d >> DATA_VALUE_SHIFT) & 0xFFFFu);
        out.staticEval = static_cast<int16_t>((d >> DATA_EVAL_SHIFT) & 0xFFFFu);
        return true;
      }

      return false;
    }

    [[nodiscard]] LILIA_ALWAYS_INLINE std::optional<TTEntry> probe(std::uint64_t key) const
    {
      TTEntry e{};
      if (probe_into(key, e))
        return e;
      return std::nullopt;
    }

    void store(std::uint64_t key,
               int32_t value,
               int16_t depth,
               Bound bound,
               const chess::Move &best,
               int16_t staticEval = std::numeric_limits<int16_t>::min()) noexcept
    {
      if (!table_)
        return;

      Cluster &c = table_[index(key)];
      LILIA_PREFETCHW_L1(&c);

      const std::uint8_t curAge = current_generation();
      const std::uint8_t newDepth =
          static_cast<std::uint8_t>(depth < 0 ? 0 : (depth > 255 ? 255 : depth));

      const std::uint32_t sig = signature(key);
      const std::int16_t newValue16 = clamp_i16(value);
      const std::int16_t newEval16 = static_cast<std::int16_t>(staticEval);
      const bool hasNewMove = !is_null_move(best);
      const std::uint32_t newMoveBits = hasNewMove ? pack_move32(best) : 0u;

      auto stale = [&](std::uint8_t oldAge) -> bool
      {
        return static_cast<std::uint8_t>(curAge - oldAge) != 0;
      };

      auto should_replace_value_part =
          [&](std::uint8_t oldDepth, Bound oldBound, std::uint8_t oldAge) -> bool
      {
        if (bound == Bound::None)
          return false;

        if (oldBound == Bound::None)
          return true;

        if (bound == Bound::Exact && oldBound != Bound::Exact)
          return true;

        if (stale(oldAge))
          return true;

        return newDepth + 4 >= oldDepth;
      };

      auto should_refresh_move =
          [&](std::uint32_t oldMoveBits, std::uint8_t oldDepth, Bound oldBound, std::uint8_t oldAge) -> bool
      {
        if (!hasNewMove)
          return false;

        if (oldMoveBits == 0u)
          return true;

        if (stale(oldAge))
          return true;

        if (bound == Bound::Exact)
          return true;

        if (oldBound == Bound::None)
          return true;

        return newDepth >= oldDepth;
      };

      auto should_refresh_eval =
          [&](std::int16_t oldEval16, std::uint8_t oldAge, std::uint8_t oldDepth) -> bool
      {
        if (staticEval == SE_UNSET)
          return false;

        if (oldEval16 == SE_UNSET)
          return true;

        if (stale(oldAge))
          return true;

        return newDepth >= oldDepth;
      };

      // 1) Same-key update.
      for (auto &s : c.slot)
      {
        const std::uint64_t oldMeta = s.meta;

        if ((oldMeta & META_VALID_MASK) == 0ull)
          continue;

        if (meta_sig(oldMeta) != sig)
          continue;

        const std::uint64_t oldData = s.data;

        const Bound oldBound = meta_bound(oldMeta);
        const std::uint8_t oldDepth = meta_depth(oldMeta);
        const std::uint8_t oldAge = meta_age(oldMeta);
        const std::uint32_t oldMoveBits = data_move(oldData);
        const std::int16_t oldValue16 = static_cast<std::int16_t>((oldData >> DATA_VALUE_SHIFT) & 0xFFFFu);
        const std::int16_t oldEval16 = static_cast<std::int16_t>((oldData >> DATA_EVAL_SHIFT) & 0xFFFFu);

        std::uint32_t outMoveBits = oldMoveBits;
        std::int16_t outValue16 = oldValue16;
        std::int16_t outEval16 = oldEval16;
        std::uint8_t outDepth = oldDepth;
        Bound outBound = oldBound;

        bool changed = false;

        if (should_refresh_move(oldMoveBits, oldDepth, oldBound, oldAge))
        {
          outMoveBits = newMoveBits;
          changed = true;
        }

        if (should_refresh_eval(oldEval16, oldAge, oldDepth))
        {
          outEval16 = newEval16;
          changed = true;
        }

        if (should_replace_value_part(oldDepth, oldBound, oldAge))
        {
          outValue16 = newValue16;
          outDepth = newDepth;
          outBound = bound;
          changed = true;
        }

        if (!changed && oldAge == curAge)
          return;

        // Same key: update payload first, meta last.
        s.data = pack_data(outMoveBits, outValue16, outEval16);
        s.meta = pack_meta(sig, curAge, outDepth, outBound, true);
        return;
      }

      // 2) Empty slot.
      for (auto &s : c.slot)
      {
        if ((s.meta & META_VALID_MASK) != 0ull)
          continue;

        s.data = pack_data(newMoveBits, newValue16, newEval16);
        s.meta = pack_meta(sig, curAge, newDepth, bound, true);
        return;
      }

      // 3) Eval-only stores must not evict searched entries.
      if (bound == Bound::None)
        return;

      // 4) Replace the easiest victim.
      int victim = -1;
      int victimMetric = std::numeric_limits<int>::min();

      for (int i = 0; i < ClusterSize; ++i)
      {
        const std::uint64_t m = c.slot[i].meta;
        const int metric = victim_replace_metric(m, curAge);
        if (metric > victimMetric)
        {
          victimMetric = metric;
          victim = i;
        }
      }

      if (victim < 0)
        return;

      Slot &s = c.slot[victim];

      // Cross-key overwrite:
      // invalidate first so a probe does not see old signature with new payload.
      s.meta = 0ull;
      s.data = pack_data(newMoveBits, newValue16, newEval16);
      s.meta = pack_meta(sig, curAge, newDepth, bound, true);
    }

  private:
    static constexpr int ClusterSize = 4;
    static constexpr int16_t SE_UNSET = std::numeric_limits<int16_t>::min();

    // meta:
    // [ 0..31] signature32
    // [32..39] age8
    // [40..47] depth8
    // [48..49] bound2
    // [63    ] VALID
    static constexpr unsigned META_AGE_SHIFT = 32;
    static constexpr unsigned META_DEPTH_SHIFT = 40;
    static constexpr unsigned META_BOUND_SHIFT = 48;

    static constexpr std::uint64_t META_SIG_MASK = 0xFFFFFFFFull;
    static constexpr std::uint64_t META_VALID_MASK = (1ull << 63);

    // data:
    // [ 0..31] move32
    // [32..47] value16
    // [48..63] eval16
    static constexpr unsigned DATA_VALUE_SHIFT = 32;
    static constexpr unsigned DATA_EVAL_SHIFT = 48;

    struct Slot
    {
      std::uint64_t meta = 0;
      std::uint64_t data = 0;
    };

    struct alignas(64) Cluster
    {
      std::array<Slot, ClusterSize> slot{};
    };

    static LILIA_ALWAYS_INLINE bool is_null_move(const chess::Move &m) noexcept
    {
      return m.from() == m.to();
    }

    static LILIA_ALWAYS_INLINE std::int16_t clamp_i16(int32_t v) noexcept
    {
      if (v > std::numeric_limits<int16_t>::max())
        return std::numeric_limits<int16_t>::max();
      if (v < std::numeric_limits<int16_t>::min())
        return std::numeric_limits<int16_t>::min();
      return static_cast<std::int16_t>(v);
    }

    static LILIA_ALWAYS_INLINE std::uint32_t signature(std::uint64_t key) noexcept
    {
      return static_cast<std::uint32_t>(key) ^ static_cast<std::uint32_t>(key >> 32);
    }

    static LILIA_ALWAYS_INLINE std::uint64_t pack_meta(std::uint32_t sig,
                                                       std::uint8_t age,
                                                       std::uint8_t depth,
                                                       Bound bound,
                                                       bool valid) noexcept
    {
      std::uint64_t m = static_cast<std::uint64_t>(sig);
      m |= (static_cast<std::uint64_t>(age) << META_AGE_SHIFT);
      m |= (static_cast<std::uint64_t>(depth) << META_DEPTH_SHIFT);
      m |= (static_cast<std::uint64_t>(static_cast<std::uint8_t>(bound) & 0x3u) << META_BOUND_SHIFT);
      if (valid)
        m |= META_VALID_MASK;
      return m;
    }

    static LILIA_ALWAYS_INLINE std::uint32_t meta_sig(std::uint64_t m) noexcept
    {
      return static_cast<std::uint32_t>(m & META_SIG_MASK);
    }

    static LILIA_ALWAYS_INLINE std::uint8_t meta_age(std::uint64_t m) noexcept
    {
      return static_cast<std::uint8_t>((m >> META_AGE_SHIFT) & 0xFFu);
    }

    static LILIA_ALWAYS_INLINE std::uint8_t meta_depth(std::uint64_t m) noexcept
    {
      return static_cast<std::uint8_t>((m >> META_DEPTH_SHIFT) & 0xFFu);
    }

    static LILIA_ALWAYS_INLINE Bound meta_bound(std::uint64_t m) noexcept
    {
      return static_cast<Bound>((m >> META_BOUND_SHIFT) & 0x3u);
    }

    static LILIA_ALWAYS_INLINE std::uint16_t promo_to3(chess::PieceType p) noexcept
    {
      switch (p)
      {
      case chess::PieceType::Knight:
        return 1;
      case chess::PieceType::Bishop:
        return 2;
      case chess::PieceType::Rook:
        return 3;
      case chess::PieceType::Queen:
        return 4;
      default:
        return 0;
      }
    }

    static LILIA_ALWAYS_INLINE chess::PieceType promo_from3(std::uint32_t v) noexcept
    {
      switch (v & 0x7u)
      {
      case 1:
        return chess::PieceType::Knight;
      case 2:
        return chess::PieceType::Bishop;
      case 3:
        return chess::PieceType::Rook;
      case 4:
        return chess::PieceType::Queen;
      default:
        return chess::PieceType::None;
      }
    }

    static LILIA_ALWAYS_INLINE std::uint32_t pack_move32(const chess::Move &m) noexcept
    {
      std::uint32_t v = 0u;
      v |= (static_cast<std::uint32_t>(static_cast<unsigned>(m.from())) & 0x3Fu);
      v |= ((static_cast<std::uint32_t>(static_cast<unsigned>(m.to())) & 0x3Fu) << 6);
      v |= (static_cast<std::uint32_t>(promo_to3(m.promotion())) << 12);

      if (m.isCapture())
        v |= (1u << 15);
      if (m.isEnPassant())
        v |= (1u << 16);

      switch (m.castle())
      {
      case chess::CastleSide::KingSide:
        v |= (1u << 17);
        break;
      case chess::CastleSide::QueenSide:
        v |= (2u << 17);
        break;
      default:
        break;
      }

      return v;
    }

    static LILIA_ALWAYS_INLINE chess::Move unpack_move32(std::uint32_t v) noexcept
    {
      chess::Move m{};
      m.set_from(static_cast<chess::Square>(v & 0x3Fu));
      m.set_to(static_cast<chess::Square>((v >> 6) & 0x3Fu));
      m.set_promotion(promo_from3((v >> 12) & 0x7u));
      m.set_capture(((v >> 15) & 1u) != 0);
      m.set_enpassant(((v >> 16) & 1u) != 0);

      const std::uint32_t castle = (v >> 17) & 0x3u;
      if (castle == 1u)
        m.set_castle(chess::CastleSide::KingSide);
      else if (castle == 2u)
        m.set_castle(chess::CastleSide::QueenSide);
      else
        m.set_castle(chess::CastleSide::None);

      return m;
    }

    static LILIA_ALWAYS_INLINE std::uint64_t pack_data(std::uint32_t moveBits,
                                                       std::int16_t value16,
                                                       std::int16_t eval16) noexcept
    {
      return static_cast<std::uint64_t>(moveBits) |
             (static_cast<std::uint64_t>(static_cast<std::uint16_t>(value16)) << DATA_VALUE_SHIFT) |
             (static_cast<std::uint64_t>(static_cast<std::uint16_t>(eval16)) << DATA_EVAL_SHIFT);
    }

    static LILIA_ALWAYS_INLINE std::uint32_t data_move(std::uint64_t d) noexcept
    {
      return static_cast<std::uint32_t>(d & 0xFFFFFFFFull);
    }

    // Higher metric => easier to replace.
    static LILIA_ALWAYS_INLINE int victim_replace_metric(std::uint64_t meta, std::uint8_t curAge) noexcept
    {
      if ((meta & META_VALID_MASK) == 0ull)
        return std::numeric_limits<int>::max();

      const int depth = static_cast<int>(meta_depth(meta));
      const int age = static_cast<int>(static_cast<std::uint8_t>(curAge - meta_age(meta)));

      int bonus = 0;
      switch (meta_bound(meta))
      {
      case Bound::Exact:
        bonus = 3;
        break;
      case Bound::Lower:
        bonus = 1;
        break;
      case Bound::Upper:
        bonus = 0;
        break;
      case Bound::None:
        bonus = -1;
        break;
      }

      return 8 * age - depth - bonus;
    }

    LILIA_ALWAYS_INLINE std::uint8_t current_generation() const noexcept
    {
      return generation_ ? generation_ : 1u;
    }

    LILIA_ALWAYS_INLINE std::size_t index(std::uint64_t key) const noexcept
    {
#if defined(__SIZEOF_INT128__)
      return static_cast<std::size_t>((static_cast<__uint128_t>(key) * clusterCount_) >> 64);
#else
      return static_cast<std::size_t>(key % clusterCount_);
#endif
    }

    std::unique_ptr<Cluster[]> table_;
    std::size_t clusterCount_ = 1;
    std::uint8_t generation_ = 0u;
  };

} // namespace lilia::engine
