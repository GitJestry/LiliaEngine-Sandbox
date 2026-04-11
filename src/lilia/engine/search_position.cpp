#include "lilia/engine/search_position.hpp"
#include "lilia/chess/compiler.hpp"

namespace lilia::engine
{
  void SearchPosition::applyEvalDelta(const chess::StateInfo &st, EvalAcc &eval)
  {
    if (st.isNullMove())
      return;

    const chess::Move &m = st.move;
    const chess::Color us = st.moved.color;
    const chess::Color them = ~us;

    if (st.isPromotion())
    {
      eval.remove_piece(us, chess::PieceType::Pawn, int(m.from()));

      if (st.isCapture())
        eval.remove_piece(them, st.captured.type, int(st.capturedSquare));

      eval.add_piece(us, m.promotion(), int(m.to()));
    }
    else
    {
      if (st.isCapture())
        eval.remove_piece(them, st.captured.type, int(st.capturedSquare));

      eval.move_piece(us, st.moved.type, int(m.from()), int(m.to()));
    }

    if (st.isCastle())
      eval.move_piece(us, chess::PieceType::Rook, int(st.rookFrom), int(st.rookTo));
  }

  bool SearchPosition::doMove(const chess::Move &m)
  {
    if (LILIA_UNLIKELY(m_ply + 1 >= STACK_CAP))
      return false;

    StackEntry &next = m_stack[m_ply + 1];
    next.eval = m_stack[m_ply].eval;

    if (!m_pos.doMove(m, next.st))
      return false;

    applyEvalDelta(next.st, next.eval);
    ++m_ply;
    return true;
  }

  void SearchPosition::undoMove()
  {
    if (LILIA_UNLIKELY(m_ply <= 0))
      return;

    m_pos.undoMove();
    --m_ply;
  }

  bool SearchPosition::doNullMove()
  {
    if (LILIA_UNLIKELY(m_ply + 1 >= STACK_CAP))
      return false;

    StackEntry &next = m_stack[m_ply + 1];
    next.eval = m_stack[m_ply].eval;

    if (!m_pos.doNullMove(next.st))
      return false;

    ++m_ply;
    return true;
  }

  void SearchPosition::undoNullMove()
  {
    if (LILIA_UNLIKELY(m_ply <= 0))
      return;

    m_pos.undoNullMove();
    --m_ply;
  }
}
