#pragma once

#include <vector>

#include "move_buffer.hpp"
#include "board.hpp"
#include "game_state.hpp"

namespace lilia::chess
{

  struct Move;

  // Generates pseudo-legal moves for a position, including specialized subsets for search.
  class MoveGenerator
  {
  public:
    // Full pseudo-legal move generation (quiet moves + captures + promotions + en passant +
    // optionally castling)
    void generatePseudoLegalMoves(const Board &b, const GameState &st, std::vector<Move> &out) const;

    // Captures + promotions (including en passant and quiet promotions)
    void generateTacticalMoves(const Board &b, const GameState &st, std::vector<Move> &out) const;

    // Check evasions: safe king moves plus (in single-check) capturing the checker and/or blocking
    // the check Pseudo-legal — final legality is verified via doMove()
    void generateEvasions(const Board &b, const GameState &st, std::vector<Move> &out) const;

    // Non-capture promotions only (i.e., quiet promotions)
    void generateNonCapturePromotions(const Board &b, const GameState &st,
                                      std::vector<Move> &out) const;

    // Return: num generated moves
    int generatePseudoLegalMoves(const Board &, const GameState &, MoveBuffer &buf);
    int generateTacticalMoves(const Board &, const GameState &, MoveBuffer &buf);
    int generateEvasions(const Board &, const GameState &, MoveBuffer &buf);
    int generateNonCapturePromotions(const Board &b, const GameState &st, MoveBuffer &buf);
  };

}
