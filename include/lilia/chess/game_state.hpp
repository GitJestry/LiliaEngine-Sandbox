#pragma once
#include <cstdint>
#include <type_traits>

#include "core/bitboard.hpp"
#include "move.hpp"

namespace lilia::chess
{

  struct alignas(8) GameState
  {
    bb::Bitboard pawnKey = 0; // incremental pawn hash for THIS position
    std::uint32_t fullmoveNumber = 1;
    std::uint16_t halfmoveClock = 0;
    std::uint8_t castlingRights =
        CastlingRights::WhiteKingSide | CastlingRights::WhiteQueenSide |
        CastlingRights::BlackKingSide | CastlingRights::BlackQueenSide;
    Color sideToMove = Color::White;
    Square enPassantSquare = NO_SQUARE;
  };

  enum StateFlag : std::uint8_t
  {
    StateNone = 0,
    StateCapture = 1u << 0,
    StateEnPassant = 1u << 1,
    StateCastle = 1u << 2,
    StatePromotion = 1u << 3,
    StateNullMove = 1u << 4
  };

  // Linked state node for the CURRENT position.
  struct alignas(16) StateInfo : GameState
  {
    bb::Bitboard zobristKey{0};
    StateInfo *previous{nullptr};

    Move move{};
    Piece moved{PieceType::None, Color::White};
    Piece captured{PieceType::None, Color::White};

    Square capturedSquare{NO_SQUARE}; // EP capture square or normal to-square
    Square rookFrom{NO_SQUARE};       // castling undo / eval delta
    Square rookTo{NO_SQUARE};

    std::uint16_t pliesFromNull{0};
    std::uint8_t gaveCheck{0};
    std::uint8_t flags{StateNone};

    [[nodiscard]] bool isNullMove() const noexcept { return (flags & StateNullMove) != 0; }
    [[nodiscard]] bool isCapture() const noexcept { return (flags & StateCapture) != 0; }
    [[nodiscard]] bool isEnPassant() const noexcept { return (flags & StateEnPassant) != 0; }
    [[nodiscard]] bool isCastle() const noexcept { return (flags & StateCastle) != 0; }
    [[nodiscard]] bool isPromotion() const noexcept { return (flags & StatePromotion) != 0; }
  };

  static_assert(
      (CastlingRights::WhiteKingSide | CastlingRights::WhiteQueenSide |
       CastlingRights::BlackKingSide | CastlingRights::BlackQueenSide) <= 0xF,
      "Castling rights must fit in 4 bits");

  static_assert(std::is_trivially_copyable_v<GameState>, "GameState should be POD");
  static_assert(std::is_trivially_copyable_v<StateInfo>, "StateInfo should be POD");

  static_assert(sizeof(Color) <= 1, "Color should be 1 byte for compact state");
  static_assert(sizeof(Square) <= 1, "Square should be 1 byte for compact state");

}
