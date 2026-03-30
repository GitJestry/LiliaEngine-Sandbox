#pragma once

#include <string>

#include "lilia/chess/chess_types.hpp"

namespace lilia::app::domain
{

  // Converts a GameResult + the side who is to move in the terminal position
  // into a PGN-style score string.
  // - In CHECKMATE/TIMEOUT, "sideToMoveInTerminal" is treated as the losing side.
  // - For draw-ish results, returns "1/2-1/2".
  // - For ongoing, returns "*" for PGN or "" for UI.
  inline std::string result_string(chess::GameResult res,
                                   chess::Color sideToMoveInTerminal,
                                   bool forPgn)
  {
    switch (res)
    {
    case chess::GameResult::CHECKMATE:
    case chess::GameResult::TIMEOUT:
      // sideToMove in terminal position is the loser
      return (sideToMoveInTerminal == chess::Color::White) ? "0-1" : "1-0";

    case chess::GameResult::REPETITION:
    case chess::GameResult::MOVERULE:
    case chess::GameResult::STALEMATE:
    case chess::GameResult::INSUFFICIENT:
      return "1/2-1/2";

    case chess::GameResult::ONGOING:
    default:
      return forPgn ? "*" : "";
    }
  }

} // namespace lilia::model::analysis
