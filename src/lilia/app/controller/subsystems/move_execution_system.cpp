#include "lilia/app/controller/subsystems/move_execution_system.hpp"

#include <cmath>

#include "lilia/app/controller/subsystems/clock_system.hpp"
#include "lilia/app/controller/subsystems/history_system.hpp"
#include "lilia/app/controller/subsystems/legal_move_cache.hpp"
#include "lilia/app/controller/subsystems/premove_system.hpp"
#include "lilia/chess/chess_game.hpp"
#include "lilia/protocol/uci/uci_helper.hpp"

namespace lilia::app::controller
{

  MoveExecutionSystem::MoveExecutionSystem(view::ui::GameView &view, chess::ChessGame &game,
                                           view::audio::SoundManager &sfx,
                                           LegalMoveCache &legal, HistorySystem &history,
                                           ClockSystem &clock, PremoveSystem &premove)
      : m_view(view),
        m_game(game),
        m_sfx(sfx),
        m_legal(legal),
        m_history(history),
        m_clock(clock),
        m_premove(premove) {}

  void MoveExecutionSystem::applyMove(const chess::Move &move, bool isPlayerMove, bool onClick)
  {
    m_legal.invalidate();

    const chess::Square from = move.from();
    const chess::Square to = move.to();

    chess::Square epVictimSq = chess::NO_SQUARE;
    const chess::Color moverColorBefore = ~m_game.getGameState().sideToMove;
    if (move.isEnPassant())
    {
      epVictimSq = (moverColorBefore == chess::Color::White) ? static_cast<chess::Square>(to - 8)
                                                             : static_cast<chess::Square>(to + 8);
    }

    chess::PieceType capturedType = chess::PieceType::None;
    const bool skipAnim = m_premove.takeSkipAnimationFlag();
    const chess::PieceType capOverride = m_premove.takeCaptureOverride();

    if (move.isCapture())
    {
      if (capOverride != chess::PieceType::None)
      {
        capturedType = capOverride;
      }
      else
      {
        const chess::Square capSq = move.isEnPassant() ? epVictimSq : to;
        capturedType = m_view.getPieceType(capSq);
      }
    }

    if (skipAnim && move.isEnPassant() && epVictimSq != chess::NO_SQUARE)
    {
      m_view.removePiece(epVictimSq);
    }

    if (!skipAnim)
    {
      if (onClick)
        m_view.animationMovePiece(from, to, epVictimSq, move.promotion());
      else
        m_view.animationDropPiece(from, to, epVictimSq, move.promotion());
    }

    if (move.castle() != chess::CastleSide::None)
    {
      const chess::Square rookFrom =
          m_game.getRookSquareFromCastleside(move.castle(), moverColorBefore);
      const chess::Square rookTo = (move.castle() == chess::CastleSide::KingSide)
                                       ? static_cast<chess::Square>(to - 1)
                                       : static_cast<chess::Square>(to + 1);
      if (!skipAnim)
        m_view.animationMovePiece(rookFrom, rookTo);
    }

    const chess::Color sideToMoveNow = m_game.getGameState().sideToMove;

    view::audio::Effect effect;
    if (m_game.isKingInCheck(sideToMoveNow))
      effect = view::audio::Effect::Check;
    else if (move.promotion() != chess::PieceType::None)
      effect = view::audio::Effect::Promotion;
    else if (move.isCapture())
      effect = view::audio::Effect::Capture;
    else if (move.castle() != chess::CastleSide::None)
      effect = view::audio::Effect::Castle;
    else
      effect = isPlayerMove ? view::audio::Effect::PlayerMove : view::audio::Effect::EnemyMove;

    m_sfx.playEffect(effect);

    if (move.isCapture())
      m_view.addCapturedPiece(moverColorBefore, capturedType);

    if (m_clock.enabled())
    {
      m_clock.onMove(moverColorBefore);
      m_view.updateClock(chess::Color::White, m_clock.time(chess::Color::White));
      m_view.updateClock(chess::Color::Black, m_clock.time(chess::Color::Black));
      m_view.setClockActive(m_clock.active());
    }

    const domain::analysis::TimeView tv =
        m_clock.enabled() ? m_clock.snapshot(sideToMoveNow) : domain::analysis::TimeView{0.f, 0.f, sideToMoveNow};

    MoveView mvInfo{move, moverColorBefore, capturedType, effect};
    m_history.onMoveCommitted(mvInfo, m_game.getFen(), tv);

    m_premove.scheduleFromQueueIfTurnMatches();
  }

} // namespace lilia::controller
