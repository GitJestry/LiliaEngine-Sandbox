#include "lilia/chess/position.hpp"

#include <algorithm>
#include <array>

#include "lilia/chess/core/magic.hpp"
#include "lilia/chess/move_generator.hpp"
#include "lilia/chess/move_helper.hpp"

namespace lilia::chess
{

  namespace
  {
    constexpr std::array<std::uint8_t, SQ_NB> CR_CLEAR_FROM = []
    {
      std::array<std::uint8_t, SQ_NB> a{};
      a[bb::E1] |= CastlingRights::WhiteKingSide | CastlingRights::WhiteQueenSide;
      a[bb::E8] |= CastlingRights::BlackKingSide | CastlingRights::BlackQueenSide;
      a[bb::H1] |= CastlingRights::WhiteKingSide;
      a[bb::A1] |= CastlingRights::WhiteQueenSide;
      a[bb::H8] |= CastlingRights::BlackKingSide;
      a[bb::A8] |= CastlingRights::BlackQueenSide;
      return a;
    }();

    constexpr std::array<std::uint8_t, SQ_NB> CR_CLEAR_TO = CR_CLEAR_FROM;

    LILIA_ALWAYS_INLINE bool on_board_0_63(int s)
    {
      return static_cast<unsigned>(s) < 64u;
    }

    LILIA_ALWAYS_INLINE bool is_castle_move(Color us, const Piece &moved, const Move &m)
    {
      if (m.castle() != CastleSide::None)
        return true;

      if (moved.type != PieceType::King)
        return false;

      if (us == Color::White)
        return m.from() == bb::E1 && (m.to() == bb::G1 || m.to() == bb::C1);

      return m.from() == bb::E8 && (m.to() == bb::G8 || m.to() == bb::C8);
    }

    LILIA_ALWAYS_INLINE Square ep_capture_square(Color us, Square to)
    {
      return us == Color::White ? static_cast<Square>(int(to) - 8)
                                : static_cast<Square>(int(to) + 8);
    }

    LILIA_ALWAYS_INLINE bool is_en_passant_move(const Board &board,
                                                const GameState &st,
                                                const Piece &moved,
                                                const Move &m)
    {
      if (moved.type != PieceType::Pawn)
        return false;

      if (st.enPassantSquare == NO_SQUARE || m.to() != st.enPassantSquare)
        return false;

      const int df = int(m.to()) - int(m.from());
      const bool diag = (df == 7 || df == 9 || df == -7 || df == -9);
      if (!diag)
        return false;

      if (board.getPiece(m.to()).has_value())
        return false;

      const Square capSq = ep_capture_square(moved.color, m.to());
      const auto cap = board.getPiece(capSq);

      return cap && cap->color == ~moved.color && cap->type == PieceType::Pawn;
    }
  }

  bool Position::checkInsufficientMaterial()
  {
    const bb::Bitboard nonMinors =
        m_board.getPieces(Color::White, PieceType::Pawn) |
        m_board.getPieces(Color::Black, PieceType::Pawn) |
        m_board.getPieces(Color::White, PieceType::Rook) |
        m_board.getPieces(Color::Black, PieceType::Rook) |
        m_board.getPieces(Color::White, PieceType::Queen) |
        m_board.getPieces(Color::Black, PieceType::Queen);

    if (nonMinors)
      return false;

    const bb::Bitboard whiteB = m_board.getPieces(Color::White, PieceType::Bishop);
    const bb::Bitboard blackB = m_board.getPieces(Color::Black, PieceType::Bishop);
    const bb::Bitboard whiteN = m_board.getPieces(Color::White, PieceType::Knight);
    const bb::Bitboard blackN = m_board.getPieces(Color::Black, PieceType::Knight);

    const int wB = bb::popcount(whiteB);
    const int bB = bb::popcount(blackB);
    const int wN = bb::popcount(whiteN);
    const int bN = bb::popcount(blackN);

    const int totalB = wB + bB;
    const int totalN = wN + bN;
    const int totalMinors = totalB + totalN;

    if (totalMinors <= 1)
      return true;

    if (totalB == 0)
      return totalN <= 2;

    auto is_dark = [](Square s) -> bool
    {
      return ((int(s) ^ (int(s) >> 3)) & 1) != 0;
    };

    auto has_bishops_on_both_colors = [&](bb::Bitboard bishops) -> bool
    {
      bool anyDark = false, anyLight = false;
      while (bishops)
      {
        const Square s = bb::pop_lsb(bishops);
        if (is_dark(s))
          anyDark = true;
        else
          anyLight = true;

        if (anyDark && anyLight)
          return true;
      }
      return false;
    };

    if (totalN == 0)
      return !has_bishops_on_both_colors(whiteB) &&
             !has_bishops_on_both_colors(blackB);

    if ((wB == 1 && wN == 0 && bB == 0 && bN == 1) ||
        (wB == 0 && wN == 1 && bB == 1 && bN == 0))
      return true;

    return false;
  }

