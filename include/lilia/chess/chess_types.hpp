#pragma once
#include <cstdint>

namespace lilia::chess
{
  using Square = std::uint8_t;
  inline constexpr Square NO_SQUARE = 64;

  inline constexpr int PIECE_NB = 6;
  inline constexpr int SQ_NB = 64;

  inline bool validSquare(Square sq)
  {
    return sq != NO_SQUARE;
  }

  inline constexpr std::uint8_t NUM_PIECE_TYPES = 6;
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

  enum class CastleSide : std::uint8_t
  {
    None = 0,
    KingSide = 1,
    QueenSide = 2
  };

  enum Castling : std::uint8_t
  {
    WK = 1 << 0,
    WQ = 1 << 1,
    BK = 1 << 2,
    BQ = 1 << 3
  };

  enum class Color : std::uint8_t
  {
    White = 0,
    Black = 1
  };

  struct Piece
  {
    PieceType type = PieceType::None;
    Color color = Color::White;
    [[nodiscard]] constexpr bool isNone() const noexcept { return type == PieceType::None; }
  };

  constexpr int idx(PieceType p) noexcept
  {
    return static_cast<int>(p);
  }

  constexpr inline Color operator~(Color c)
  {
    return c == Color::White ? Color::Black : Color::White;
  }

  enum GameResult
  {
    ONGOING,
    CHECKMATE,
    TIMEOUT,
    REPETITION,
    MOVERULE,
    STALEMATE,
    INSUFFICIENT
  };
} // namespace lilia
