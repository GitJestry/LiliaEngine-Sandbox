#pragma once
#include <string>
#include <string_view>
#include <vector>

#include "lilia/model/position.hpp"
#include "lilia/model/move.hpp"

namespace lilia::model::notation
{

  std::string toSan(const model::Position &pos, const model::Move &mv);

  // Finds the move corresponding to a SAN token in the given position.
  bool fromSan(const model::Position &pos, std::string_view sanToken, model::Move &out);

} // namespace lilia::model::notation
