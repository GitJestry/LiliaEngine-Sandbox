#pragma once
#include <cstdint>
namespace lilia::core
{
  using Square = std::uint8_t;
  constexpr Square NO_SQUARE = 64;

  inline bool validSquare(core::Square sq)
  {
    return sq != core::NO_SQUARE;
  }

  constexpr std::uint8_t NUM_PIECE_TYPES = 6;
  enum class PieceType : std::uint8_t
  {
    Pawn = 0,
    Knight,
    Bishop,
    Rook,
    Queen,
    King,
    None
  };

  constexpr int idx(PieceType p) noexcept
  {
    return static_cast<int>(p);
  }

  enum class Color : std::uint8_t
  {
    White = 0,
    Black = 1
  };
  constexpr inline core::Color operator~(core::Color c)
  {
    return c == core::Color::White ? core::Color::Black : core::Color::White;
  }
} // namespace lilia::core
