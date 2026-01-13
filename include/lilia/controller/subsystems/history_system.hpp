#pragma once

#include <atomic>
#include <cstddef>
#include <string>
#include <vector>

#include "../../chess_types.hpp"
#include "../../view/audio/sound_manager.hpp"
#include "lilia/view/ui/screens/game_view.hpp"
#include "../game_controller_types.hpp"
#include "../selection_manager.hpp"
#include "lilia/model/analysis/analysis_types.hpp"
#include "lilia/model/analysis/game_record.hpp"

namespace lilia::model
{
  class ChessGame;
}

namespace lilia::controller
{

  class PremoveSystem;

  class HistorySystem
  {
  public:
    HistorySystem(view::GameView &view, model::ChessGame &game, SelectionManager &sel,
                  view::sound::SoundManager &sfx, std::atomic<int> &evalCp);

    // removed startEval
    void reset(const std::string &startFen, const model::analysis::TimeView &startTime);

    bool atHead() const;
    std::size_t fenIndex() const { return m_fen_index; }
    const std::string &fenAt(std::size_t idx) const { return m_fen_history[idx]; }
    const std::string &currentFen() const { return m_fen_history[m_fen_index]; }

    void ensureHeadVisibleForLivePlay();

    // removed evalAfter
    void onMoveCommitted(const MoveView &mv, const std::string &fenAfter, const model::analysis::TimeView &timeAfter);

    bool handleMoveListClick(core::MousePos mp, PremoveSystem &premove);
    void onWheelScroll(float delta);
    void stepBackward(PremoveSystem &premove);
    void stepForward(PremoveSystem &premove);

    void updateEvalAtHead();
    void syncCapturedPieces();

    void stashSelectedPiece();
    void restoreSelectedPiece();

    // Builds history vectors from a record. silent=true avoids sfx on build.
    bool loadFromRecord(const model::analysis::GameRecord &rec,
                        bool populateMoveListWithSan = true);
    model::analysis::GameRecord toRecord() const;

  private:
    static constexpr std::size_t kInvalidMoveIdx = static_cast<std::size_t>(-1);

    view::GameView &m_view;
    model::ChessGame &m_game;
    SelectionManager &m_sel;
    view::sound::SoundManager &m_sfx;

    // keep live eval
    std::atomic<int> &m_eval_cp;

    std::vector<std::string> m_fen_history;
    std::vector<MoveView> m_move_history;
    std::vector<model::analysis::TimeView> m_time_history;

    std::size_t m_fen_index{0};
    core::Square m_stashed_selected{core::NO_SQUARE};
  };

} // namespace lilia::controller
