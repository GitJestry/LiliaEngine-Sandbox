#include "lilia/engine/search_position.hpp"

namespace lilia::engine
{
  namespace
  {
    bool isCastleMove(const chess::Position &pos, const chess::Move &m, const chess::Piece &fromPiece)
    {
      if (m.castle() != chess::CastleSide::None)
        return true;

      if (fromPiece.type != chess::PieceType::King)
        return false;

      const chess::Color us = pos.getState().sideToMove;
      if (us == chess::Color::White && m.from() == chess::bb::E1 &&
          (m.to() == chess::Square{6} || m.to() == chess::Square{2}))
        return true;
      if (us == chess::Color::Black && m.from() == chess::bb::E8 &&
          (m.to() == chess::Square{62} || m.to() == chess::Square{58}))
        return true;

      return false;
    }

    bool isEnPassantMove(const chess::Position &pos, const chess::Move &m, const chess::Piece &fromPiece)
    {
      if (m.isEnPassant())
        return true;

      if (fromPiece.type != chess::PieceType::Pawn)
        return false;

      const auto prevEP = pos.getState().enPassantSquare;
      if (prevEP == chess::NO_SQUARE || m.to() != prevEP)
        return false;

      const int df = int(m.to()) - int(m.from());
      const bool diag = (df == 7 || df == 9 || df == -7 || df == -9);
      if (!diag)
        return false;

      return !pos.getBoard().getPiece(m.to()).has_value();
    }
  } // namespace

  void SearchPosition::applyEvalDelta(const chess::Position &posBefore,
                                      const chess::Move &m,
                                      EvalAcc &eval)
  {
    const auto fromPieceOpt = posBefore.getBoard().getPiece(m.from());
    if (!fromPieceOpt)
      return;

    const chess::Piece fromPiece = *fromPieceOpt;
    const chess::Color us = posBefore.getState().sideToMove;
    const chess::Color them = ~us;

    const bool isCastle = isCastleMove(posBefore, m, fromPiece);
    const bool isEP = isEnPassantMove(posBefore, m, fromPiece);

    bool isCap = m.isCapture();
    if (!isCap && !isEP)
    {
      auto cap = posBefore.getBoard().getPiece(m.to());
      if (cap && cap->color == them)
        isCap = true;
    }

    // Promotion path
    if (m.promotion() != chess::PieceType::None)
    {
      eval.remove_piece(us, fromPiece.type, int(m.from()));

      if (isEP)
      {
        const chess::Square capSq = (us == chess::Color::White)
                                        ? chess::Square(int(m.to()) - 8)
                                        : chess::Square(int(m.to()) + 8);
        eval.remove_piece(them, chess::PieceType::Pawn, int(capSq));
      }
      else if (isCap)
      {
        if (auto cap = posBefore.getBoard().getPiece(m.to()))
          eval.remove_piece(them, cap->type, int(m.to()));
      }

      eval.add_piece(us, m.promotion(), int(m.to()));
    }
    else if (isEP)
    {
      const chess::Square capSq = (us == chess::Color::White)
                                      ? chess::Square(int(m.to()) - 8)
                                      : chess::Square(int(m.to()) + 8);

      eval.remove_piece(them, chess::PieceType::Pawn, int(capSq));
      eval.move_piece(us, chess::PieceType::Pawn, int(m.from()), int(m.to()));
    }
    else if (isCap)
    {
      if (auto cap = posBefore.getBoard().getPiece(m.to()))
        eval.remove_piece(them, cap->type, int(m.to()));

      eval.move_piece(us, fromPiece.type, int(m.from()), int(m.to()));
    }
    else
    {
      eval.move_piece(us, fromPiece.type, int(m.from()), int(m.to()));
    }

    // Castle rook move
    if (isCastle)
    {
      if (us == chess::Color::White)
      {
        if (m.to() == chess::Square{6} || m.castle() == chess::CastleSide::KingSide)
          eval.move_piece(us, chess::PieceType::Rook, int(chess::bb::H1), 5);
        else
          eval.move_piece(us, chess::PieceType::Rook, int(chess::bb::A1), 3);
      }
      else
      {
        if (m.to() == chess::Square{62} || m.castle() == chess::CastleSide::KingSide)
          eval.move_piece(us, chess::PieceType::Rook, int(chess::bb::H8), 61);
        else
          eval.move_piece(us, chess::PieceType::Rook, int(chess::bb::A8), 59);
      }
    }
  }

  bool SearchPosition::doMove(const chess::Move &m)
  {
    assert(m_evalTop < EVAL_STACK_CAP);
    m_evalStack[m_evalTop++] = m_eval;

    applyEvalDelta(m_pos, m, m_eval);

    if (!m_pos.doMove(m))
    {
      m_eval = m_evalStack[--m_evalTop];
      return false;
    }

    return true;
  }

  void SearchPosition::undoMove()
  {
    m_pos.undoMove();
    assert(m_evalTop > 0);
    m_eval = m_evalStack[--m_evalTop];
  }

  bool SearchPosition::doNullMove()
  {
    assert(m_evalTop < EVAL_STACK_CAP);
    m_evalStack[m_evalTop++] = m_eval;

    if (!m_pos.doNullMove())
    {
      --m_evalTop;
      return false;
    }

    // EvalAcc is white-POV material/PST state, so null move does not change it.
    return true;
  }

  void SearchPosition::undoNullMove()
  {
    m_pos.undoNullMove();
    assert(m_evalTop > 0);
    m_eval = m_evalStack[--m_evalTop];
  }
} // namespace lilia::engine
