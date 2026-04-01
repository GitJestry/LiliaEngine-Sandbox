#pragma once
#include <cstdint>
#include <type_traits>

#include "core/bitboard.hpp"
#include "move.hpp"

namespace lilia::chess
{

  // Mutable game-state data that belongs to the current position.
  struct alignas(8) GameState
  {
    bb::Bitboard pawnKey = 0; // incremental pawn hash
    std::uint32_t fullmoveNumber = 1;
    std::uint16_t halfmoveClock = 0;
    std::uint8_t castlingRights =
        CastlingRights::WhiteKingSide | CastlingRights::WhiteQueenSide |
        CastlingRights::BlackKingSide | CastlingRights::BlackQueenSide;
    Color sideToMove = Color::White;
    Square enPassantSquare = NO_SQUARE;
  };

  // Undo record for a normal move. Stores the state needed to restore the position.
  struct alignas(8) StateInfo
  {
    bb::Bitboard zobristKey{};
    bb::Bitboard prevPawnKey{};

    Move move{};
    Piece captured{};

    std::uint16_t prevHalfmoveClock{};
    std::uint8_t prevCastlingRights{};
    std::uint8_t gaveCheck{};
    Square prevEnPassantSquare{NO_SQUARE};
  };

  // Undo record for a null move. Only stores the state changed by null-move search.
  struct alignas(8) NullState
  {
    bb::Bitboard zobristKey{0};
    std::uint32_t prevFullmoveNumber{1};
    std::uint16_t prevHalfmoveClock{0};
    std::uint8_t prevCastlingRights{0};
    Square prevEnPassantSquare{NO_SQUARE};
  };

  static_assert(
      (CastlingRights::WhiteKingSide | CastlingRights::WhiteQueenSide |
       CastlingRights::BlackKingSide | CastlingRights::BlackQueenSide) <= 0xF,
      "Castling rights must fit in 4 bits");

  static_assert(std::is_trivially_copyable_v<GameState>, "GameState should be POD");
  static_assert(std::is_trivially_copyable_v<StateInfo>, "StateInfo should be POD");
  static_assert(std::is_trivially_copyable_v<NullState>, "NullState should be POD");

  static_assert(sizeof(Color) <= 1, "Color should be 1 byte for compact state");
  static_assert(sizeof(Square) <= 1, "Square should be 1 byte for compact state");

}
