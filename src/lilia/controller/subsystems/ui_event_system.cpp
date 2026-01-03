#include "lilia/controller/subsystems/ui_event_system.hpp"

#include <SFML/Window/Clipboard.hpp>
#include <SFML/Window/Mouse.hpp>

#include "lilia/controller/subsystems/history_system.hpp"
#include "lilia/controller/subsystems/premove_system.hpp"
#include "lilia/model/chess_game.hpp"
#include "lilia/view/ui/style/modals/modal.hpp"

namespace lilia::controller
{

  UiEventSystem::UiEventSystem(view::GameView &view, model::ChessGame &game, HistorySystem &history,
                               PremoveSystem &premove, NextAction &nextAction)
      : m_view(view),
        m_game(game),
        m_history(history),
        m_premove(premove),
        m_next_action(nextAction) {}

  bool UiEventSystem::handleEvent(const sf::Event &event)
  {
    // --- Modal-first routing ---
    // With the new modal system, the modal owns hit-testing and button logic.
    // While any modal is open, we forward events to it and block underlying UI interactions.
    if (m_view.isAnyModalOpen())
    {
      (void)m_view.handleModalEvent(event);

      // Apply modal actions (if any) after the modal has processed the event.
      const auto action = m_view.consumeModalAction();
      switch (action)
      {
      case view::ui::ModalAction::ResignYes:
        if (m_resign_fn)
          m_resign_fn(m_resign_ctx);
        m_view.hideResignPopup();
        break;

      case view::ui::ModalAction::ResignNo:
      case view::ui::ModalAction::Close:
        // Close should dismiss whichever modal is currently open.
        if (m_view.isResignPopupOpen())
          m_view.hideResignPopup();
        if (m_view.isGameOverPopupOpen())
          m_view.hideGameOverPopup();
        break;

      case view::ui::ModalAction::NewBot:
        m_next_action = NextAction::NewBot;
        m_view.hideGameOverPopup();
        break;

      case view::ui::ModalAction::Rematch:
        m_next_action = NextAction::Rematch;
        m_view.hideGameOverPopup();
        break;

      case view::ui::ModalAction::None:
      default:
        break;
      }

      return true; // modal blocks the rest of the UI
    }

    // --- Normal UI (no modal open) ---
    if (event.type == sf::Event::MouseButtonPressed && event.mouseButton.button == sf::Mouse::Left)
    {
      const core::MousePos mp(event.mouseButton.x, event.mouseButton.y);

      if (m_view.isOnEvalToggle(mp))
      {
        m_view.toggleEvalBarVisibility();
        return true;
      }

      if (m_view.isOnFlipIcon(mp))
      {
        m_view.toggleBoardOrientation();
        m_premove.onBoardFlipped();
        return true;
      }

      const auto opt = m_view.getOptionAt(mp);
      switch (opt)
      {
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
        // Placeholder / future hook
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

      if (m_history.handleMoveListClick(mp, m_premove))
        return true;
    }

    if (event.type == sf::Event::MouseWheelScrolled)
    {
      m_history.onWheelScroll(event.mouseWheelScroll.delta);
      return true;
    }

    if (event.type == sf::Event::KeyPressed)
    {
      if (event.key.code == sf::Keyboard::Left)
      {
        m_history.stepBackward(m_premove);
        return true;
      }
      if (event.key.code == sf::Keyboard::Right)
      {
        m_history.stepForward(m_premove);
        return true;
      }
    }

    // When game is over, allow board flip via icon click (kept from your old behavior).
    if (m_game.getResult() != core::GameResult::ONGOING)
    {
      if (event.type == sf::Event::MouseButtonPressed && event.mouseButton.button == sf::Mouse::Left)
      {
        const core::MousePos mp(event.mouseButton.x, event.mouseButton.y);
        if (m_view.isOnFlipIcon(mp))
        {
          m_view.toggleBoardOrientation();
          m_premove.onBoardFlipped();
          return true;
        }
      }
    }

    return false;
  }

} // namespace lilia::controller
