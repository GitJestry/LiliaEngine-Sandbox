#pragma once

#include "lilia/chess/chess_types.hpp"

namespace lilia::app::domain::analysis
{

  struct TimeView
  {
    float white{};
    float black{};
    chess::Color active = chess::Color::White;
  };

}
