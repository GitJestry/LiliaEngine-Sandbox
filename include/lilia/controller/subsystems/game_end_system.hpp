#pragma once

#include <string>

#include "../../chess_types.hpp"
#include "../../view/audio/sound_manager.hpp"
#include "lilia/view/ui/screens/game_view.hpp"
#include "../game_controller_types.hpp"

namespace lilia::model
{
  class ChessGame;
}

namespace lilia::controller
{

  class ClockSystem;
  class PremoveSystem;

  class GameEndSystem
  {
  public:
    GameEndSystem(view::GameView &view, model::ChessGame &game, view::sound::SoundManager &sfx);

    void show(core::GameResult res, core::Color sideToMove, bool whiteIsBot, bool blackIsBot,
              ClockSystem &clock, PremoveSystem &premove);

  private:
    view::GameView &m_view;
    model::ChessGame &m_game;
    view::sound::SoundManager &m_sfx;
  };

} // namespace lilia::controller
