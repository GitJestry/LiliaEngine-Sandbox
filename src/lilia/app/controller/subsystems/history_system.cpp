#include "lilia/app/controller/subsystems/history_system.hpp"

#include "lilia/app/controller/subsystems/premove_system.hpp"
#include "lilia/chess/chess_game.hpp"
#include "lilia/app/domain/game_record.hpp"
#include "lilia/app/domain/notation/san_notation.hpp"
#include "lilia/protocol/uci/uci_helper.hpp"
#include "lilia/app/domain/result_utils.hpp"
#include "lilia/chess/chess_constants.hpp"

namespace lilia::app::controller
{

  HistorySystem::HistorySystem(view::ui::GameView &view, chess::ChessGame &game, SelectionManager &sel,
                               view::audio::SoundManager &sfx)
      : m_view(view), m_game(game), m_sel(sel), m_sfx(sfx) {}

  void HistorySystem::reset(const std::string &startFen, const domain::analysis::TimeView &startTime)
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
      const domain::analysis::TimeView &tv = m_time_history[m_fen_index];
      m_view.updateClock(chess::Color::White, tv.white);
      m_view.updateClock(chess::Color::Black, tv.black);
      m_view.setClockActive(tv.active);
    }

    m_view.restoreRightClickHighlights();
    restoreSelectedPiece();
  }

  void HistorySystem::onMoveCommitted(const MoveView &mv, const std::string &fenAfter,
                                      const domain::analysis::TimeView &timeAfter)
  {
    // Build SAN from the *previous* position (current head FEN before push)
    std::string moveText;
    if (!m_fen_history.empty())
    {
      chess::ChessGame tmp;
      tmp.setPosition(m_fen_history.back());
      const chess::Position &pos = tmp.getPositionRefForBot();
      moveText = domain::notation::toSan(pos, mv.move);
    }

    if (moveText.empty())
      moveText = protocol::uci::move_to_uci(mv.move);

    m_view.addMove(moveText);

    // existing logic...
    m_move_history.push_back(mv);
    m_fen_history.push_back(fenAfter);
    m_time_history.push_back(timeAfter);

    m_fen_index = m_fen_history.size() - 1;

    m_view.updateFen(fenAfter);
    m_view.selectMove(m_fen_index ? m_fen_index - 1 : kInvalidMoveIdx);

    if (m_game.getResult() != chess::GameResult::Ongoing)
    {
      m_view.setEvalResult(domain::result_string(m_game.getResult(), m_game.getGameState().sideToMove, /*forPgn=*/false));
    }

    m_sel.dehoverSquare();
    if (m_sel.getSelectedSquare() != chess::NO_SQUARE)
      m_sel.deselectSquare();

    m_view.clearAttackHighlights();
    m_view.clearNonPremoveHighlights();

    m_sel.clearLastMoveHighlight();
    m_sel.setLastMove(mv.move.from(), mv.move.to());
    m_sel.highlightLastMove();
  }

  bool HistorySystem::handleMoveListClick(view::MousePos mp, PremoveSystem &premove)
  {
    const std::size_t idx = m_view.getMoveIndexAt(mp);
    if (idx == kInvalidMoveIdx)
      return false;

    premove.clearAll();

    const bool leavingFinalState = (m_game.getResult() != chess::GameResult::Ongoing && atHead() &&
                                    idx + 1 != m_fen_history.size() - 1);

    const bool enteringFinalState =
        (m_game.getResult() != chess::GameResult::Ongoing && idx + 1 == m_fen_history.size() - 1);

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
      m_view.setEvalResult(domain::result_string(m_game.getResult(), m_game.getGameState().sideToMove, /*forPgn=*/false));
    }

    if (m_fen_index < m_time_history.size())
    {
      const domain::analysis::TimeView &tv = m_time_history[m_fen_index];
      m_view.updateClock(chess::Color::White, tv.white);
      m_view.updateClock(chess::Color::Black, tv.black);
      const bool latest = atHead() && m_game.getResult() == chess::GameResult::Ongoing;
      m_view.setClockActive(latest ? std::optional<chess::Color>(tv.active) : std::nullopt);
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
    if (m_stashed_selected != chess::NO_SQUARE)
      m_sel.deselectSquare();
  }

  void HistorySystem::restoreSelectedPiece()
  {
    if (m_stashed_selected != chess::NO_SQUARE)
      m_sel.selectSquare(m_stashed_selected);
    m_stashed_selected = chess::NO_SQUARE;
  }

  void HistorySystem::stepBackward(PremoveSystem &premove)
  {
    premove.suspendVisualsIfAtHead(atHead());

    if (atHead())
      stashSelectedPiece();

    if (m_fen_index == 0)
      return;

    const bool leavingFinalState = (m_game.getResult() != chess::GameResult::Ongoing && atHead());

    if (atHead())
      m_view.stashRightClickHighlights();

    m_view.setBoardFen(m_fen_history[m_fen_index]);

    const MoveView &info = m_move_history[m_fen_index - 1];

    chess::Square epVictim = chess::NO_SQUARE;
    if (info.move.isEnPassant())
    {
      epVictim = (info.moverColor == chess::Color::White)
                     ? static_cast<chess::Square>(info.move.to() - 8)
                     : static_cast<chess::Square>(info.move.to() + 8);
    }

    m_view.animationMovePiece(
        info.move.to(), info.move.from(), chess::NO_SQUARE, chess::PieceType::None,
        [this, info, epVictim]()
        {
          if (info.move.isCapture())
          {
            const chess::Square capSq = info.move.isEnPassant() ? epVictim : info.move.to();
            m_view.addPiece(info.capturedType, ~info.moverColor, capSq);
          }
          if (info.move.promotion() != chess::PieceType::None)
          {
            m_view.removePiece(info.move.from());
            m_view.addPiece(chess::PieceType::Pawn, info.moverColor, info.move.from());
          }
        });

    if (info.move.castle() != chess::CastleSide::None)
    {
      const chess::Square rookFrom =
          m_game.getRookSquareFromCastleside(info.move.castle(), info.moverColor);
      const chess::Square rookTo = (info.move.castle() == chess::CastleSide::KingSide)
                                       ? static_cast<chess::Square>(info.move.to() - 1)
                                       : static_cast<chess::Square>(info.move.to() + 1);
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
      const domain::analysis::TimeView &tv = m_time_history[m_fen_index];
      m_view.updateClock(chess::Color::White, tv.white);
      m_view.updateClock(chess::Color::Black, tv.black);
      const bool latest = atHead() && m_game.getResult() == chess::GameResult::Ongoing;
      m_view.setClockActive(latest ? std::optional<chess::Color>(tv.active) : std::nullopt);
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

    const bool enteringFinalState = (m_game.getResult() != chess::GameResult::Ongoing &&
                                     m_fen_index + 1 == m_fen_history.size() - 1);

    m_view.setBoardFen(m_fen_history[m_fen_index]);

    const MoveView &info = m_move_history[m_fen_index];

    chess::Square epVictim = chess::NO_SQUARE;
    if (info.move.isEnPassant())
    {
      epVictim = (info.moverColor == chess::Color::White)
                     ? static_cast<chess::Square>(info.move.to() - 8)
                     : static_cast<chess::Square>(info.move.to() + 8);
      m_view.removePiece(epVictim);
    }
    else if (info.move.isCapture())
    {
      m_view.removePiece(info.move.to());
    }

    if (info.move.castle() != chess::CastleSide::None)
    {
      const chess::Square rookFrom =
          m_game.getRookSquareFromCastleside(info.move.castle(), info.moverColor);
      const chess::Square rookTo = (info.move.castle() == chess::CastleSide::KingSide)
                                       ? static_cast<chess::Square>(info.move.to() - 1)
                                       : static_cast<chess::Square>(info.move.to() + 1);
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
      m_view.setEvalResult(domain::result_string(m_game.getResult(), m_game.getGameState().sideToMove, /*forPgn=*/false));
    }

    m_view.updateFen(m_fen_history[m_fen_index]);

    if (m_fen_index < m_time_history.size())
    {
      const domain::analysis::TimeView &tv = m_time_history[m_fen_index];
      m_view.updateClock(chess::Color::White, tv.white);
      m_view.updateClock(chess::Color::Black, tv.black);
      const bool latest = atHead() && m_game.getResult() == chess::GameResult::Ongoing;
      m_view.setClockActive(latest ? std::optional<chess::Color>(tv.active) : std::nullopt);
    }

    syncCapturedPieces();

    if (atHead())
      m_view.restoreRightClickHighlights();
  }

  domain::GameRecord HistorySystem::toRecord() const
  {
    domain::GameRecord rec{};

    if (!m_fen_history.empty())
      rec.startFen = m_fen_history.front();

    if (!m_time_history.empty())
      rec.startTime = m_time_history.front();

    rec.plies.reserve(m_move_history.size());
    for (std::size_t i = 0; i < m_move_history.size(); ++i)
    {
      domain::PlyRecord ply{};
      ply.move = m_move_history[i].move;

      // timeAfter is aligned with fen index i+1
      if (i + 1 < m_time_history.size())
        ply.timeAfter = m_time_history[i + 1];

      rec.plies.push_back(ply);
    }

    if (m_game.getResult() != chess::GameResult::Ongoing)
      rec.result = domain::result_string(m_game.getResult(), m_game.getGameState().sideToMove, /*forPgn=*/true);
    else
      rec.result = "*";

    return rec;
  }

  bool HistorySystem::loadFromRecord(const domain::GameRecord &rec, bool populateMoveListWithSan)
  {
    const std::string startFen = rec.startFen.empty() ? std::string{chess::constant::START_FEN} : rec.startFen;

    // IMPORTANT: expect GameView::init(startFen) was called by GameController before thisg.
    reset(startFen, rec.startTime);

    chess::ChessGame scratch;
    scratch.setPosition(startFen);
    scratch.setResult(chess::GameResult::Ongoing);

    // Position reference for SAN generation
    chess::Position posBefore = scratch.getPositionRefForBot();

    // Build vectors without per-ply UI highlight churn
    for (std::size_t i = 0; i < rec.plies.size(); ++i)
    {
      const chess::Move &mv = rec.plies[i].move;

      const chess::Color moverColorBefore = scratch.getGameState().sideToMove;

      chess::PieceType capturedType = chess::PieceType::None;
      if (mv.isCapture())
      {
        if (mv.isEnPassant())
        {
          const chess::Square to = mv.to();
          const chess::Square epVictim = (moverColorBefore == chess::Color::White)
                                             ? static_cast<chess::Square>(to - 8)
                                             : static_cast<chess::Square>(to + 8);
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
        moveText = domain::notation::toSan(posBefore, mv);
      if (moveText.empty())
        moveText = protocol::uci::move_to_uci(mv);

      m_view.addMove(moveText);

      // Apply on scratch
      if (!scratch.doMove(mv.from(), mv.to(), mv.promotion()))
        return false;

      const std::string fenAfter = scratch.getFen();
      posBefore = scratch.getPositionRefForBot(); // now "before" for next ply

      const chess::Color stmNow = scratch.getGameState().sideToMove;

      view::audio::Effect effect;
      if (scratch.isKingInCheck(stmNow))
        effect = view::audio::Effect::Check;
      else if (mv.promotion() != chess::PieceType::None)
        effect = view::audio::Effect::Promotion;
      else if (mv.isCapture())
        effect = view::audio::Effect::Capture;
      else if (mv.castle() != chess::CastleSide::None)
        effect = view::audio::Effect::Castle;
      else
        effect = view::audio::Effect::EnemyMove;

      MoveView mvInfo{mv, moverColorBefore, capturedType, effect};

      m_move_history.push_back(mvInfo);
      m_fen_history.push_back(fenAfter);
      m_time_history.push_back(rec.plies[i].timeAfter);
    }

    ensureHeadVisibleForLivePlay();
    return true;
  }

} // namespace lilia::controller
