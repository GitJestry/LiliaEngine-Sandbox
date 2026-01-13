#pragma once

#include <string>

#include "lilia/chess_types.hpp"
#include "lilia/constants.hpp"

namespace lilia::model::analysis
{

  // Converts a GameResult + the side who is to move in the terminal position
  // into a PGN-style score string.
  // - In CHECKMATE/TIMEOUT, "sideToMoveInTerminal" is treated as the losing side.
  // - For draw-ish results, returns "1/2-1/2".
  // - For ongoing, returns "*" for PGN or "" for UI.
  inline std::string result_string(core::GameResult res,
                                   core::Color sideToMoveInTerminal,
                                   bool forPgn)
  {
    using core::GameResult;

    switch (res)
    {
    case GameResult::CHECKMATE:
    case GameResult::TIMEOUT:
      // sideToMove in terminal position is the loser
      return (sideToMoveInTerminal == core::Color::White) ? "0-1" : "1-0";

    case GameResult::REPETITION:
    case GameResult::MOVERULE:
    case GameResult::STALEMATE:
    case GameResult::INSUFFICIENT:
      return "1/2-1/2";

    case GameResult::ONGOING:
    default:
      return forPgn ? "*" : "";
    }
  }

} // namespace lilia::model::analysis
