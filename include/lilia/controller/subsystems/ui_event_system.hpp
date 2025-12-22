#pragma once

#include <SFML/Window/Event.hpp>

#include "../../view/game_view.hpp"
#include "../game_controller_types.hpp"

namespace lilia::model {
class ChessGame;
}

namespace lilia::controller {

class HistorySystem;
class PremoveSystem;

class UiEventSystem {
 public:
  UiEventSystem(view::GameView& view, model::ChessGame& game, HistorySystem& history,
                PremoveSystem& premove, NextAction& nextAction);

  void setResignHandler(void (*fn)(void*), void* ctx) {
    m_resign_fn = fn;
    m_resign_ctx = ctx;
  }

  bool handleEvent(const sf::Event& event);

 private:
  view::GameView& m_view;
  model::ChessGame& m_game;
  HistorySystem& m_history;
  PremoveSystem& m_premove;
  NextAction& m_next_action;

  void (*m_resign_fn)(void*){nullptr};
  void* m_resign_ctx{nullptr};
};

}  // namespace lilia::controller
