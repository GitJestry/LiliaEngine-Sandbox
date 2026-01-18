#pragma once

#include <string>

namespace lilia::core
{
  const std::string START_FEN = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
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
  // ------------------ Version ------------------
  inline constexpr std::string_view SANDBOX_VERSION{"ChessSandbox 1.0v"};
}
