#pragma once

#include <SFML/Window/Event.hpp>
#include <chrono>
#include <optional>

#include "../../chess_types.hpp"
#include "../../view/audio/sound_manager.hpp"
#include "lilia/view/ui/screens/game_view.hpp"
#include "../input_manager.hpp"
#include "../selection_manager.hpp"

namespace lilia::model
{
  class ChessGame;
}

namespace lilia::controller
{

  class GameManager;
  class LegalMoveCache;
  class AttackSystem;
  class PremoveSystem;

  class BoardInputSystem
  {
  public:
    BoardInputSystem(view::GameView &view, model::ChessGame &game, InputManager &input,
                     SelectionManager &sel, view::sound::SoundManager &sfx, AttackSystem &attacks,
                     PremoveSystem &premove, LegalMoveCache &legal);

    void setGameManager(GameManager *gm) { m_game_manager = gm; }

    void bindInputCallbacks();

    void onMouseMove(core::MousePos pos);
    void onMousePressed(core::MousePos pos);
    void onMouseReleased(core::MousePos pos);
    void onRightPressed(core::MousePos pos);
    void onRightReleased(core::MousePos pos);

    void onMouseEntered();
    void onLostFocus();

    void refreshActiveHighlights();

  private:
    void onClick(core::MousePos mousePos);
    void onDrag(core::MousePos start, core::MousePos current);
    void onDrop(core::MousePos start, core::MousePos end);

    bool isHumanPiece(core::Square sq) const;

    bool tryMove(core::Square a, core::Square b) const;
    void showAttacks(const std::vector<core::Square> &att);

    view::GameView &m_view;
    model::ChessGame &m_game;
    InputManager &m_input;
    SelectionManager &m_sel;
    view::sound::SoundManager &m_sfx;
    AttackSystem &m_attacks;
    PremoveSystem &m_premove;
    LegalMoveCache &m_legal;

    GameManager *m_game_manager{nullptr};

    bool m_dragging{false};
    bool m_mouse_down{false};
    bool m_right_mouse_down{false};

    core::Square m_drag_from{core::NO_SQUARE};
    core::Square m_right_drag_from{core::NO_SQUARE};
    std::chrono::steady_clock::time_point m_right_press_time{};

    bool m_preview_active{false};
    core::Square m_prev_selected_before_preview{core::NO_SQUARE};
    bool m_selection_changed_on_press{false};
  };

} // namespace lilia::controller
