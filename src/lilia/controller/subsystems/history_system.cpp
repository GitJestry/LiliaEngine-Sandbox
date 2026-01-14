#include "lilia/controller/subsystems/history_system.hpp"

#include "lilia/controller/subsystems/premove_system.hpp"
#include "lilia/model/chess_game.hpp"
#include "lilia/model/analysis/game_record.hpp"
#include "lilia/model/analysis/san_notation.hpp"
#include "lilia/uci/uci_helper.hpp"
#include "lilia/model/analysis/result_utils.hpp"

namespace lilia::controller
{

  HistorySystem::HistorySystem(view::GameView &view, model::ChessGame &game, SelectionManager &sel,
                               view::sound::SoundManager &sfx, std::atomic<int> &evalCp)
      : m_view(view), m_game(game), m_sel(sel), m_sfx(sfx), m_eval_cp(evalCp) {}

  void HistorySystem::reset(const std::string &startFen, const model::analysis::TimeView &startTime)
  {
    m_fen_history.clear();
    m_move_history.clear();
    m_time_history.clear();

    m_fen_history.reserve(512);
    m_move_history.reserve(512);
    m_time_history.reserve(512);

    m_fen_history.push_back(startFen);
    m_time_history.push_back(startTime);

    m_fen_index = 0;
    m_view.selectMove(kInvalidMoveIdx);

    m_view.updateFen(startFen);
    m_view.setBoardFen(startFen);
    m_view.clearCapturedPieces();

    // eval: start neutral or current live value; choose one:
    m_view.resetEvalBar();
    m_view.updateEval(m_eval_cp.load());

    stashSelectedPiece();
    restoreSelectedPiece();
  }

  bool HistorySystem::atHead() const
  {
    return m_fen_index == (m_fen_history.empty() ? 0 : m_fen_history.size() - 1);
  }

  void HistorySystem::ensureHeadVisibleForLivePlay()
  {
    if (m_fen_history.empty())
      return;
    if (atHead())
      return;

    m_fen_index = m_fen_history.size() - 1;
    m_view.setBoardFen(m_fen_history[m_fen_index]);

    // at head: show live eval
    m_view.updateEval(m_eval_cp.load());

    m_view.selectMove(m_fen_index ? m_fen_index - 1 : kInvalidMoveIdx);
    m_view.clearAllHighlights();

    if (!m_move_history.empty())
    {
      const MoveView &info = m_move_history.back();
      m_sel.clearLastMoveHighlight();
      m_sel.setLastMove(info.move.from(), info.move.to());
      m_sel.highlightLastMove();
    }

    syncCapturedPieces();

    if (m_fen_index < m_time_history.size())
    {
      const model::analysis::TimeView &tv = m_time_history[m_fen_index];
      m_view.updateClock(core::Color::White, tv.white);
      m_view.updateClock(core::Color::Black, tv.black);
      m_view.setClockActive(tv.active);
    }

    m_view.restoreRightClickHighlights();
    restoreSelectedPiece();
  }

  void HistorySystem::onMoveCommitted(const MoveView &mv, const std::string &fenAfter,
                                      const model::analysis::TimeView &timeAfter)
  {
    // Build SAN from the *previous* position (current head FEN before push)
    std::string moveText;
    if (!m_fen_history.empty())
    {
      model::ChessGame tmp;
      tmp.setPosition(m_fen_history.back());
      const model::Position &pos = tmp.getPositionRefForBot();
      moveText = model::notation::toSan(pos, mv.move);
    }

    if (moveText.empty())
      moveText = move_to_uci(mv.move);

    m_view.addMove(moveText);

    // existing logic...
    m_move_history.push_back(mv);
    m_fen_history.push_back(fenAfter);
    m_time_history.push_back(timeAfter);

    m_fen_index = m_fen_history.size() - 1;

    m_view.updateFen(fenAfter);
    m_view.selectMove(m_fen_index ? m_fen_index - 1 : kInvalidMoveIdx);

    // show live eval (engine callback updates m_eval_cp asynchronously)
    m_view.updateEval(m_eval_cp.load());

    if (m_game.getResult() != core::GameResult::ONGOING)
    {
      m_view.setEvalResult(model::analysis::result_string(m_game.getResult(), m_game.getGameState().sideToMove, /*forPgn=*/false));
    }

    m_sel.dehoverSquare();
    if (m_sel.getSelectedSquare() != core::NO_SQUARE)
      m_sel.deselectSquare();

    m_view.clearAttackHighlights();
    m_view.clearNonPremoveHighlights();

    m_sel.clearLastMoveHighlight();
    m_sel.setLastMove(mv.move.from(), mv.move.to());
    m_sel.highlightLastMove();
  }

