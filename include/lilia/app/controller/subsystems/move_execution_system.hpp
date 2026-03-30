#pragma once

#include <atomic>

#include "lilia/app/view/audio/sound_manager.hpp"
#include "lilia/app/view/ui/screens/game_view.hpp"

namespace lilia::chess
{
  class ChessGame;
  struct Move;
} // namespace lilia::model

namespace lilia::app::controller
{

  class LegalMoveCache;
  class HistorySystem;
  class ClockSystem;
  class PremoveSystem;

  class MoveExecutionSystem
  {
  public:
    MoveExecutionSystem(view::ui::GameView &view, chess::ChessGame &game, view::audio::SoundManager &sfx, LegalMoveCache &legal, HistorySystem &history,
                        ClockSystem &clock, PremoveSystem &premove);

    void applyMove(const chess::Move &move, bool isPlayerMove, bool onClick);

  private:
    view::ui::GameView &m_view;
    chess::ChessGame &m_game;
    view::audio::SoundManager &m_sfx;

    LegalMoveCache &m_legal;
    HistorySystem &m_history;
    ClockSystem &m_clock;
    PremoveSystem &m_premove;
  };

} // namespace lilia::controller
