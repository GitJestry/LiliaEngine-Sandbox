#pragma once
#include "lilia/chess/position.hpp"
#include "lilia/chess/move.hpp"
#include "lilia/chess/move_helper.hpp"

namespace lilia::engine::see
{

  bool non_negative(const chess::Position &pos, const chess::Move &m);
}