  bool HistorySystem::handleMoveListClick(core::MousePos mp, PremoveSystem &premove)
  {
    const std::size_t idx = m_view.getMoveIndexAt(mp);
    if (idx == kInvalidMoveIdx)
      return false;

    premove.clearAll();

    const bool leavingFinalState = (m_game.getResult() != core::GameResult::ONGOING && atHead() &&
                                    idx + 1 != m_fen_history.size() - 1);

    const bool enteringFinalState =
        (m_game.getResult() != core::GameResult::ONGOING && idx + 1 == m_fen_history.size() - 1);

    if (leavingFinalState)
      m_view.resetEvalBar();

    if (atHead() && idx + 1 != m_fen_history.size() - 1)
    {
      m_view.stashRightClickHighlights();
      stashSelectedPiece();
    }

    m_fen_index = idx + 1;
    m_view.setBoardFen(m_fen_history[m_fen_index]);
    m_view.selectMove(idx);

    const MoveView &info = m_move_history[idx];
    m_sel.setLastMove(info.move.from(), info.move.to());
    m_view.clearAllHighlights();
    m_sel.highlightLastMove();
    m_sfx.playEffect(info.sound);

    if (enteringFinalState)
    {
      m_view.setEvalResult(model::analysis::result_string(m_game.getResult(), m_game.getGameState().sideToMove, /*forPgn=*/false));
    }

    if (m_fen_index < m_time_history.size())
    {
      const model::analysis::TimeView &tv = m_time_history[m_fen_index];
      m_view.updateClock(core::Color::White, tv.white);
      m_view.updateClock(core::Color::Black, tv.black);
      const bool latest = atHead() && m_game.getResult() == core::GameResult::ONGOING;
      m_view.setClockActive(latest ? std::optional<core::Color>(tv.active) : std::nullopt);
    }

    syncCapturedPieces();

    if (atHead())
    {
      m_view.restoreRightClickHighlights();
      restoreSelectedPiece();
    }

    return true;
  }

  void HistorySystem::onWheelScroll(float delta)
  {
    m_view.scrollMoveList(delta);
  }

  void HistorySystem::syncCapturedPieces()
  {
    m_view.clearCapturedPieces();
    for (std::size_t i = 0; i < m_fen_index && i < m_move_history.size(); ++i)
    {
      const MoveView &mv = m_move_history[i];
      if (mv.move.isCapture())
        m_view.addCapturedPiece(mv.moverColor, mv.capturedType);
    }
  }

  void HistorySystem::stashSelectedPiece()
  {
    m_stashed_selected = m_sel.getSelectedSquare();
    if (m_stashed_selected != core::NO_SQUARE)
      m_sel.deselectSquare();
  }

  void HistorySystem::restoreSelectedPiece()
  {
    if (m_stashed_selected != core::NO_SQUARE)
      m_sel.selectSquare(m_stashed_selected);
    m_stashed_selected = core::NO_SQUARE;
  }

