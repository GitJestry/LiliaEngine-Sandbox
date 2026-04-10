#include "lilia/chess/position.hpp"

#include <algorithm>
#include <array>
#include <limits>

#include "lilia/chess/core/magic.hpp"
#include "lilia/chess/move_generator.hpp"
#include "lilia/chess/move_helper.hpp"

namespace lilia::chess
{

  namespace
  {

    // --------- Castling-Right Clear-Mask (FROM/TO) ----------
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
      return (unsigned)s < 64u;
    }

  }

  bool Position::checkInsufficientMaterial()
  {
    // Any pawn, rook, or queen means mating material exists in principle.
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

    // KK, KBK, KNK
    if (totalMinors <= 1)
      return true;

    // Knight-only cases that are still dead.
    // KNNvK and KNvKN are dead positions.
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

    // Bishops only: dead unless one side owns bishops on both color complexes.
    if (totalN == 0)
      return !has_bishops_on_both_colors(whiteB) &&
             !has_bishops_on_both_colors(blackB);

    // KB vs KN is dead: neither side has mating material.
    if ((wB == 1 && wN == 0 && bB == 0 && bN == 1) ||
        (wB == 0 && wN == 1 && bB == 1 && bN == 0))
      return true;

    // Otherwise, do not claim insufficient material.
    return false;
  }

  bool Position::checkMoveRule()
  {
    return (m_state.halfmoveClock >= 100);
  }

  bool Position::checkRepetition()
  {
    int count = 0;
    const int n = (int)m_history.size();
    const int lim = std::min<int>(n, m_state.halfmoveClock);
    for (int back = 2; back <= lim; back += 2)
    {
      const int idx = n - back;
      if (idx < 0)
        break;
      if (m_history[idx].zobristKey == m_hash && ++count >= 2)
        return true;
    }
    return false;
  }

  bool Position::inCheck() const
  {
    const bb::Bitboard kbb = m_board.getPieces(m_state.sideToMove, PieceType::King);
    if (!kbb)
      return false;
    const Square ksq = static_cast<Square>(bb::ctz64(kbb));
    return attackedBy(m_board, ksq, ~m_state.sideToMove, m_board.getAllPieces());
  }

  bool Position::isPseudoLegal(const Move &m) const
  {
    if (LILIA_UNLIKELY(!on_board_0_63(m.from()) || !on_board_0_63(m.to()) || m.from() == m.to()))
      return false;

    const auto fromP = m_board.getPiece(m.from());
    if (LILIA_UNLIKELY(!fromP || fromP->color != m_state.sideToMove))
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

      // Promotion is mandatory on the back rank, and only legal there.
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

      // Quiet forward
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

      // Captures (normal or EP): exactly one file over, exactly one rank forward.
      const bool diagOne =
          (rankDelta == (white ? 1 : -1)) &&
          (fileDelta == -1 || fileDelta == 1);

      if (!diagOne)
        return false;

      if (m.isEnPassant())
      {
        if (m_state.enPassantSquare != m.to())
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
      // Normal king step
      if (bb::king_attacks_from(m.from()) & bb::sq_bb(m.to()))
        return (!toP || toP->color == them);

      auto rook_ok = [&](Square sq) -> bool
      {
        const auto rp = m_board.getPiece(sq);
        return rp && rp->color == us && rp->type == PT::Rook;
      };

      // Castling (rare -> full pseudo-legality is fine)
      if (us == Color::White)
      {
        if ((m_state.castlingRights & CastlingRights::WhiteKingSide) &&
            m.from() == bb::E1 && m.to() == bb::G1)
        {
          if (rook_ok(bb::H1) &&
              (occ & (bb::sq_bb(bb::F1) | bb::sq_bb(bb::G1))) == 0 &&
              !attackedBy(m_board, bb::E1, them, occ) &&
              !attackedBy(m_board, bb::F1, them, occ) &&
              !attackedBy(m_board, bb::G1, them, occ))
            return true;
        }

        if ((m_state.castlingRights & CastlingRights::WhiteQueenSide) &&
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
        if ((m_state.castlingRights & CastlingRights::BlackKingSide) &&
            m.from() == bb::E8 && m.to() == bb::G8)
        {
          if (rook_ok(bb::H8) &&
              (occ & (bb::sq_bb(bb::F8) | bb::sq_bb(bb::G8))) == 0 &&
              !attackedBy(m_board, bb::E8, them, occ) &&
              !attackedBy(m_board, bb::F8, them, occ) &&
              !attackedBy(m_board, bb::G8, them, occ))
            return true;
        }

        if ((m_state.castlingRights & CastlingRights::BlackQueenSide) &&
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

  bool Position::doMove(const Move &m)
  {
    if (LILIA_UNLIKELY(!isPseudoLegal(m)))
      return false;

    Color us = m_state.sideToMove;
    auto fromPiece = m_board.getPiece(m.from());
    if (LILIA_UNLIKELY(!fromPiece || fromPiece->color != us))
      return false;

    StateInfo st{};
    st.move = m;
    st.zobristKey = m_hash;
    st.prevCastlingRights = m_state.castlingRights;
    st.prevEnPassantSquare = m_state.enPassantSquare;
    st.prevHalfmoveClock = m_state.halfmoveClock;
    st.prevPawnKey = m_state.pawnKey;

    applyMove(m, st);

    // Illegal moves king
    Color movedSide = ~m_state.sideToMove;
    const bb::Bitboard kbbAfter = m_board.getPieces(movedSide, PieceType::King);
    if (!kbbAfter)
    {
      unapplyMove(st);
      m_hash = st.zobristKey;
      m_state.pawnKey = st.prevPawnKey;
      return false;
    }
    const Square ksqAfter = static_cast<Square>(bb::ctz64(kbbAfter));
    if (attackedBy(m_board, ksqAfter, m_state.sideToMove, m_board.getAllPieces()))
    {
      unapplyMove(st);
      m_hash = st.zobristKey;
      m_state.pawnKey = st.prevPawnKey;
      return false;
    }

    m_history.push_back(st);
    return true;
  }

  void Position::undoMove()
  {
    if (m_history.empty())
      return;

    const StateInfo &st = m_history.back();
    unapplyMove(st);
    m_hash = st.zobristKey;
    m_state.pawnKey = st.prevPawnKey;
    m_history.pop_back();
  }

  bool Position::doNullMove()
  {
    NullState st{};
    st.zobristKey = m_hash;
    st.prevCastlingRights = m_state.castlingRights;
    st.prevEnPassantSquare = m_state.enPassantSquare;
    st.prevHalfmoveClock = m_state.halfmoveClock;
    st.prevFullmoveNumber = m_state.fullmoveNumber;

    xorEPRelevant();
    m_state.enPassantSquare = NO_SQUARE;

    ++m_state.halfmoveClock;

    hashXorSide();
    m_state.sideToMove = ~m_state.sideToMove;
    if (m_state.sideToMove == Color::White)
      ++m_state.fullmoveNumber;

    m_null_history.push_back(st);
    return true;
  }

  void Position::undoNullMove()
  {
    if (m_null_history.empty())
      return;

    const NullState st = m_null_history.back();
    m_null_history.pop_back();

    m_state.sideToMove = ~m_state.sideToMove;
    m_state.fullmoveNumber = st.prevFullmoveNumber;
    m_state.castlingRights = st.prevCastlingRights;
    m_state.enPassantSquare = st.prevEnPassantSquare;
    m_state.halfmoveClock = st.prevHalfmoveClock;

    m_hash = st.zobristKey;
  }

  void Position::applyMove(const Move &m, StateInfo &st)
  {
    Color us = m_state.sideToMove;
    Color them = ~us;

    // EP out of hash; clear EP
    xorEPRelevant();
    const Square prevEP = m_state.enPassantSquare;
    m_state.enPassantSquare = NO_SQUARE;

    const auto fromPiece = m_board.getPiece(m.from());
    if (!fromPiece)
      return;
    const bool movingPawn = (fromPiece->type == PieceType::Pawn);

    // Detect castling
    bool isCastleMove = (m.castle() != CastleSide::None);
    if (LILIA_UNLIKELY(!isCastleMove && fromPiece->type == PieceType::King))
    {
      if (us == Color::White && m.from() == bb::E1 &&
          (m.to() == bb::G1 || m.to() == bb::C1))
        isCastleMove = true;
      if (us == Color::Black && m.from() == bb::E8 &&
          (m.to() == bb::G8 || m.to() == bb::C8))
        isCastleMove = true;
    }

    // Detect en passant robustly
    bool isEP = m.isEnPassant();
    if (!isEP && movingPawn)
    {
      const int df = (int)m.to() - (int)m.from();
      const bool diag = (df == 7 || df == 9 || df == -7 || df == -9);
      if (diag && prevEP != NO_SQUARE && m.to() == prevEP)
      {
        if (!m_board.getPiece(m.to()).has_value())
          isEP = true;
      }
    }

    // Detect capture
    bool isCap = m.isCapture();
    if (!isCap && !isEP)
    {
      auto cap = m_board.getPiece(m.to());
      if (cap && cap->color == them)
        isCap = true;
    }

    // Determine captured piece and store in state
    if (isEP)
    {
      const Square capSq = (us == Color::White) ? static_cast<Square>(m.to() - 8)
                                                : static_cast<Square>(m.to() + 8);
      auto cap = m_board.getPiece(capSq);
      st.captured = cap.value_or(Piece{PieceType::Pawn, them});
      st.captured.type = PieceType::Pawn;
    }
    else if (isCap)
    {
      auto cap = m_board.getPiece(m.to());
      st.captured = cap.value_or(Piece{PieceType::None, them});
    }
    else
    {
      st.captured = Piece{PieceType::None, them};
    }

    Piece placed = *fromPiece;
    const bool fastQuiet =
        (!isCap && !isEP && !isCastleMove && m.promotion() == PieceType::None);
    const bool fastCap = (isCap && !isEP && !isCastleMove && m.promotion() == PieceType::None &&
                          st.captured.type != PieceType::None);
    const bool fastEP = (isEP && m.promotion() == PieceType::None);

    if (LILIA_LIKELY(fastQuiet))
    {
      hashXorPiece(us, placed.type, m.from());
      m_board.movePiece_noCapture(m.from(), m.to());
      hashXorPiece(us, placed.type, m.to());
    }
    else if (fastCap)
    {

      hashXorPiece(them, st.captured.type, m.to());
      hashXorPiece(us, placed.type, m.from());
      m_board.movePiece_withCapture(m.from(), m.to(), m.to(), st.captured);
      hashXorPiece(us, placed.type, m.to());
    }
    else if (LILIA_UNLIKELY(fastEP))
    {
      const Square capSq = (us == Color::White) ? static_cast<Square>(m.to() - 8)
                                                : static_cast<Square>(m.to() + 8);
      hashXorPiece(them, PieceType::Pawn, capSq);
      hashXorPiece(us, PieceType::Pawn, m.from());
      m_board.movePiece_withCapture(m.from(), capSq, m.to(), Piece{PieceType::Pawn, them});
      hashXorPiece(us, PieceType::Pawn, m.to());
    }
    else
    {
      hashXorPiece(us, placed.type, m.from());
      m_board.removePiece(m.from());

      if (m.promotion() != PieceType::None)
        placed.type = m.promotion();

      if (isEP)
      {
        const Square capSq = (us == Color::White) ? static_cast<Square>(m.to() - 8)
                                                  : static_cast<Square>(m.to() + 8);
        hashXorPiece(them, PieceType::Pawn, capSq);
        m_board.removePiece(capSq);
      }
      else if (isCap && st.captured.type != PieceType::None)
      {
        hashXorPiece(them, st.captured.type, m.to());
        m_board.removePiece(m.to());
      }
      hashXorPiece(us, placed.type, m.to());
      m_board.setPiece(m.to(), placed);
    }

    // Castle rook move
    if (isCastleMove)
    {
      if (us == Color::White)
      {
        if (m.to() == bb::G1 || m.castle() == CastleSide::KingSide)
        {
          hashXorPiece(us, PieceType::Rook, bb::H1);
          m_board.movePiece_noCapture(bb::H1, bb::F1);
          hashXorPiece(us, PieceType::Rook, bb::F1);
        }
        else
        {
          hashXorPiece(us, PieceType::Rook, bb::A1);
          m_board.movePiece_noCapture(bb::A1, bb::D1);
          hashXorPiece(us, PieceType::Rook, bb::D1);
        }
      }
      else
      {
        if (m.to() == bb::G8 || m.castle() == CastleSide::KingSide)
        {
          hashXorPiece(us, PieceType::Rook, bb::H8);
          m_board.movePiece_noCapture(bb::H8, bb::F8);
          hashXorPiece(us, PieceType::Rook, bb::F8);
        }
        else
        {
          hashXorPiece(us, PieceType::Rook, bb::A8);
          m_board.movePiece_noCapture(bb::A8, bb::D8);
          hashXorPiece(us, PieceType::Rook, bb::D8);
        }
      }
    }

    // gaveCheck
    const bb::Bitboard kThem = m_board.getPieces(them, PieceType::King);
    std::uint8_t gc = 0;
    if (kThem)
    {
      const Square ksqThem = static_cast<Square>(bb::ctz64(kThem));
      if (attackedBy(m_board, ksqThem, us, m_board.getAllPieces()))
        gc = 1;
    }
    st.gaveCheck = gc;

    // 50-move rule
    if (movingPawn || st.captured.type != PieceType::None)
      m_state.halfmoveClock = 0;
    else
      ++m_state.halfmoveClock;

    // new EP square (double push)
    if (movingPawn)
    {
      if (us == Color::White && bb::rank_of(m.from()) == 1 && bb::rank_of(m.to()) == 3)
        m_state.enPassantSquare = static_cast<Square>(m.from() + 8);
      else if (us == Color::Black && bb::rank_of(m.from()) == 6 && bb::rank_of(m.to()) == 4)
        m_state.enPassantSquare = static_cast<Square>(m.from() - 8);
    }

    // castling rights & hash
    const std::uint8_t prevCR = m_state.castlingRights;
    m_state.castlingRights &= ~(CR_CLEAR_FROM[(int)m.from()] | CR_CLEAR_TO[(int)m.to()]);
    if (LILIA_UNLIKELY(prevCR != m_state.castlingRights))
      hashSetCastling(prevCR, m_state.castlingRights);

    // side flip & fullmove
    hashXorSide();
    m_state.sideToMove = them;
    if (them == Color::White)
      ++m_state.fullmoveNumber;

    // EP into hash
    xorEPRelevant();
  }

  void Position::unapplyMove(const StateInfo &st)
  {
    m_state.sideToMove = ~m_state.sideToMove;
    hashXorSide();
    if (m_state.sideToMove == Color::Black)
      --m_state.fullmoveNumber;

    hashSetCastling(m_state.castlingRights, st.prevCastlingRights);
    m_state.castlingRights = st.prevCastlingRights;

    m_state.enPassantSquare = st.prevEnPassantSquare;
    xorEPRelevant();

    m_state.halfmoveClock = st.prevHalfmoveClock;

    const Move &m = st.move;
    const Color us = m_state.sideToMove; // side that made 'm'
    const Color them = ~us;

    // Robustly re-detect castle / EP exactly because applyMove() may have inferred them
    // even if the move flags were not set.
    bool isCastleMove = false;
    if ((us == Color::White && m.from() == bb::E1 && (m.to() == bb::G1 || m.to() == bb::C1)) ||
        (us == Color::Black && m.from() == bb::E8 && (m.to() == bb::G8 || m.to() == bb::C8)))
    {
      if (auto moving = m_board.getPiece(m.to()); moving && moving->color == us &&
                                                  moving->type == PieceType::King)
        isCastleMove = true;
    }

    bool isEP = false;
    if (st.captured.type == PieceType::Pawn)
    {
      if (auto moving = m_board.getPiece(m.to()); moving && moving->color == us &&
                                                  moving->type == PieceType::Pawn &&
                                                  st.prevEnPassantSquare == m.to())
      {
        const int df = int(m.to()) - int(m.from());
        isEP = (us == Color::White) ? (df == 7 || df == 9)
                                    : (df == -7 || df == -9);
      }
    }

    // Undo castle rook first
    if (isCastleMove)
    {
      if (us == Color::White)
      {
        if (m.to() == bb::G1 || m.castle() == CastleSide::KingSide)
        {
          hashXorPiece(us, PieceType::Rook, bb::F1);
          m_board.movePiece_noCapture(bb::F1, bb::H1);
          hashXorPiece(us, PieceType::Rook, bb::H1);
        }
        else
        {
          hashXorPiece(us, PieceType::Rook, bb::D1);
          m_board.movePiece_noCapture(bb::D1, bb::A1);
          hashXorPiece(us, PieceType::Rook, bb::A1);
        }
      }
      else
      {
        if (m.to() == bb::G8 || m.castle() == CastleSide::KingSide)
        {
          hashXorPiece(us, PieceType::Rook, bb::F8);
          m_board.movePiece_noCapture(bb::F8, bb::H8);
          hashXorPiece(us, PieceType::Rook, bb::H8);
        }
        else
        {
          hashXorPiece(us, PieceType::Rook, bb::D8);
          m_board.movePiece_noCapture(bb::D8, bb::A8);
          hashXorPiece(us, PieceType::Rook, bb::A8);
        }
      }
    }

    // No capture, no promotion
    if (LILIA_LIKELY(m.promotion() == PieceType::None && st.captured.type == PieceType::None))
    {
      if (auto moving = m_board.getPiece(m.to()))
      {
        hashXorPiece(us, moving->type, m.to());
        m_board.movePiece_noCapture(m.to(), m.from());
        hashXorPiece(us, moving->type, m.from());
      }
      return;
    }

    // Non-promotion capture
    if (m.promotion() == PieceType::None && st.captured.type != PieceType::None)
    {
      if (auto moving = m_board.getPiece(m.to()))
      {
        hashXorPiece(us, moving->type, m.to());
        m_board.movePiece_noCapture(m.to(), m.from());
        hashXorPiece(us, moving->type, m.from());
      }

      if (isEP)
      {
        const Square capSq = (us == Color::White) ? static_cast<Square>(m.to() - 8)
                                                  : static_cast<Square>(m.to() + 8);
        hashXorPiece(them, st.captured.type, capSq);
        m_board.setPiece(capSq, st.captured);
      }
      else
      {
        hashXorPiece(them, st.captured.type, m.to());
        m_board.setPiece(m.to(), st.captured);
      }
      return;
    }

    // Promotions / mixed cases
    if (auto moving = m_board.getPiece(m.to()))
    {
      m_board.removePiece(m.to());

      Piece placed = *moving;
      if (m.promotion() != PieceType::None)
        placed.type = PieceType::Pawn;

      hashXorPiece(us, moving->type, m.to());
      hashXorPiece(us, placed.type, m.from());
      m_board.setPiece(m.from(), placed);
    }
    else
    {
      return;
    }

    if (isEP)
    {
      const Square capSq = (us == Color::White) ? static_cast<Square>(m.to() - 8)
                                                : static_cast<Square>(m.to() + 8);
      if (st.captured.type != PieceType::None)
      {
        hashXorPiece(them, st.captured.type, capSq);
        m_board.setPiece(capSq, st.captured);
      }
    }
    else if (st.captured.type != PieceType::None)
    {
      hashXorPiece(them, st.captured.type, m.to());
      m_board.setPiece(m.to(), st.captured);
    }
  }

}
