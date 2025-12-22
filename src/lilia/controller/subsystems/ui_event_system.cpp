#include "lilia/controller/subsystems/ui_event_system.hpp"

#include <SFML/Window/Clipboard.hpp>
#include <SFML/Window/Mouse.hpp>

#include "lilia/controller/subsystems/history_system.hpp"
#include "lilia/controller/subsystems/premove_system.hpp"
#include "lilia/model/chess_game.hpp"

namespace lilia::controller {

UiEventSystem::UiEventSystem(view::GameView& view, model::ChessGame& game, HistorySystem& history,
                             PremoveSystem& premove, NextAction& nextAction)
    : m_view(view),
      m_game(game),
      m_history(history),
      m_premove(premove),
      m_next_action(nextAction) {}

bool UiEventSystem::handleEvent(const sf::Event& event) {
  if (m_view.isResignPopupOpen() || m_view.isGameOverPopupOpen()) {
    if (event.type == sf::Event::MouseButtonPressed &&
        event.mouseButton.button == sf::Mouse::Left) {
      const core::MousePos mp(event.mouseButton.x, event.mouseButton.y);

      if (m_view.isResignPopupOpen()) {
        if (m_view.isOnResignYes(mp)) {
          if (m_resign_fn) m_resign_fn(m_resign_ctx);
          m_view.hideResignPopup();
        } else if (m_view.isOnResignNo(mp) || m_view.isOnModalClose(mp)) {
          m_view.hideResignPopup();
        }
      } else {
        if (m_view.isOnNewBot(mp)) {
          m_next_action = NextAction::NewBot;
          m_view.hideGameOverPopup();
        } else if (m_view.isOnRematch(mp)) {
          m_next_action = NextAction::Rematch;
          m_view.hideGameOverPopup();
        } else if (m_view.isOnModalClose(mp)) {
          m_view.hideGameOverPopup();
        }
      }
    }
    return true;
  }

  if (event.type == sf::Event::MouseButtonPressed && event.mouseButton.button == sf::Mouse::Left) {
    const core::MousePos mp(event.mouseButton.x, event.mouseButton.y);

    if (m_view.isOnEvalToggle(mp)) {
      m_view.toggleEvalBarVisibility();
      return true;
    }

    if (m_view.isOnFlipIcon(mp)) {
      m_view.toggleBoardOrientation();
      m_premove.onBoardFlipped();
      return true;
    }

    const auto opt = m_view.getOptionAt(mp);
    switch (opt) {
      case view::MoveListView::Option::Resign:
        m_view.showResignPopup();
        return true;
      case view::MoveListView::Option::Prev:
        m_history.stepBackward(m_premove);
        return true;
      case view::MoveListView::Option::Next:
        m_history.stepForward(m_premove);
        return true;
      case view::MoveListView::Option::Settings:
        return true;
      case view::MoveListView::Option::NewBot:
        m_next_action = NextAction::NewBot;
        return true;
      case view::MoveListView::Option::Rematch:
        m_next_action = NextAction::Rematch;
        return true;
      case view::MoveListView::Option::ShowFen:
        sf::Clipboard::setString(m_history.currentFen());
        return true;
      default:
        break;
    }

    if (m_history.handleMoveListClick(mp, m_premove)) return true;
  }

  if (event.type == sf::Event::MouseWheelScrolled) {
    m_history.onWheelScroll(event.mouseWheelScroll.delta);
    return true;
  }

  if (event.type == sf::Event::KeyPressed) {
    if (event.key.code == sf::Keyboard::Left) {
      m_history.stepBackward(m_premove);
      return true;
    }
    if (event.key.code == sf::Keyboard::Right) {
      m_history.stepForward(m_premove);
      return true;
    }
  }

  if (m_game.getResult() != core::GameResult::ONGOING) {
    if (event.type == sf::Event::MouseButtonPressed &&
        event.mouseButton.button == sf::Mouse::Left) {
      const core::MousePos mp(event.mouseButton.x, event.mouseButton.y);
      if (m_view.isOnFlipIcon(mp)) {
        m_view.toggleBoardOrientation();
        m_premove.onBoardFlipped();
        return true;
      }
    }
  }

  return false;
}

}  // namespace lilia::controller
