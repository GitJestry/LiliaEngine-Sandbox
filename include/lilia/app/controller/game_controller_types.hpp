#pragma once

#include <deque>
#include <string>
#include <vector>

#include "lilia/app/domain/analysis/analysis_types.hpp"
#include "lilia/chess/move.hpp"
#include "lilia/app/view/audio/sound_effect.hpp"

namespace lilia::app::controller
{

  struct MoveView
  {
    chess::Move move;
    chess::Color moverColor;
    chess::PieceType capturedType;
    view::audio::Effect sound;
  };

  struct Premove
  {
    chess::Square from;
    chess::Square to;
    chess::PieceType promotion = chess::PieceType::None;
    chess::PieceType capturedType = chess::PieceType::None;
    chess::Color capturedColor = chess::Color::White;
    chess::Color moverColor = chess::Color::White;
  };

  enum class NextAction
  {
    None,
    NewBot,
    Rematch
  };

} // namespace lilia::controller
