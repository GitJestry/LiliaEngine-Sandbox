#pragma once
#include <string>
#include <string_view>
#include <vector>

#include "lilia/chess/position.hpp"
#include "lilia/chess/move.hpp"

namespace lilia::app::domain::notation
{
  std::string toSan(const chess::Position &pos, const chess::Move &mv);

  // Finds the move corresponding to a SAN token in the given position.
  bool fromSan(const chess::Position &pos, std::string_view sanToken, chess::Move &out);

}
