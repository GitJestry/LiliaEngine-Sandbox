#pragma once

#include <atomic>
#include <memory>
#include <string>

namespace sf
{
  class Event;
}

#include "lilia/app/view/audio/sound_manager.hpp"
#include "lilia/app/view/ui/screens/game_view.hpp"
#include "game_controller_types.hpp"
#include "input_manager.hpp"
#include "selection_manager.hpp"
#include "lilia/app/domain/game_record.hpp"

#include "lilia/app/domain/analysis/config/start_config.hpp"

namespace lilia::chess
{
  class ChessGame;
  struct Move;
} // namespace lilia::chess

namespace lilia::app::controller
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
    explicit GameController(view::ui::GameView &gView, chess::ChessGame &game);
    ~GameController();

    void startGame(const domain::analysis::config::StartConfig &cfg);

    app::domain::GameRecord buildGameRecord() const;
    void startReplay(const app::domain::GameRecord &rec);

    void update(float dt);
    void handleEvent(const sf::Event &event);
    void render();

    [[nodiscard]] NextAction getNextAction() const { return m_next_action; }

  private:
    void resign();

    view::ui::GameView &m_view;
    chess::ChessGame &m_game;

    InputManager m_input;
    view::audio::SoundManager m_sfx;
    SelectionManager m_selection;

    std::unique_ptr<GameManager> m_game_manager;

    bool m_white_is_bot{false};
    bool m_black_is_bot{false};
    bool m_replay_mode{false};

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
