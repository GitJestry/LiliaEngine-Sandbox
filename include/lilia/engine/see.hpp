#pragma once
#include "lilia/chess/position.hpp"
#include "lilia/chess/move.hpp"
#include "lilia/chess/move_helper.hpp"

namespace lilia::engine::see
{
  // threshold = 0 -> non negative see
  [[nodiscard]] bool see_ge_impl(const chess::Position &pos,
                                 const chess::Move &m,
                                 int threshold) noexcept;
}
