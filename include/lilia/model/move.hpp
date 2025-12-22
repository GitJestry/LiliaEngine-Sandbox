#pragma once
#include <cstdint>
#include <type_traits>

#include "core/model_types.hpp"

namespace lilia::model {

enum class CastleSide : std::uint8_t { None = 0, KingSide = 1, QueenSide = 2 };

struct Move {
  union {
    std::uint32_t raw{0};
    struct {
      std::uint32_t from : 6;
      std::uint32_t to : 6;
      std::uint32_t promotion : 4;
      std::uint32_t capture : 1;
      std::uint32_t ep : 1;
      std::uint32_t castle : 2;
      std::uint32_t reserved : 12;
    } b;
  };

  // Bit layout constants (match the documented layout)
  static constexpr std::uint32_t FROM_SHIFT = 0;
  static constexpr std::uint32_t TO_SHIFT = 6;
  static constexpr std::uint32_t PROMO_SHIFT = 12;
  static constexpr std::uint32_t CAP_SHIFT = 16;
  static constexpr std::uint32_t EP_SHIFT = 17;
  static constexpr std::uint32_t CASTLE_SHIFT = 18;

  static constexpr std::uint32_t FROM_MASK = 0x3Fu << FROM_SHIFT;
  static constexpr std::uint32_t TO_MASK = 0x3Fu << TO_SHIFT;
  static constexpr std::uint32_t PROMO_MASK = 0x0Fu << PROMO_SHIFT;
  static constexpr std::uint32_t CAP_MASK = 0x01u << CAP_SHIFT;
  static constexpr std::uint32_t EP_MASK = 0x01u << EP_SHIFT;
  static constexpr std::uint32_t CASTLE_MASK = 0x03u << CASTLE_SHIFT;

  static constexpr std::uint32_t PACK16_MASK = 0xFFFFu;

  // Constructors
  constexpr Move() noexcept = default;

  constexpr Move(core::Square f, core::Square t, core::PieceType promo = core::PieceType::None,
                 bool isCap = false, bool isEP = false, CastleSide cs = CastleSide::None) noexcept {
    const std::uint32_t ff = (static_cast<std::uint32_t>(f) & 0x3Fu);
    const std::uint32_t tt = (static_cast<std::uint32_t>(t) & 0x3Fu);
    const std::uint32_t pp = (static_cast<std::uint32_t>(promo) & 0x0Fu);
    const std::uint32_t cc = isCap ? 1u : 0u;
    const std::uint32_t ee = isEP ? 1u : 0u;
    const std::uint32_t cs2 = (static_cast<std::uint32_t>(cs) & 0x03u);

    raw = (ff << FROM_SHIFT) | (tt << TO_SHIFT) | (pp << PROMO_SHIFT) | (cc << CAP_SHIFT) |
          (ee << EP_SHIFT) | (cs2 << CASTLE_SHIFT);
  }

  static constexpr Move null() noexcept { return Move{}; }

  // Accessors (raw-based)
  [[nodiscard]] constexpr core::Square from() const noexcept {
    return static_cast<core::Square>((raw >> FROM_SHIFT) & 0x3Fu);
  }
  [[nodiscard]] constexpr core::Square to() const noexcept {
    return static_cast<core::Square>((raw >> TO_SHIFT) & 0x3Fu);
  }
  [[nodiscard]] constexpr core::PieceType promotion() const noexcept {
    return static_cast<core::PieceType>((raw >> PROMO_SHIFT) & 0x0Fu);
  }
  [[nodiscard]] constexpr bool isCapture() const noexcept { return (raw & CAP_MASK) != 0; }
  [[nodiscard]] constexpr bool isEnPassant() const noexcept { return (raw & EP_MASK) != 0; }
  [[nodiscard]] constexpr CastleSide castle() const noexcept {
    return static_cast<CastleSide>((raw >> CASTLE_SHIFT) & 0x03u);
  }

  // Mutators (raw-based; keeps union/bitfield view valid)
  constexpr void set_from(core::Square s) noexcept {
    raw = (raw & ~FROM_MASK) | ((static_cast<std::uint32_t>(s) & 0x3Fu) << FROM_SHIFT);
  }
  constexpr void set_to(core::Square s) noexcept {
    raw = (raw & ~TO_MASK) | ((static_cast<std::uint32_t>(s) & 0x3Fu) << TO_SHIFT);
  }
  constexpr void set_promotion(core::PieceType p) noexcept {
    raw = (raw & ~PROMO_MASK) | ((static_cast<std::uint32_t>(p) & 0x0Fu) << PROMO_SHIFT);
  }
  constexpr void set_capture(bool v) noexcept { raw = v ? (raw | CAP_MASK) : (raw & ~CAP_MASK); }
  constexpr void set_enpassant(bool v) noexcept { raw = v ? (raw | EP_MASK) : (raw & ~EP_MASK); }
  constexpr void set_castle(CastleSide c) noexcept {
    raw = (raw & ~CASTLE_MASK) | ((static_cast<std::uint32_t>(c) & 0x03u) << CASTLE_SHIFT);
  }
  constexpr void clear_flags() noexcept { raw &= ~(CAP_MASK | EP_MASK | CASTLE_MASK); }

  // Convenience helpers
  [[nodiscard]] constexpr bool isCastle() const noexcept { return (raw & CASTLE_MASK) != 0; }
  [[nodiscard]] constexpr bool isQuiet() const noexcept {
    return (raw & (CAP_MASK | EP_MASK | CASTLE_MASK | PROMO_MASK)) == 0;
  }
  [[nodiscard]] constexpr bool isNull() const noexcept { return raw == 0; }

  // 16-bit packing: low 16 bits are exactly from/to/promo
  [[nodiscard]] constexpr std::uint16_t pack16() const noexcept {
    return static_cast<std::uint16_t>(raw & PACK16_MASK);
  }

  static constexpr Move from_packed16(std::uint16_t p) noexcept {
    Move m;
    m.raw = static_cast<std::uint32_t>(p);  // flags/reserved = 0
    return m;
  }

  // Equality: from/to/promotion only
  friend constexpr bool operator==(const Move& a, const Move& b) noexcept {
    return (a.raw & PACK16_MASK) == (b.raw & PACK16_MASK);
  }
  friend constexpr bool operator!=(const Move& a, const Move& b) noexcept { return !(a == b); }
};

static_assert(std::is_trivially_copyable_v<Move>, "Move must be trivially copyable");
static_assert(sizeof(Move) == 4, "Move should be tightly packed to 4 bytes");

}  // namespace lilia::model
