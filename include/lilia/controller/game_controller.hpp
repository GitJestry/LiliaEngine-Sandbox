#pragma once

#include <atomic>
#include <memory>
#include <string>

namespace sf
{
  class Event;
}

#include "../chess_types.hpp"
#include "../constants.hpp"
#include "../view/audio/sound_manager.hpp"
#include "lilia/view/ui/screens/game_view.hpp"
#include "game_controller_types.hpp"
#include "input_manager.hpp"
#include "selection_manager.hpp"

namespace lilia::model
{
  class ChessGame;
  struct Move;
} // namespace lilia::model

namespace lilia::controller
{

  class GameManager;

  class LegalMoveCache;
  class AttackSystem;
  class PremoveSystem;
  class HistorySystem;
  class ClockSystem;
  class MoveExecutionSystem;
  class GameEndSystem;
  class BoardInputSystem;
  class UiEventSystem;

  class GameController
  {
  public:
    explicit GameController(view::GameView &gView, model::ChessGame &game);
    ~GameController();

    void startGame(const std::string &fen = core::START_FEN, bool whiteIsBot = false,
                   bool blackIsBot = true, int whiteThinkTimeMs = 1000, int whiteDepth = 5,
                   int blackThinkTimeMs = 1000, int blackDepth = 5, bool useTimer = true,
                   int baseSeconds = 0, int incrementSeconds = 0);

    void update(float dt);
    void handleEvent(const sf::Event &event);
    void render();

    [[nodiscard]] NextAction getNextAction() const { return m_next_action; }

  private:
    void resign();

    view::GameView &m_view;
    model::ChessGame &m_game;

    InputManager m_input;
    view::sound::SoundManager m_sfx;
    SelectionManager m_selection;

    std::unique_ptr<GameManager> m_game_manager;

    bool m_white_is_bot{false};
    bool m_black_is_bot{false};

    std::atomic<int> m_eval_cp{0};
    NextAction m_next_action{NextAction::None};

    std::unique_ptr<LegalMoveCache> m_legal;
    std::unique_ptr<AttackSystem> m_attacks;
    std::unique_ptr<PremoveSystem> m_premove;
    std::unique_ptr<HistorySystem> m_history;
    std::unique_ptr<ClockSystem> m_clock;
    std::unique_ptr<MoveExecutionSystem> m_move_exec;
    std::unique_ptr<GameEndSystem> m_game_end;
    std::unique_ptr<BoardInputSystem> m_board_input;
    std::unique_ptr<UiEventSystem> m_ui;
  };

} // namespace lilia::controller
