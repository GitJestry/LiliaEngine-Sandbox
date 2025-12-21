#pragma once
#include <cstdint>
#include <type_traits>

#include "core/model_types.hpp"

namespace lilia::model {

// Explicit 8-bit so we can safely encode it in 2 bits
enum class CastleSide : std::uint8_t { None = 0, KingSide = 1, QueenSide = 2 };

/**
 * Move (32-bit, tightly packed)
 *
 * Layout (LSB -> MSB):
 *  - from:      6 bits  (0..63)
 *  - to:        6 bits  (0..63)
 *  - promotion: 4 bits  (core::PieceType in 0..15)
 *  - capture:   1 bit
 *  - ep:        1 bit
 *  - castle:    2 bits  (CastleSide in 0..3)
 *  - reserved: 12 bits  (free for future flags)
 *
 * IMPORTANT:
 *  - Use a uniform base type (uint32_t) for all bitfields -> MSVC packs correctly.
 *  - operator== compares ONLY from/to/promotion (compatible with TT 16-bit packing).
 */
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

  // Constructors
  constexpr Move() noexcept = default;

  constexpr Move(core::Square f, core::Square t, core::PieceType promo = core::PieceType::None,
                 bool isCap = false, bool isEP = false, CastleSide cs = CastleSide::None) noexcept {
    b.from = static_cast<std::uint32_t>(f);
    b.to = static_cast<std::uint32_t>(t);
    b.promotion = static_cast<std::uint32_t>(promo);
    b.capture = isCap ? 1u : 0u;
    b.ep = isEP ? 1u : 0u;
    b.castle = static_cast<std::uint32_t>(cs);
    b.reserved = 0u;
  }

  // Factory helpers
  static constexpr Move null() noexcept { return Move{}; }

  // Accessors (type-safe)
  constexpr core::Square from() const noexcept { return static_cast<core::Square>(b.from); }
  constexpr core::Square to() const noexcept { return static_cast<core::Square>(b.to); }
  constexpr core::PieceType promotion() const noexcept {
    return static_cast<core::PieceType>(b.promotion);
  }
  constexpr bool isCapture() const noexcept { return b.capture != 0; }
  constexpr bool isEnPassant() const noexcept { return b.ep != 0; }
  constexpr CastleSide castle() const noexcept { return static_cast<CastleSide>(b.castle); }

  // Mutators (if needed)
  constexpr void set_from(core::Square s) noexcept { b.from = static_cast<std::uint32_t>(s); }
  constexpr void set_to(core::Square s) noexcept { b.to = static_cast<std::uint32_t>(s); }
  constexpr void set_promotion(core::PieceType p) noexcept {
    b.promotion = static_cast<std::uint32_t>(p);
  }
  constexpr void set_capture(bool v) noexcept { b.capture = v ? 1u : 0u; }
  constexpr void set_enpassant(bool v) noexcept { b.ep = v ? 1u : 0u; }
  constexpr void set_castle(CastleSide c) noexcept { b.castle = static_cast<std::uint32_t>(c); }
  constexpr void clear_flags() noexcept {
    b.capture = b.ep = 0u;
    b.castle = 0u;
  }

  // Convenience helpers
  constexpr bool isCastle() const noexcept { return b.castle != 0; }
  constexpr bool isQuiet() const noexcept {
    return !isCapture() && !isEnPassant() && !isCastle() && promotion() == core::PieceType::None;
  }
  constexpr bool isNull() const noexcept {
    return b.from == 0u && b.to == 0u &&
           b.promotion == static_cast<std::uint32_t>(core::PieceType::None) && b.capture == 0u &&
           b.ep == 0u && b.castle == 0u && b.reserved == 0u;
  }

  // 16-bit packing (compatible with operator==)
  // [0..5] from, [6..11] to, [12..15] promotion
  constexpr std::uint16_t pack16() const noexcept {
    return static_cast<std::uint16_t>((b.from & 0x3Fu) | ((b.to & 0x3Fu) << 6) |
                                      ((b.promotion & 0x0Fu) << 12));
  }
  static constexpr Move from_packed16(std::uint16_t p) noexcept {
    Move m;
    m.b.from = (p) & 0x3Fu;
    m.b.to = (p >> 6) & 0x3Fu;
    m.b.promotion = (p >> 12) & 0x0Fu;
    m.b.capture = 0u;
    m.b.ep = 0u;
    m.b.castle = 0u;
    m.b.reserved = 0u;
    return m;
  }

  // Equality: from/to/promotion only (flags intentionally ignored)
  friend constexpr bool operator==(const Move& a, const Move& b) noexcept {
    return a.b.from == b.b.from && a.b.to == b.b.to && a.b.promotion == b.b.promotion;
  }
  friend constexpr bool operator!=(const Move& a, const Move& b) noexcept { return !(a == b); }
};

static_assert(std::is_trivially_copyable_v<Move>, "Move must be trivially copyable");
static_assert(sizeof(Move) == 4, "Move should be tightly packed to 4 bytes");

}  // namespace lilia::model
