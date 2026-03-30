#pragma once

#include <atomic>
#include <cstddef>
#include <string>
#include <vector>

#include "lilia/app/view/audio/sound_manager.hpp"
#include "lilia/app/view/ui/screens/game_view.hpp"
#include "lilia/app/controller/game_controller_types.hpp"
#include "lilia/app/controller/selection_manager.hpp"
#include "lilia/app/domain/analysis/analysis_types.hpp"
#include "lilia/app/domain/game_record.hpp"

namespace lilia::chess
{
  class ChessGame;
}

namespace lilia::app::controller
{

  class PremoveSystem;

  class HistorySystem
  {
  public:
    HistorySystem(view::ui::GameView &view, chess::ChessGame &game, SelectionManager &sel,
                  view::audio::SoundManager &sfx);

    // removed startEval
    void reset(const std::string &startFen, const domain::analysis::TimeView &startTime);

    bool atHead() const;
    std::size_t fenIndex() const { return m_fen_index; }
    const std::string &fenAt(std::size_t idx) const { return m_fen_history[idx]; }
    const std::string &currentFen() const { return m_fen_history[m_fen_index]; }

    void ensureHeadVisibleForLivePlay();

    // removed evalAfter
    void onMoveCommitted(const MoveView &mv, const std::string &fenAfter, const domain::analysis::TimeView &timeAfter);

    bool handleMoveListClick(view::MousePos mp, PremoveSystem &premove);
    void onWheelScroll(float delta);
    void stepBackward(PremoveSystem &premove);
    void stepForward(PremoveSystem &premove);

    void syncCapturedPieces();

    void stashSelectedPiece();
    void restoreSelectedPiece();

    // Builds history vectors from a record. silent=true avoids sfx on build.
    bool loadFromRecord(const domain::GameRecord &rec,
                        bool populateMoveListWithSan = true);
    domain::GameRecord toRecord() const;

  private:
    static constexpr std::size_t kInvalidMoveIdx = static_cast<std::size_t>(-1);

    view::ui::GameView &m_view;
    chess::ChessGame &m_game;
    SelectionManager &m_sel;
    view::audio::SoundManager &m_sfx;

    std::vector<std::string> m_fen_history;
    std::vector<MoveView> m_move_history;
    std::vector<domain::analysis::TimeView> m_time_history;

    std::size_t m_fen_index{0};
    chess::Square m_stashed_selected{chess::NO_SQUARE};
  };

} // namespace lilia::controller