  bool Position::checkMoveRule()
  {
    return m_st->halfmoveClock >= 100;
  }

  bool Position::checkRepetition()
  {
    int count = 0;
    const int limit = std::min<int>(m_st->halfmoveClock, m_st->pliesFromNull);

    const StateInfo *p = m_st;
    for (int back = 2; back <= limit; back += 2)
    {
      const StateInfo *p1 = p->previous;
      if (!p1 || p1->isNullMove())
        break;

      const StateInfo *p2 = p1->previous;
      if (!p2 || p2->isNullMove())
        break;

      p = p2;

      if (p->zobristKey == m_st->zobristKey && ++count >= 2)
        return true;
    }

    return false;
  }

  bool Position::inCheck() const
  {
    const bb::Bitboard kbb = m_board.getPieces(m_st->sideToMove, PieceType::King);
    if (!kbb)
      return false;

    const Square ksq = static_cast<Square>(bb::ctz64(kbb));
    return attackedBy(m_board, ksq, ~m_st->sideToMove, m_board.getAllPieces());
  }

  bool Position::isPseudoLegal(const Move &m) const
  {
    if (LILIA_UNLIKELY(!on_board_0_63(m.from()) || !on_board_0_63(m.to()) || m.from() == m.to()))
      return false;

    const auto fromP = m_board.getPiece(m.from());
    if (LILIA_UNLIKELY(!fromP || fromP->color != m_st->sideToMove))
      return false;

    const auto toP = m_board.getPiece(m.to());
    const Color us = fromP->color, them = ~us;
    const bool isCap = (m.isEnPassant() ? true : (toP && toP->color == them));
    const bb::Bitboard occ = m_board.getAllPieces();

    using PT = PieceType;
    switch (fromP->type)
    {
    case PT::Pawn:
    {
      const int from = int(m.from());
      const int to = int(m.to());
      const int df = to - from;
      const int fromRank = bb::rank_of(m.from());
      const int toRank = bb::rank_of(m.to());
      const int fileDelta = (to & 7) - (from & 7);
      const int rankDelta = toRank - fromRank;
      const bool white = (us == Color::White);
      const bool toLastRank = white ? (toRank == 7) : (toRank == 0);

      if (m.promotion() != PT::None)
      {
        if (!toLastRank)
          return false;
        if (!(m.promotion() == PT::Knight || m.promotion() == PT::Bishop ||
              m.promotion() == PT::Rook || m.promotion() == PT::Queen))
          return false;
      }
      else if (toLastRank)
      {
        return false;
      }

      if (!isCap)
      {
        if (white)
        {
          if (df != 8 && !(df == 16 && fromRank == 1))
            return false;
          if (occ & bb::sq_bb(static_cast<Square>(from + 8)))
            return false;
          if (df == 16 && (occ & bb::sq_bb(static_cast<Square>(from + 16))))
            return false;
        }
        else
        {
          if (df != -8 && !(df == -16 && fromRank == 6))
            return false;
          if (occ & bb::sq_bb(static_cast<Square>(from - 8)))
            return false;
          if (df == -16 && (occ & bb::sq_bb(static_cast<Square>(from - 16))))
            return false;
        }
        return true;
      }

      const bool diagOne =
          (rankDelta == (white ? 1 : -1)) &&
          (fileDelta == -1 || fileDelta == 1);

      if (!diagOne)
        return false;

      if (m.isEnPassant())
      {
        if (m_st->enPassantSquare != m.to())
          return false;

        const Square capSq =
            white ? static_cast<Square>(to - 8) : static_cast<Square>(to + 8);
        const auto capP = m_board.getPiece(capSq);
        return capP && capP->color == them && capP->type == PT::Pawn;
      }

      return toP && toP->color == them;
    }

    case PT::Knight:
    {
      const bb::Bitboard atk = bb::knight_attacks_from(m.from());
      return (atk & bb::sq_bb(m.to())) && (!toP || toP->color == them);
    }

    case PT::Bishop:
    {
      const bb::Bitboard ray = magic::sliding_attacks(magic::Slider::Bishop, m.from(), occ);
      return (ray & bb::sq_bb(m.to())) && (!toP || toP->color == them);
    }

    case PT::Rook:
    {
      const bb::Bitboard ray = magic::sliding_attacks(magic::Slider::Rook, m.from(), occ);
      return (ray & bb::sq_bb(m.to())) && (!toP || toP->color == them);
    }

    case PT::Queen:
    {
      const bb::Bitboard ray = magic::sliding_attacks(magic::Slider::Bishop, m.from(), occ) |
                               magic::sliding_attacks(magic::Slider::Rook, m.from(), occ);
      return (ray & bb::sq_bb(m.to())) && (!toP || toP->color == them);
    }

    case PT::King:
    {
      if (bb::king_attacks_from(m.from()) & bb::sq_bb(m.to()))
        return (!toP || toP->color == them);

      auto rook_ok = [&](Square sq) -> bool
      {
        const auto rp = m_board.getPiece(sq);
        return rp && rp->color == us && rp->type == PT::Rook;
      };

      if (us == Color::White)
      {
        if ((m_st->castlingRights & CastlingRights::WhiteKingSide) &&
            m.from() == bb::E1 && m.to() == bb::G1)
        {
          if (rook_ok(bb::H1) &&
              (occ & (bb::sq_bb(bb::F1) | bb::sq_bb(bb::G1))) == 0 &&
              !attackedBy(m_board, bb::E1, them, occ) &&
              !attackedBy(m_board, bb::F1, them, occ) &&
              !attackedBy(m_board, bb::G1, them, occ))
            return true;
        }

        if ((m_st->castlingRights & CastlingRights::WhiteQueenSide) &&
            m.from() == bb::E1 && m.to() == bb::C1)
        {
          if (rook_ok(bb::A1) &&
              (occ & (bb::sq_bb(bb::B1) | bb::sq_bb(bb::C1) | bb::sq_bb(bb::D1))) == 0 &&
              !attackedBy(m_board, bb::E1, them, occ) &&
              !attackedBy(m_board, bb::D1, them, occ) &&
              !attackedBy(m_board, bb::C1, them, occ))
            return true;
        }
      }
      else
      {
        if ((m_st->castlingRights & CastlingRights::BlackKingSide) &&
            m.from() == bb::E8 && m.to() == bb::G8)
        {
          if (rook_ok(bb::H8) &&
              (occ & (bb::sq_bb(bb::F8) | bb::sq_bb(bb::G8))) == 0 &&
              !attackedBy(m_board, bb::E8, them, occ) &&
              !attackedBy(m_board, bb::F8, them, occ) &&
              !attackedBy(m_board, bb::G8, them, occ))
            return true;
        }

        if ((m_st->castlingRights & CastlingRights::BlackQueenSide) &&
            m.from() == bb::E8 && m.to() == bb::C8)
        {
          if (rook_ok(bb::A8) &&
              (occ & (bb::sq_bb(bb::B8) | bb::sq_bb(bb::C8) | bb::sq_bb(bb::D8))) == 0 &&
              !attackedBy(m_board, bb::E8, them, occ) &&
              !attackedBy(m_board, bb::D8, them, occ) &&
              !attackedBy(m_board, bb::C8, them, occ))
            return true;
        }
      }

      return false;
    }

    default:
      break;
    }

    return false;
  }

