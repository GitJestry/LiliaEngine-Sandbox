#pragma once
#include <cstdint>
#include <type_traits>

#include "core/bitboard.hpp"
#include "move.hpp"

namespace lilia::chess
{

  struct alignas(8) GameState
  {
    bb::Bitboard pawnKey = 0;         // incremental pawn hash
    std::uint32_t fullmoveNumber = 1; // 1..2^32-1
    std::uint16_t halfmoveClock = 0;  // 0..100 is plenty
    std::uint8_t castlingRights =
        CastlingRights::WhiteKingSide | CastlingRights::WhiteQueenSide | CastlingRights::BlackKingSide | CastlingRights::BlackQueenSide;
    Color sideToMove = Color::White;
    Square enPassantSquare = NO_SQUARE;
  };

  struct alignas(8) StateInfo
  {
    // Put 8-byte fields first to reduce padding; improves stack/array locality in search.
    bb::Bitboard zobristKey{};  // full hash before move
    bb::Bitboard prevPawnKey{}; // pawn hash before move

    Move move{};      // last move
    Piece captured{}; // captured piece (type+color)

    std::uint16_t prevHalfmoveClock{}; // halfmove clock before move
    std::uint8_t prevCastlingRights{}; // castling rights before move
    std::uint8_t gaveCheck{0};         // 0/1
    Square prevEnPassantSquare{NO_SQUARE};
  };

  struct alignas(8) NullState
  {
    bb::Bitboard zobristKey{0}; // full hash before null move
    std::uint32_t prevFullmoveNumber{1};
    std::uint16_t prevHalfmoveClock{0};
    std::uint8_t prevCastlingRights{0};
    Square prevEnPassantSquare{NO_SQUARE};
  };

  // Sanity checks (cheap, catch accidental changes early)
  static_assert((CastlingRights::WhiteKingSide | CastlingRights::WhiteQueenSide | CastlingRights::BlackKingSide | CastlingRights::BlackQueenSide) <= 0xF,
                "Castling rights must fit in 4 bits");

  static_assert(std::is_trivially_copyable_v<GameState>, "GameState should be POD");
  static_assert(std::is_trivially_copyable_v<StateInfo>, "StateInfo should be POD");
  static_assert(std::is_trivially_copyable_v<NullState>, "NullState should be POD");

  // Critical for performance: avoid silent enum widening.
  static_assert(sizeof(Color) <= 1, "Color should be 1 byte for compact state");
  static_assert(sizeof(Square) <= 1, "Square should be 1 byte for compact state");

}