  void HistorySystem::stepBackward(PremoveSystem &premove)
  {
    premove.suspendVisualsIfAtHead(atHead());

    if (atHead())
      stashSelectedPiece();

    if (m_fen_index == 0)
      return;

    const bool leavingFinalState = (m_game.getResult() != core::GameResult::ONGOING && atHead());

    if (atHead())
      m_view.stashRightClickHighlights();

    m_view.setBoardFen(m_fen_history[m_fen_index]);

    const MoveView &info = m_move_history[m_fen_index - 1];

    core::Square epVictim = core::NO_SQUARE;
    if (info.move.isEnPassant())
    {
      epVictim = (info.moverColor == core::Color::White)
                     ? static_cast<core::Square>(info.move.to() - 8)
                     : static_cast<core::Square>(info.move.to() + 8);
    }

    m_view.animationMovePiece(
        info.move.to(), info.move.from(), core::NO_SQUARE, core::PieceType::None,
        [this, info, epVictim]()
        {
          if (info.move.isCapture())
          {
            const core::Square capSq = info.move.isEnPassant() ? epVictim : info.move.to();
            m_view.addPiece(info.capturedType, ~info.moverColor, capSq);
          }
          if (info.move.promotion() != core::PieceType::None)
          {
            m_view.removePiece(info.move.from());
            m_view.addPiece(core::PieceType::Pawn, info.moverColor, info.move.from());
          }
        });

    if (info.move.castle() != model::CastleSide::None)
    {
      const core::Square rookFrom =
          m_game.getRookSquareFromCastleside(info.move.castle(), info.moverColor);
      const core::Square rookTo = (info.move.castle() == model::CastleSide::KingSide)
                                      ? static_cast<core::Square>(info.move.to() - 1)
                                      : static_cast<core::Square>(info.move.to() + 1);
      m_view.animationMovePiece(rookTo, rookFrom);
    }

    --m_fen_index;

    m_view.selectMove(m_fen_index ? m_fen_index - 1 : kInvalidMoveIdx);
    m_sel.setLastMove(info.move.from(), info.move.to());
    m_view.clearAllHighlights();
    m_sel.highlightLastMove();
    m_sfx.playEffect(info.sound);

    m_view.updateFen(m_fen_history[m_fen_index]);

    if (m_fen_index < m_time_history.size())
    {
      const model::analysis::TimeView &tv = m_time_history[m_fen_index];
      m_view.updateClock(core::Color::White, tv.white);
      m_view.updateClock(core::Color::Black, tv.black);
      const bool latest = atHead() && m_game.getResult() == core::GameResult::ONGOING;
      m_view.setClockActive(latest ? std::optional<core::Color>(tv.active) : std::nullopt);
    }

    syncCapturedPieces();

    if (atHead())
    {
      m_view.restoreRightClickHighlights();
      restoreSelectedPiece();
    }
  }

  void HistorySystem::stepForward(PremoveSystem &premove)
  {
    if (m_fen_index >= m_move_history.size())
      return;

    const bool enteringFinalState = (m_game.getResult() != core::GameResult::ONGOING &&
                                     m_fen_index + 1 == m_fen_history.size() - 1);

    m_view.setBoardFen(m_fen_history[m_fen_index]);

    const MoveView &info = m_move_history[m_fen_index];

    core::Square epVictim = core::NO_SQUARE;
    if (info.move.isEnPassant())
    {
      epVictim = (info.moverColor == core::Color::White)
                     ? static_cast<core::Square>(info.move.to() - 8)
                     : static_cast<core::Square>(info.move.to() + 8);
      m_view.removePiece(epVictim);
    }
    else if (info.move.isCapture())
    {
      m_view.removePiece(info.move.to());
    }

    if (info.move.castle() != model::CastleSide::None)
    {
      const core::Square rookFrom =
          m_game.getRookSquareFromCastleside(info.move.castle(), info.moverColor);
      const core::Square rookTo = (info.move.castle() == model::CastleSide::KingSide)
                                      ? static_cast<core::Square>(info.move.to() - 1)
                                      : static_cast<core::Square>(info.move.to() + 1);
      m_view.animationMovePiece(rookFrom, rookTo);
    }

    auto onMainMoveDone = [this, &premove]()
    { premove.restoreVisualsIfNeeded(atHead()); };

    m_view.animationMovePiece(info.move.from(), info.move.to(), epVictim, info.move.promotion(),
                              onMainMoveDone);

    ++m_fen_index;

    m_view.selectMove(m_fen_index ? m_fen_index - 1 : kInvalidMoveIdx);
    m_sel.setLastMove(info.move.from(), info.move.to());
    m_view.clearAllHighlights();
    m_sel.highlightLastMove();
    m_sfx.playEffect(info.sound);

    if (enteringFinalState)
    {
      m_view.setEvalResult(model::analysis::result_string(m_game.getResult(), m_game.getGameState().sideToMove, /*forPgn=*/false));
    }

    m_view.updateFen(m_fen_history[m_fen_index]);

    if (m_fen_index < m_time_history.size())
    {
      const model::analysis::TimeView &tv = m_time_history[m_fen_index];
      m_view.updateClock(core::Color::White, tv.white);
      m_view.updateClock(core::Color::Black, tv.black);
      const bool latest = atHead() && m_game.getResult() == core::GameResult::ONGOING;
      m_view.setClockActive(latest ? std::optional<core::Color>(tv.active) : std::nullopt);
    }

    syncCapturedPieces();

    if (atHead())
      m_view.restoreRightClickHighlights();
  }

