#pragma once

#include <string>

#include "lilia/view/audio/sound_manager.hpp"
#include "lilia/view/ui/screens/game_view.hpp"
#include "lilia/controller/game_controller_types.hpp"

namespace lilia::controller
{

  class ClockSystem;
  class PremoveSystem;

  class GameEndSystem
  {
  public:
    GameEndSystem(view::GameView &view, view::sound::SoundManager &sfx);

    void show(core::GameResult res, core::Color sideToMove, bool whiteIsBot, bool blackIsBot,
              ClockSystem &clock, PremoveSystem &premove);

  private:
    view::GameView &m_view;
    view::sound::SoundManager &m_sfx;
  };

} // namespace lilia::controller