  bool Position::doMove(const Move &m, StateInfo &newState)
  {
    if (LILIA_UNLIKELY(&newState == m_st))
      return false;

    if (LILIA_UNLIKELY(!isPseudoLegal(m)))
      return false;

    const auto fromPiece = m_board.getPiece(m.from());
    if (LILIA_UNLIKELY(!fromPiece || fromPiece->color != m_st->sideToMove))
      return false;

    newState = *m_st;
    newState.previous = m_st;
    newState.move = m;
    newState.moved = *fromPiece;
    newState.captured = Piece{PieceType::None, ~fromPiece->color};
    newState.capturedSquare = NO_SQUARE;
    newState.rookFrom = NO_SQUARE;
    newState.rookTo = NO_SQUARE;
    newState.gaveCheck = 0;
    newState.flags = StateNone;
    newState.pliesFromNull =
        static_cast<std::uint16_t>(m_st->isNullMove() ? 1 : (m_st->pliesFromNull + 1));

    applyMove(m, newState);

    const Color movedSide = newState.moved.color;
    const bb::Bitboard kbb = m_board.getPieces(movedSide, PieceType::King);
    if (!kbb)
    {
      unapplyMove(newState);
      return false;
    }

    const Square ksq = static_cast<Square>(bb::ctz64(kbb));
    if (attackedBy(m_board, ksq, newState.sideToMove, m_board.getAllPieces()))
    {
      unapplyMove(newState);
      return false;
    }

    m_st = &newState;
    return true;
  }