  void HistorySystem::updateEvalAtHead()
  {
    if (m_fen_history.empty())
      return;
    if (!atHead())
      return;
    m_view.updateEval(m_eval_cp.load());
  }

  model::analysis::GameRecord HistorySystem::toRecord() const
  {
    model::analysis::GameRecord rec{};

    if (!m_fen_history.empty())
      rec.startFen = m_fen_history.front();

    if (!m_time_history.empty())
      rec.startTime = m_time_history.front();

    rec.plies.reserve(m_move_history.size());
    for (std::size_t i = 0; i < m_move_history.size(); ++i)
    {
      model::analysis::PlyRecord ply{};
      ply.move = m_move_history[i].move;

      // timeAfter is aligned with fen index i+1
      if (i + 1 < m_time_history.size())
        ply.timeAfter = m_time_history[i + 1];

      rec.plies.push_back(ply);
    }

    if (m_game.getResult() != core::GameResult::ONGOING)
      rec.result = model::analysis::result_string(m_game.getResult(), m_game.getGameState().sideToMove, /*forPgn=*/true);
    else
      rec.result = "*";

    return rec;
  }

  bool HistorySystem::loadFromRecord(const model::analysis::GameRecord &rec, bool populateMoveListWithSan)
  {
    const std::string startFen = rec.startFen.empty() ? core::START_FEN : rec.startFen;

    // IMPORTANT: expect GameView::init(startFen) was called by GameController before thisg.
    reset(startFen, rec.startTime);

    model::ChessGame scratch;
    scratch.setPosition(startFen);
    scratch.setResult(core::GameResult::ONGOING);

    // Position reference for SAN generation
    model::Position posBefore = scratch.getPositionRefForBot();

    // Build vectors without per-ply UI highlight churn
    for (std::size_t i = 0; i < rec.plies.size(); ++i)
    {
      const model::Move &mv = rec.plies[i].move;

      const core::Color moverColorBefore = scratch.getGameState().sideToMove;

      core::PieceType capturedType = core::PieceType::None;
      if (mv.isCapture())
      {
        if (mv.isEnPassant())
        {
          const core::Square to = mv.to();
          const core::Square epVictim = (moverColorBefore == core::Color::White)
                                            ? static_cast<core::Square>(to - 8)
                                            : static_cast<core::Square>(to + 8);
          capturedType = scratch.getPiece(epVictim).type;
        }
        else
        {
          capturedType = scratch.getPiece(mv.to()).type;
        }
      }

      // Move text (SAN or UCI)
      std::string moveText;
      if (populateMoveListWithSan)
        moveText = model::notation::toSan(posBefore, mv);
      if (moveText.empty())
        moveText = move_to_uci(mv);

      m_view.addMove(moveText);

      // Apply on scratch
      if (!scratch.doMove(mv.from(), mv.to(), mv.promotion()))
        return false;

      const std::string fenAfter = scratch.getFen();
      posBefore = scratch.getPositionRefForBot(); // now "before" for next ply

      const core::Color stmNow = scratch.getGameState().sideToMove;

      view::sound::Effect effect;
      if (scratch.isKingInCheck(stmNow))
        effect = view::sound::Effect::Check;
      else if (mv.promotion() != core::PieceType::None)
        effect = view::sound::Effect::Promotion;
      else if (mv.isCapture())
        effect = view::sound::Effect::Capture;
      else if (mv.castle() != model::CastleSide::None)
        effect = view::sound::Effect::Castle;
      else
        effect = view::sound::Effect::EnemyMove;

      MoveView mvInfo{mv, moverColorBefore, capturedType, effect, 0};

      m_move_history.push_back(mvInfo);
      m_fen_history.push_back(fenAfter);
      m_time_history.push_back(rec.plies[i].timeAfter);
    }

    ensureHeadVisibleForLivePlay();
    return true;
  }

} // namespace lilia::controller
