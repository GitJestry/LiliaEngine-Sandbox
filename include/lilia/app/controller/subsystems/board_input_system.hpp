#pragma once

#include <SFML/Window/Event.hpp>
#include <chrono>
#include <optional>

#include "lilia/app/view/audio/sound_manager.hpp"
#include "lilia/app/view/ui/screens/game_view.hpp"
#include "lilia/app/controller/input_manager.hpp"
#include "lilia/app/controller/selection_manager.hpp"
#include "lilia/app/controller/subsystems/attack_system.hpp"

namespace lilia::chess
{
  class ChessGame;
}

namespace lilia::app::controller
{

  class GameManager;
  class LegalMoveCache;
  class PremoveSystem;

  class BoardInputSystem
  {
  public:
    BoardInputSystem(view::ui::GameView &view, chess::ChessGame &game, InputManager &input,
                     SelectionManager &sel, view::audio::SoundManager &sfx,
                     PremoveSystem &premove, AttackSystem &att);

    /// @brief Sets the gamemanager pointer internally
    /// @param gm
    void setGameManager(GameManager *gm) { m_game_manager = gm; }

    /// @brief sets up the input callbacks from the current onClick, onDrag and onDrop
    void bindInputCallbacks();

    /// @brief
    /// @param pos
    void onMouseMove(view::MousePos pos);
    void onMousePressed(view::MousePos pos);
    void onMouseReleased(view::MousePos pos);
    void onRightPressed(view::MousePos pos);
    void onRightReleased(view::MousePos pos);

    void onMouseEntered();
    void onLostFocus();

    void refreshActiveHighlights();

  private:
    void onClick(view::MousePos mousePos);
    void onDrag(view::MousePos start, view::MousePos current);
    void onDrop(view::MousePos start, view::MousePos end);

    bool isHumanPiece(chess::Square sq) const;

    bool tryMove(chess::Square a, chess::Square b) const;
    void showAttacks(const std::vector<chess::Square> &att);

    view::ui::GameView &m_view;
    chess::ChessGame &m_game;
    InputManager &m_input;
    SelectionManager &m_sel;
    view::audio::SoundManager &m_sfx;
    AttackSystem &m_attacks;
    PremoveSystem &m_premove;

    GameManager *m_game_manager{nullptr};

    bool m_dragging{false};
    bool m_mouse_down{false};
    bool m_right_mouse_down{false};

    chess::Square m_drag_from{chess::NO_SQUARE};
    chess::Square m_right_drag_from{chess::NO_SQUARE};
    std::chrono::steady_clock::time_point m_right_press_time{};

    bool m_preview_active{false};
    chess::Square m_prev_selected_before_preview{chess::NO_SQUARE};
    bool m_selection_changed_on_press{false};
  };

}
