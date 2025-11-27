#pragma once

#include <optional>
#include <string>
#include <vector>

#include "lilia/chess_types.hpp"
#include "lilia/model/move.hpp"

namespace lilia::model {

struct PgnMove {
  Move move;
  std::string san;
  core::Color mover;
  core::PieceType captured;
  bool gaveCheck{false};
  bool gaveMate{false};
};

struct PgnGame {
  std::string startFen;
  std::vector<std::string> fenHistory;  // includes start position at index 0
  std::vector<PgnMove> moves;
  std::string result;
};

std::optional<PgnGame> parsePgn(const std::string &pgnText);

}  // namespace lilia::model
