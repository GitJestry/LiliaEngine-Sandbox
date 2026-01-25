#pragma once

#include <deque>
#include <string>
#include <vector>

#include "lilia/model/analysis/analysis_types.hpp"
#include "lilia/model/move.hpp"
#include "lilia/view/audio/sound_effect.hpp"

namespace lilia::controller
{

  struct MoveView
  {
    model::Move move;
    core::Color moverColor;
    core::PieceType capturedType;
    view::sound::Effect sound;
  };

  struct Premove
  {
    core::Square from;
    core::Square to;
    core::PieceType promotion = core::PieceType::None;
    core::PieceType capturedType = core::PieceType::None;
    core::Color capturedColor = core::Color::White;
    core::Color moverColor = core::Color::White;
  };

  enum class NextAction
  {
    None,
    NewBot,
    Rematch
  };

} // namespace lilia::controller
