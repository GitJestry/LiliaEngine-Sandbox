#include "lilia/engine/search_position.hpp"
#include "lilia/chess/compiler.hpp"

namespace lilia::engine
{
  namespace
  {
    LILIA_ALWAYS_INLINE bool isCastleMove(const chess::Position &pos, const chess::Move &m, const chess::Piece &fromPiece)
    {
      if (LILIA_UNLIKELY(m.castle() != chess::CastleSide::None))
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

    LILIA_ALWAYS_INLINE bool isEnPassantMove(const chess::Position &pos, const chess::Move &m, const chess::Piece &fromPiece)
    {
      if (LILIA_UNLIKELY(m.isEnPassant()))
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
  }

  void SearchPosition::applyEvalDelta(const chess::Position &posBefore,
                                      const chess::Move &m,
                                      EvalAcc &eval)
  {
    const auto &board = posBefore.getBoard();
    const auto &st = posBefore.getState();

    const auto fromPieceOpt = board.getPiece(m.from());
    if (!fromPieceOpt)
      return;

    const chess::Piece fromPiece = *fromPieceOpt;
    const chess::Color us = st.sideToMove;
    const chess::Color them = ~us;

    const auto toPieceOpt = board.getPiece(m.to());

    const bool isCastle = m.isCastle() || isCastleMove(posBefore, m, fromPiece);

    bool isEP = m.isEnPassant();
    if (!isEP && fromPiece.type == chess::PieceType::Pawn)
    {
      const auto prevEP = st.enPassantSquare;
      if (prevEP != chess::NO_SQUARE && m.to() == prevEP && !toPieceOpt)
      {
        const int df = int(m.to()) - int(m.from());
        isEP = (df == 7 || df == 9 || df == -7 || df == -9);
      }
    }

    const bool isCap = isEP || (toPieceOpt && toPieceOpt->color == them);
    const chess::PieceType promo = m.promotion();

    if (promo != chess::PieceType::None)
    {
      eval.remove_piece(us, fromPiece.type, int(m.from()));

      if (isEP)
      {
        const chess::Square capSq =
            (us == chess::Color::White) ? chess::Square(int(m.to()) - 8)
                                        : chess::Square(int(m.to()) + 8);
        eval.remove_piece(them, chess::PieceType::Pawn, int(capSq));
      }
      else if (isCap)
      {
        eval.remove_piece(them, toPieceOpt->type, int(m.to()));
      }

      eval.add_piece(us, promo, int(m.to()));
    }
    else if (isEP)
    {
      const chess::Square capSq =
          (us == chess::Color::White) ? chess::Square(int(m.to()) - 8)
                                      : chess::Square(int(m.to()) + 8);

      eval.remove_piece(them, chess::PieceType::Pawn, int(capSq));
      eval.move_piece(us, chess::PieceType::Pawn, int(m.from()), int(m.to()));
    }
    else
    {
      if (isCap)
        eval.remove_piece(them, toPieceOpt->type, int(m.to()));

      eval.move_piece(us, fromPiece.type, int(m.from()), int(m.to()));
    }

    // Standard chess castling rook move.
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
}
