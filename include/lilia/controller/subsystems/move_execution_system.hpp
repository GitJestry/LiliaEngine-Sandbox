#pragma once

#include <atomic>

#include "../../chess_types.hpp"
#include "../../view/audio/sound_manager.hpp"
#include "lilia/view/ui/screens/game_view.hpp"
#include "../game_controller_types.hpp"

namespace lilia::model
{
  class ChessGame;
  struct Move;
} // namespace lilia::model

namespace lilia::controller
{

  class LegalMoveCache;
  class HistorySystem;
  class ClockSystem;
  class PremoveSystem;

  class MoveExecutionSystem
  {
  public:
    MoveExecutionSystem(view::GameView &view, model::ChessGame &game, view::sound::SoundManager &sfx,
                        std::atomic<int> &evalCp, LegalMoveCache &legal, HistorySystem &history,
                        ClockSystem &clock, PremoveSystem &premove);

    void applyMove(const model::Move &move, bool isPlayerMove, bool onClick);

  private:
    view::GameView &m_view;
    model::ChessGame &m_game;
    view::sound::SoundManager &m_sfx;
    std::atomic<int> &m_eval_cp;

    LegalMoveCache &m_legal;
    HistorySystem &m_history;
    ClockSystem &m_clock;
    PremoveSystem &m_premove;
  };

} // namespace lilia::controller