  void Position::undoMove()
  {
    if (!m_st->previous)
      return;

    const StateInfo *cur = m_st;
    unapplyMove(*cur);
    m_st = cur->previous;
  }

  bool Position::doNullMove(StateInfo &newState)
  {
    if (LILIA_UNLIKELY(&newState == m_st))
      return false;

    if (LILIA_UNLIKELY(inCheck()))
      return false;

    newState = *m_st;
    newState.previous = m_st;
    newState.move = Move{};
    newState.moved = Piece{PieceType::None, m_st->sideToMove};
    newState.captured = Piece{PieceType::None, ~m_st->sideToMove};
    newState.capturedSquare = NO_SQUARE;
    newState.rookFrom = NO_SQUARE;
    newState.rookTo = NO_SQUARE;
    newState.gaveCheck = 0;
    newState.flags = StateNullMove;
    newState.pliesFromNull = 0;

    xorEPRelevant(newState);
    newState.enPassantSquare = NO_SQUARE;

    ++newState.halfmoveClock;

    hashXorSide(newState);
    newState.sideToMove = ~newState.sideToMove;
    if (newState.sideToMove == Color::White)
      ++newState.fullmoveNumber;

    m_st = &newState;
    return true;
  }

  void Position::undoNullMove()
  {
    if (!m_st->previous || !m_st->isNullMove())
      return;

    m_st = m_st->previous;
  }

