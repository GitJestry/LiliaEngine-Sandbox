#pragma once

#include <string>

#include "lilia/app/view/audio/sound_manager.hpp"
#include "lilia/app/view/ui/screens/game_view.hpp"
#include "lilia/app/controller/game_controller_types.hpp"

namespace lilia::app::controller
{

  class ClockSystem;
  class PremoveSystem;

  class GameEndSystem
  {
  public:
    GameEndSystem(view::ui::GameView &view, view::audio::SoundManager &sfx);

    void show(chess::GameResult res, chess::Color sideToMove, bool whiteIsBot, bool blackIsBot,
              ClockSystem &clock, PremoveSystem &premove);

  private:
    view::ui::GameView &m_view;
    view::audio::SoundManager &m_sfx;
  };

}
