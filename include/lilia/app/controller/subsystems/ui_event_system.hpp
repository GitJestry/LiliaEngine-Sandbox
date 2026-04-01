#pragma once

#include <SFML/Window/Event.hpp>

#include "lilia/app/view/ui/screens/game_view.hpp"
#include "lilia/app/controller/game_controller_types.hpp"

namespace lilia::chess
{
  class ChessGame;
}

namespace lilia::app::controller
{

  class HistorySystem;
  class PremoveSystem;

  class UiEventSystem
  {
  public:
    UiEventSystem(view::ui::GameView &view, chess::ChessGame &game, HistorySystem &history,
                  PremoveSystem &premove, NextAction &nextAction);

    void setResignHandler(void (*fn)(void *), void *ctx)
    {
      m_resign_fn = fn;
      m_resign_ctx = ctx;
    }

    bool handleEvent(const sf::Event &event);

  private:
    view::ui::GameView &m_view;
    chess::ChessGame &m_game;
    HistorySystem &m_history;
    PremoveSystem &m_premove;
    NextAction &m_next_action;

    void (*m_resign_fn)(void *){nullptr};
    void *m_resign_ctx{nullptr};
  };

}