  void Position::applyMove(const Move &m, StateInfo &st)
  {
    const Color us = st.moved.color;
    const Color them = ~us;

    xorEPRelevant(st);
    st.enPassantSquare = NO_SQUARE;

    const bool movingPawn = (st.moved.type == PieceType::Pawn);
    const bool isCastle = is_castle_move(us, st.moved, m);
    const bool isEP = is_en_passant_move(m_board, *st.previous, st.moved, m);

    if (isCastle)
      st.flags |= StateCastle;
    if (isEP)
      st.flags |= StateEnPassant;

    bool isCapture = false;

    if (isEP)
    {
      st.capturedSquare = ep_capture_square(us, m.to());
      const auto cap = m_board.getPiece(st.capturedSquare);
      st.captured = cap.value_or(Piece{PieceType::Pawn, them});
      st.captured.type = PieceType::Pawn;
      st.flags |= StateCapture;
      isCapture = true;
    }
    else if (const auto cap = m_board.getPiece(m.to()); cap && cap->color == them)
    {
      st.captured = *cap;
      st.capturedSquare = m.to();
      st.flags |= StateCapture;
      isCapture = true;
    }

    if (m.promotion() != PieceType::None)
      st.flags |= StatePromotion;

    if (isCastle)
    {
      if (us == Color::White)
      {
        if (m.to() == bb::G1 || m.castle() == CastleSide::KingSide)
        {
          st.rookFrom = bb::H1;
          st.rookTo = bb::F1;
        }
        else
        {
          st.rookFrom = bb::A1;
          st.rookTo = bb::D1;
        }
      }
      else
      {
        if (m.to() == bb::G8 || m.castle() == CastleSide::KingSide)
        {
          st.rookFrom = bb::H8;
          st.rookTo = bb::F8;
        }
        else
        {
          st.rookFrom = bb::A8;
          st.rookTo = bb::D8;
        }
      }
    }

    Piece placed = st.moved;
    const bool quiet = !isCapture && !isCastle && !st.isPromotion();
    const bool normalCap = isCapture && !isEP && !isCastle && !st.isPromotion();
    const bool fastEP = isEP && !st.isPromotion();

    if (LILIA_LIKELY(quiet))
    {
      hashXorPiece(st, us, placed.type, m.from());
      m_board.movePiece_noCapture(m.from(), m.to());
      hashXorPiece(st, us, placed.type, m.to());
    }
    else if (normalCap)
    {
      hashXorPiece(st, them, st.captured.type, m.to());
      hashXorPiece(st, us, placed.type, m.from());
      m_board.movePiece_withCapture(m.from(), m.to(), m.to(), st.captured);
      hashXorPiece(st, us, placed.type, m.to());
    }
    else if (LILIA_UNLIKELY(fastEP))
    {
      hashXorPiece(st, them, PieceType::Pawn, st.capturedSquare);
      hashXorPiece(st, us, PieceType::Pawn, m.from());
      m_board.movePiece_withCapture(m.from(), st.capturedSquare, m.to(), Piece{PieceType::Pawn, them});
      hashXorPiece(st, us, PieceType::Pawn, m.to());
    }
    else
    {
      hashXorPiece(st, us, placed.type, m.from());
      m_board.removePiece(m.from());

      if (st.isPromotion())
        placed.type = m.promotion();

      if (isCapture)
      {
        hashXorPiece(st, them, st.captured.type, st.capturedSquare);
        m_board.removePiece(st.capturedSquare);
      }

      hashXorPiece(st, us, placed.type, m.to());
      m_board.setPiece(m.to(), placed);
    }

    if (isCastle)
    {
      hashXorPiece(st, us, PieceType::Rook, st.rookFrom);
      m_board.movePiece_noCapture(st.rookFrom, st.rookTo);
      hashXorPiece(st, us, PieceType::Rook, st.rookTo);
    }

    if (movingPawn || isCapture)
      st.halfmoveClock = 0;
    else
      ++st.halfmoveClock;

    const std::uint8_t prevCR = st.castlingRights;
    st.castlingRights &= ~(CR_CLEAR_FROM[int(m.from())] | CR_CLEAR_TO[int(m.to())]);
    if (prevCR != st.castlingRights)
      hashSetCastling(st, prevCR, st.castlingRights);

    if (movingPawn)
    {
      Square ep = NO_SQUARE;
      if (us == Color::White && bb::rank_of(m.from()) == 1 && bb::rank_of(m.to()) == 3)
        ep = static_cast<Square>(int(m.from()) + 8);
      else if (us == Color::Black && bb::rank_of(m.from()) == 6 && bb::rank_of(m.to()) == 4)
        ep = static_cast<Square>(int(m.from()) - 8);

      if (ep != NO_SQUARE)
      {
        const bb::Bitboard enemyPawns = m_board.getPieces(them, PieceType::Pawn);
        if (enemyPawns & Zobrist::epCaptureMask[bb::ci(them)][int(ep)])
          st.enPassantSquare = ep;
      }
    }

    const bb::Bitboard kThem = m_board.getPieces(them, PieceType::King);
    st.gaveCheck = 0;
    if (kThem)
    {
      const Square ksqThem = static_cast<Square>(bb::ctz64(kThem));
      if (attackedBy(m_board, ksqThem, us, m_board.getAllPieces()))
        st.gaveCheck = 1;
    }

    hashXorSide(st);
    st.sideToMove = them;
    if (them == Color::White)
      ++st.fullmoveNumber;

    xorEPRelevant(st);
  }

  void Position::unapplyMove(const StateInfo &st)
  {
    if (st.isNullMove())
      return;

    const Move &m = st.move;
    const Color us = st.moved.color;

    if (st.isCastle())
      m_board.movePiece_noCapture(st.rookTo, st.rookFrom);

    if (st.isPromotion())
    {
      m_board.removePiece(m.to());
      m_board.setPiece(m.from(), st.moved);
    }
    else
    {
      m_board.movePiece_noCapture(m.to(), m.from());
    }

    if (st.isCapture())
      m_board.setPiece(st.capturedSquare, st.captured);
  }

}
