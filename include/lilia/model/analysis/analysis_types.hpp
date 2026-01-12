#pragma once

#include "lilia/chess_types.hpp"

namespace lilia::model::analysis
{

  struct TimeView
  {
    float white{};
    float black{};
    core::Color active = core::Color::White;
  };

}
