#pragma once
#include <cstdint>

namespace lilia::chess
{
  using Square = std::uint8_t;
  inline constexpr Square NO_SQUARE = 64;
  inline constexpr int SQ_NB = 64;
  inline constexpr std::uint8_t PIECE_TYPE_NB = 6;

  [[nodiscard]] constexpr bool validSquare(Square sq) noexcept
  {
    return sq < SQ_NB;
  }

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

  enum class Color : std::uint8_t
  {
    White = 0,
    Black = 1
  };

  [[nodiscard]] constexpr Color operator~(Color c) noexcept
  {
    return c == Color::White ? Color::Black : Color::White;
  }

  enum class CastleSide : std::uint8_t
  {
    None = 0,
    KingSide = 1,
    QueenSide = 2
  };

  enum CastlingRights : std::uint8_t
  {
    NoCastling = 0,
    WhiteKingSide = 1 << 0,
    WhiteQueenSide = 1 << 1,
    BlackKingSide = 1 << 2,
    BlackQueenSide = 1 << 3
  };

  struct Piece
  {
    PieceType type{PieceType::None};
    Color color{Color::White};

    [[nodiscard]] constexpr bool isNone() const noexcept
    {
      return type == PieceType::None;
    }
  };

  [[nodiscard]] constexpr int idx(PieceType p) noexcept
  {
    return static_cast<int>(p);
  }

  enum class GameResult : std::uint8_t
  {
    Ongoing,
    Checkmate,
    Timeout,
    Repetition,
    MoveRule,
    Stalemate,
    InsufficientMaterial
  };
} // namespace lilia::chess
