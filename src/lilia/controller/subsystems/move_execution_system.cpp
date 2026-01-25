#include "lilia/controller/subsystems/move_execution_system.hpp"

#include <cmath>

#include "lilia/controller/subsystems/clock_system.hpp"
#include "lilia/controller/subsystems/history_system.hpp"
#include "lilia/controller/subsystems/legal_move_cache.hpp"
#include "lilia/controller/subsystems/premove_system.hpp"
#include "lilia/model/chess_game.hpp"
#include "lilia/uci/uci_helper.hpp"

namespace lilia::controller
{

  namespace
  {
    inline bool valid(core::Square sq)
    {
      return sq != core::NO_SQUARE;
    }
  } // namespace

  MoveExecutionSystem::MoveExecutionSystem(view::GameView &view, model::ChessGame &game,
                                           view::sound::SoundManager &sfx,
                                           LegalMoveCache &legal, HistorySystem &history,
                                           ClockSystem &clock, PremoveSystem &premove)
      : m_view(view),
        m_game(game),
        m_sfx(sfx),
        m_legal(legal),
        m_history(history),
        m_clock(clock),
        m_premove(premove) {}

  void MoveExecutionSystem::applyMove(const model::Move &move, bool isPlayerMove, bool onClick)
  {
    m_legal.invalidate();

    const core::Square from = move.from();
    const core::Square to = move.to();

    core::Square epVictimSq = core::NO_SQUARE;
    const core::Color moverColorBefore = ~m_game.getGameState().sideToMove;
    if (move.isEnPassant())
    {
      epVictimSq = (moverColorBefore == core::Color::White) ? static_cast<core::Square>(to - 8)
                                                            : static_cast<core::Square>(to + 8);
    }

    core::PieceType capturedType = core::PieceType::None;
    const bool skipAnim = m_premove.takeSkipAnimationFlag();
    const core::PieceType capOverride = m_premove.takeCaptureOverride();

    if (move.isCapture())
    {
      if (capOverride != core::PieceType::None)
      {
        capturedType = capOverride;
      }
      else
      {
        const core::Square capSq = move.isEnPassant() ? epVictimSq : to;
        capturedType = m_view.getPieceType(capSq);
      }
    }

    if (skipAnim && move.isEnPassant() && epVictimSq != core::NO_SQUARE)
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

    if (move.castle() != model::CastleSide::None)
    {
      const core::Square rookFrom =
          m_game.getRookSquareFromCastleside(move.castle(), moverColorBefore);
      const core::Square rookTo = (move.castle() == model::CastleSide::KingSide)
                                      ? static_cast<core::Square>(to - 1)
                                      : static_cast<core::Square>(to + 1);
      if (!skipAnim)
        m_view.animationMovePiece(rookFrom, rookTo);
    }

    const core::Color sideToMoveNow = m_game.getGameState().sideToMove;

    view::sound::Effect effect;
    if (m_game.isKingInCheck(sideToMoveNow))
      effect = view::sound::Effect::Check;
    else if (move.promotion() != core::PieceType::None)
      effect = view::sound::Effect::Promotion;
    else if (move.isCapture())
      effect = view::sound::Effect::Capture;
    else if (move.castle() != model::CastleSide::None)
      effect = view::sound::Effect::Castle;
    else
      effect = isPlayerMove ? view::sound::Effect::PlayerMove : view::sound::Effect::EnemyMove;

    m_sfx.playEffect(effect);

    if (move.isCapture())
      m_view.addCapturedPiece(moverColorBefore, capturedType);

    if (m_clock.enabled())
    {
      m_clock.onMove(moverColorBefore);
      m_view.updateClock(core::Color::White, m_clock.time(core::Color::White));
      m_view.updateClock(core::Color::Black, m_clock.time(core::Color::Black));
      m_view.setClockActive(m_clock.active());
    }

    const model::analysis::TimeView tv =
        m_clock.enabled() ? m_clock.snapshot(sideToMoveNow) : model::analysis::TimeView{0.f, 0.f, sideToMoveNow};

    MoveView mvInfo{move, moverColorBefore, capturedType, effect};
    m_history.onMoveCommitted(mvInfo, m_game.getFen(), tv);

    m_premove.scheduleFromQueueIfTurnMatches();
  }

} // namespace lilia::controller
