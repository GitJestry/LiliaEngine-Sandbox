#include "lilia/model/position.hpp"

#include <algorithm>
#include <array>
#include <limits>

#include "lilia/engine/config.hpp"
#include "lilia/model/core/magic.hpp"
#include "lilia/model/move_generator.hpp"
#include "lilia/model/move_helper.hpp"

namespace lilia::model {

namespace {

// --------- Castling-Right Clear-Masken (FROM/TO) ----------
constexpr std::array<std::uint8_t, 64> CR_CLEAR_FROM = [] {
  std::array<std::uint8_t, 64> a{};
  a[bb::E1] |= bb::Castling::WK | bb::Castling::WQ;
  a[bb::E8] |= bb::Castling::BK | bb::Castling::BQ;
  a[bb::H1] |= bb::Castling::WK;
  a[bb::A1] |= bb::Castling::WQ;
  a[bb::H8] |= bb::Castling::BK;
  a[bb::A8] |= bb::Castling::BQ;
  return a;
}();

constexpr std::array<std::uint8_t, 64> CR_CLEAR_TO = CR_CLEAR_FROM;  // identisch

inline bb::Bitboard pawn_attackers_to(core::Square sq, core::Color by, bb::Bitboard pawns) {
  const bb::Bitboard t = bb::sq_bb(sq);
  return by == core::Color::White ? ((bb::sw(t) | bb::se(t)) & pawns)
                                  : ((bb::nw(t) | bb::ne(t)) & pawns);
}

inline bool on_board_0_63(int s) {
  return (unsigned)s < 64u;
}

}  // namespace

// ---------------------- Utility Checks ----------------------

bool Position::checkInsufficientMaterial() {
  // Any pawn/rook/queen? -> not insufficient
  const bb::Bitboard majorsMinors = m_board.getPieces(core::Color::White, core::PieceType::Pawn) |
                                    m_board.getPieces(core::Color::Black, core::PieceType::Pawn) |
                                    m_board.getPieces(core::Color::White, core::PieceType::Rook) |
                                    m_board.getPieces(core::Color::Black, core::PieceType::Rook) |
                                    m_board.getPieces(core::Color::White, core::PieceType::Queen) |
                                    m_board.getPieces(core::Color::Black, core::PieceType::Queen);
  if (majorsMinors) return false;

  const bb::Bitboard whiteB = m_board.getPieces(core::Color::White, core::PieceType::Bishop);
  const bb::Bitboard blackB = m_board.getPieces(core::Color::Black, core::PieceType::Bishop);
  const bb::Bitboard whiteN = m_board.getPieces(core::Color::White, core::PieceType::Knight);
  const bb::Bitboard blackN = m_board.getPieces(core::Color::Black, core::PieceType::Knight);

  const int totalB = bb::popcount(whiteB) + bb::popcount(blackB);
  const int totalN = bb::popcount(whiteN) + bb::popcount(blackN);

  if (totalB == 0 && totalN == 0) return true;                                    // KK
  if ((totalB == 0 && totalN == 1) || (totalB == 1 && totalN == 0)) return true;  // KNK / KBK

  if (totalB == 2 && totalN == 0) {
    auto same_color_squares = [](bb::Bitboard bishops) {
      int light = 0, dark = 0;
      while (bishops) {
        core::Square s = bb::pop_lsb(bishops);
        ((int(s) ^ (int(s) >> 3)) & 1) ? ++dark : ++light;
      }
      return std::max(light, dark);
    };
    if (same_color_squares(whiteB) == 2 || same_color_squares(blackB) == 2)
      return true;  // KBKB same color
  }

  if (totalB == 0 && totalN == 2) return true;  // KNNK (usually draw)

  return false;
}

bool Position::checkMoveRule() {
  return (m_state.halfmoveClock >= 100);
}

bool Position::checkRepetition() {
  int count = 0;
  const int n = (int)m_history.size();
  const int lim = std::min<int>(n, m_state.halfmoveClock);
  for (int back = 2; back <= lim; back += 2) {
    const int idx = n - back;
    if (idx < 0) break;
    if (m_history[idx].zobristKey == m_hash && ++count >= 2) return true;
  }
  return false;
}

bool Position::inCheck() const {
  const bb::Bitboard kbb = m_board.getPieces(m_state.sideToMove, core::PieceType::King);
  if (!kbb) return false;
  const core::Square ksq = static_cast<core::Square>(bb::ctz64(kbb));
  return attackedBy(m_board, ksq, ~m_state.sideToMove, m_board.getAllPieces());
}

// ---------------------- isPseudoLegal (for search) ----------------------
// Checks basic move shape, occupancy and board consistency (but NOT self-check).
// For castling we also check path emptiness and (cheap) “no-through-check” to be safe.
bool Position::isPseudoLegal(const Move& m) const {
  if (!on_board_0_63(m.from()) || !on_board_0_63(m.to()) || m.from() == m.to()) return false;

  const auto fromP = m_board.getPiece(m.from());
  if (!fromP || fromP->color != m_state.sideToMove) return false;

  const auto toP = m_board.getPiece(m.to());
  const core::Color us = fromP->color, them = ~us;
  const bool isCap = (m.isEnPassant() ? true : (toP && toP->color == them));
  const bb::Bitboard occ = m_board.getAllPieces();

  using PT = core::PieceType;
  switch (fromP->type) {
    case PT::Pawn: {
      const int df = (int)m.to() - (int)m.from();
      const int fromRank = bb::rank_of(m.from());
      const bool white = (us == core::Color::White);

      // Promotions legal?
      if (m.promotion() != PT::None) {
        if (!((white && bb::rank_of(m.to()) == 7) || (!white && bb::rank_of(m.to()) == 0)))
          return false;
        if (!(m.promotion() == PT::Knight || m.promotion() == PT::Bishop ||
              m.promotion() == PT::Rook || m.promotion() == PT::Queen))
          return false;
      }

      // Quiet forward
      if (!isCap) {
        if (white) {
          if (df != 8 && !(df == 16 && fromRank == 1)) return false;
          if (occ & bb::sq_bb(static_cast<core::Square>(m.from() + 8))) return false;
          if (df == 16 && (occ & bb::sq_bb(static_cast<core::Square>(m.from() + 16)))) return false;
        } else {
          if (df != -8 && !(df == -16 && fromRank == 6)) return false;
          if (occ & bb::sq_bb(static_cast<core::Square>(m.from() - 8))) return false;
          if (df == -16 && (occ & bb::sq_bb(static_cast<core::Square>(m.from() - 16))))
            return false;
        }
        return true;
      }

      // Captures (normal or EP)
      if (m.isEnPassant()) {
        if (m_state.enPassantSquare != m.to()) return false;
        // moving diagonally by one
        if (white ? !(df == 7 || df == 9) : !(df == -7 || df == -9)) return false;
        // Captured pawn must be on the EP square’s behind
        const core::Square capSq =
            white ? static_cast<core::Square>(m.to() - 8) : static_cast<core::Square>(m.to() + 8);
        const auto capP = m_board.getPiece(capSq);
        return capP && capP->color == them && capP->type == PT::Pawn;
      } else {
        if (!toP || toP->color != them) return false;
        return white ? (df == 7 || df == 9) : (df == -7 || df == -9);
      }
    }

    case PT::Knight: {
      const bb::Bitboard atk = bb::knight_attacks_from(m.from());
      return (atk & bb::sq_bb(m.to())) && (!toP || toP->color == them);
    }

    case PT::Bishop: {
      const bb::Bitboard ray = magic::sliding_attacks(magic::Slider::Bishop, m.from(), occ);
      return (ray & bb::sq_bb(m.to())) && (!toP || toP->color == them);
    }

    case PT::Rook: {
      const bb::Bitboard ray = magic::sliding_attacks(magic::Slider::Rook, m.from(), occ);
      return (ray & bb::sq_bb(m.to())) && (!toP || toP->color == them);
    }

    case PT::Queen: {
      const bb::Bitboard ray = magic::sliding_attacks(magic::Slider::Bishop, m.from(), occ) |
                               magic::sliding_attacks(magic::Slider::Rook, m.from(), occ);
      return (ray & bb::sq_bb(m.to())) && (!toP || toP->color == them);
    }

    case PT::King: {
      // Normal king step
      if (bb::king_attacks_from(m.from()) & bb::sq_bb(m.to())) {
        return (!toP || toP->color == them);
      }
      // Castling (rare → we can afford the full legality here)
      if (us == core::Color::White) {
        if ((m_state.castlingRights & bb::Castling::WK) && m.from() == bb::E1 &&
            m.to() == core::Square{6}) {
          if ((occ & (bb::sq_bb(core::Square{5}) | bb::sq_bb(core::Square{6}))) == 0 &&
              !attackedBy(m_board, core::Square{4}, them, occ) &&
              !attackedBy(m_board, core::Square{5}, them, occ) &&
              !attackedBy(m_board, core::Square{6}, them, occ))
            return true;
        }
        if ((m_state.castlingRights & bb::Castling::WQ) && m.from() == bb::E1 &&
            m.to() == core::Square{2}) {
          if ((occ & (bb::sq_bb(core::Square{1}) | bb::sq_bb(core::Square{2}) |
                      bb::sq_bb(core::Square{3}))) == 0 &&
              !attackedBy(m_board, core::Square{4}, them, occ) &&
              !attackedBy(m_board, core::Square{3}, them, occ) &&
              !attackedBy(m_board, core::Square{2}, them, occ))
            return true;
        }
      } else {
        if ((m_state.castlingRights & bb::Castling::BK) && m.from() == bb::E8 &&
            m.to() == core::Square{62}) {
          if ((occ & (bb::sq_bb(core::Square{61}) | bb::sq_bb(core::Square{62}))) == 0 &&
              !attackedBy(m_board, core::Square{60}, them, occ) &&
              !attackedBy(m_board, core::Square{61}, them, occ) &&
              !attackedBy(m_board, core::Square{62}, them, occ))
            return true;
        }
        if ((m_state.castlingRights & bb::Castling::BQ) && m.from() == bb::E8 &&
            m.to() == core::Square{58}) {
          if ((occ & (bb::sq_bb(core::Square{57}) | bb::sq_bb(core::Square{58}) |
                      bb::sq_bb(core::Square{59}))) == 0 &&
              !attackedBy(m_board, core::Square{60}, them, occ) &&
              !attackedBy(m_board, core::Square{59}, them, occ) &&
              !attackedBy(m_board, core::Square{58}, them, occ))
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
// ------- SEE (Static Exchange Evaluation) -------
// Returns true if material result of playing 'm' on square 'to' is >= 0.
bool Position::see(const model::Move& m) const {
  using core::Color;
  using core::PieceType;
  using core::Square;

  // Trivial non-captures: SEE is used for captures; be permissive for quiets.
  if (!m.isCapture() && !m.isEnPassant()) return true;

  const auto fromP = m_board.getPiece(m.from());
  if (!fromP) return true;

  const Color us = fromP->color;
  const Color them = Color(~us);
  const Square to = m.to();

  // Snapshot occupancy and piece sets
  bb::Bitboard occ = m_board.getAllPieces();

  bb::Bitboard pcs[2][6];
  for (int c = 0; c < 2; ++c)
    for (int pt = 0; pt < 6; ++pt) pcs[c][pt] = m_board.getPieces((Color)c, (PieceType)pt);

  auto alive = [&](Color c, PieceType pt) -> bb::Bitboard { return pcs[(int)c][(int)pt] & occ; };
  auto val = [&](PieceType pt) -> int { return engine::base_value[(int)pt]; };

  auto king_sq = [&](Color c) -> Square {
    bb::Bitboard kbb = pcs[(int)c][(int)PieceType::King];
    return (Square)bb::ctz64(kbb);
  };

  // Helpers to get all attackers of side 'c' to 'to' given current 'occ'
  auto diag_rays = [&](bb::Bitboard o) {
    return magic::sliding_attacks(magic::Slider::Bishop, to, o);
  };
  auto ortho_rays = [&](bb::Bitboard o) {
    return magic::sliding_attacks(magic::Slider::Rook, to, o);
  };

  auto pawn_atk = [&](Color c) { return pawn_attackers_to(to, c, alive(c, PieceType::Pawn)); };
  auto knight_atk = [&](Color c) {
    return bb::knight_attacks_from(to) & alive(c, PieceType::Knight);
  };
  auto bishop_atk = [&](Color c, bb::Bitboard o) {
    return diag_rays(o) & alive(c, PieceType::Bishop);
  };
  auto rook_atk = [&](Color c, bb::Bitboard o) {
    return ortho_rays(o) & alive(c, PieceType::Rook);
  };
  auto queen_atk = [&](Color c, bb::Bitboard o) {
    const bb::Bitboard rays = diag_rays(o) | ortho_rays(o);
    return rays & alive(c, PieceType::Queen);
  };

  // Pin/legal guard: only check when we actually consider that specific attacker
  auto illegal_due_to_pin = [&](Color c, Square fromSq, bb::Bitboard oNow) -> bool {
    // Move piece from 'fromSq' to 'to' and see if own king becomes attacked.
    bb::Bitboard kbb = pcs[(int)c][(int)PieceType::King] & oNow;
    if (!kbb) return false;
    const Square ksq = (Square)bb::ctz64(kbb);

    // Remove from, add on 'to' (square 'to' remains occupied after a capture)
    bb::Bitboard occTest = (oNow & ~bb::sq_bb(fromSq)) | bb::sq_bb(to);
    return attackedBy(m_board, ksq, Color(~c), occTest);
  };

  // Pick the least valuable legal attacker for side 'c'. Returns false if none.
  auto pick_lva = [&](Color c, Square& fromSq, PieceType& pt, bb::Bitboard oNow) -> bool {
    // Compute rays once for current occupancy
    const bb::Bitboard diag = diag_rays(oNow);
    const bb::Bitboard ortho = ortho_rays(oNow);

    // Order: Pawn, Knight, Bishop, Rook, Queen, King
    bb::Bitboard cand;

    // Pawns
    cand = pawn_atk(c) & alive(c, PieceType::Pawn);
    while (cand) {
      Square f = bb::pop_lsb(cand);
      if (!illegal_due_to_pin(c, f, oNow)) {
        fromSq = f;
        pt = PieceType::Pawn;
        return true;
      }
    }

    // Knights
    cand = knight_atk(c) & alive(c, PieceType::Knight);
    while (cand) {
      Square f = bb::pop_lsb(cand);
      if (!illegal_due_to_pin(c, f, oNow)) {
        fromSq = f;
        pt = PieceType::Knight;
        return true;
      }
    }

    // Bishops
    cand = (diag & alive(c, PieceType::Bishop));
    while (cand) {
      Square f = bb::pop_lsb(cand);
      if (!illegal_due_to_pin(c, f, oNow)) {
        fromSq = f;
        pt = PieceType::Bishop;
        return true;
      }
    }

    // Rooks
    cand = (ortho & alive(c, PieceType::Rook));
    while (cand) {
      Square f = bb::pop_lsb(cand);
      if (!illegal_due_to_pin(c, f, oNow)) {
        fromSq = f;
        pt = PieceType::Rook;
        return true;
      }
    }

    // Queens
    cand = ((diag | ortho) & alive(c, PieceType::Queen));
    while (cand) {
      Square f = bb::pop_lsb(cand);
      if (!illegal_due_to_pin(c, f, oNow)) {
        fromSq = f;
        pt = PieceType::Queen;
        return true;
      }
    }

    // King (check target not covered after king moves there)
    bb::Bitboard kbb = alive(c, PieceType::King);
    if (kbb) {
      const Square kf = (Square)bb::ctz64(kbb);
      if (bb::king_attacks_from(kf) & bb::sq_bb(to)) {
        bb::Bitboard occK = (oNow & ~bb::sq_bb(kf)) | bb::sq_bb(to);
        if (!attackedBy(m_board, to, Color(~c), occK)) {
          fromSq = kf;
          pt = PieceType::King;
          return true;
        }
      }
    }
    return false;
  };

  // Identify captured piece and adjust occupancy for initial position at the node
  PieceType captured = PieceType::None;
  if (m.isEnPassant()) {
    captured = PieceType::Pawn;
    const Square capSq = (us == Color::White) ? Square(int(to) - 8) : Square(int(to) + 8);
    occ &= ~bb::sq_bb(capSq);  // remove the pawn that will be taken EP
  } else if (auto cap = m_board.getPiece(to)) {
    captured = cap->type;
    occ &= ~bb::sq_bb(to);  // square 'to' becomes temporarily empty before we land on it
  }

  // Move our piece to 'to' (occupancy stays with 'to' occupied from this point on)
  occ &= ~bb::sq_bb(m.from());
  PieceType pieceOnTo = (m.promotion() != PieceType::None) ? m.promotion() : fromP->type;
  occ |= bb::sq_bb(to);

  // Swap list
  int gain[32];
  int d = 0;
  gain[d++] = val(captured);

  // Alternate sides starting with the opponent
  Color side = them;

  // Iteratively “exchange” on 'to'
  for (;;) {
    Square from2 = core::NO_SQUARE;
    PieceType pt2 = PieceType::None;

    if (!pick_lva(side, from2, pt2, occ)) break;

    // They take what's on 'to'
    gain[d] = val(pieceOnTo) - gain[d - 1];
    ++d;

    // Prune: if this side is failing already, no need to go deeper
    if (gain[d - 1] < 0) break;

    // Move attacker onto 'to' (remove it from its square; 'to' stays occupied)
    occ &= ~bb::sq_bb(from2);

    // New piece now sits on 'to'
    pieceOnTo = pt2;
    side = Color(~side);

    if (d >= 31) break;  // sanity guard
  }

  // Negamax backpropagation
  while (--d) gain[d - 1] = std::max(-gain[d], gain[d - 1]);

  return gain[0] >= 0;
}

// ================== Make/Unmake (fast paths kept) ==================

bool Position::doMove(const Move& m) {
  if (m.from() == m.to()) return false;

  core::Color us = m_state.sideToMove;
  auto fromPiece = m_board.getPiece(m.from());
  if (!fromPiece || fromPiece->color != us) return false;

  // Promotions robust validieren
  if (m.promotion() != core::PieceType::None) {
    if (fromPiece->type != core::PieceType::Pawn) return false;
    const int toRank = bb::rank_of(m.to());
    const bool onPromoRank = (us == core::Color::White) ? (toRank == 7) : (toRank == 0);
    if (!onPromoRank) return false;
    switch (m.promotion()) {
      case core::PieceType::Knight:
      case core::PieceType::Bishop:
      case core::PieceType::Rook:
      case core::PieceType::Queen:
        break;
      default:
        return false;
    }
  }

  StateInfo st{};
  st.move = m;
  st.zobristKey = m_hash;
  st.prevCastlingRights = m_state.castlingRights;
  st.prevEnPassantSquare = m_state.enPassantSquare;
  st.prevHalfmoveClock = m_state.halfmoveClock;
  st.prevPawnKey = m_state.pawnKey;

  applyMove(m, st);

  // Illegal moves king
  core::Color movedSide = ~m_state.sideToMove;
  const bb::Bitboard kbbAfter = m_board.getPieces(movedSide, core::PieceType::King);
  if (!kbbAfter) {
    unapplyMove(st);
    m_hash = st.zobristKey;
    m_state.pawnKey = st.prevPawnKey;
    return false;
  }
  const core::Square ksqAfter = static_cast<core::Square>(bb::ctz64(kbbAfter));
  if (attackedBy(m_board, ksqAfter, m_state.sideToMove, m_board.getAllPieces())) {
    unapplyMove(st);
    m_hash = st.zobristKey;
    m_state.pawnKey = st.prevPawnKey;
    return false;
  }

  m_history.push_back(st);
  return true;
}

void Position::undoMove() {
  if (m_history.empty()) return;
  StateInfo st = m_history.back();
  unapplyMove(st);
  m_hash = st.zobristKey;
  m_state.pawnKey = st.prevPawnKey;
  m_history.pop_back();
}

bool Position::doNullMove() {
  NullState st{};
  st.zobristKey = m_hash;
  st.prevCastlingRights = m_state.castlingRights;
  st.prevEnPassantSquare = m_state.enPassantSquare;
  st.prevHalfmoveClock = m_state.halfmoveClock;
  st.prevFullmoveNumber = m_state.fullmoveNumber;

  xorEPRelevant();
  m_state.enPassantSquare = core::NO_SQUARE;

  ++m_state.halfmoveClock;

  hashXorSide();
  m_state.sideToMove = ~m_state.sideToMove;
  if (m_state.sideToMove == core::Color::White) ++m_state.fullmoveNumber;

  m_null_history.push_back(st);
  return true;
}

void Position::undoNullMove() {
  if (m_null_history.empty()) return;
  NullState st = m_null_history.back();
  m_null_history.pop_back();

  m_state.sideToMove = ~m_state.sideToMove;
  hashXorSide();

  m_state.fullmoveNumber = st.prevFullmoveNumber;
  m_state.enPassantSquare = st.prevEnPassantSquare;
  xorEPRelevant();

  m_state.castlingRights = st.prevCastlingRights;
  m_state.halfmoveClock = st.prevHalfmoveClock;

  m_hash = st.zobristKey;
}

// ===== Position::applyMove (optimized) =====
void Position::applyMove(const Move& m, StateInfo& st) {
  core::Color us = m_state.sideToMove;
  core::Color them = ~us;

  // EP out of hash; clear EP
  xorEPRelevant();
  const core::Square prevEP = m_state.enPassantSquare;
  m_state.enPassantSquare = core::NO_SQUARE;

  const auto fromPiece = m_board.getPiece(m.from());
  if (!fromPiece) return;
  const bool movingPawn = (fromPiece->type == core::PieceType::Pawn);

  // Detect castling
  bool isCastleMove = (m.castle() != CastleSide::None);
  if (!isCastleMove && fromPiece->type == core::PieceType::King) {
    if (us == core::Color::White && m.from() == bb::E1 &&
        (m.to() == core::Square{6} || m.to() == core::Square{2}))
      isCastleMove = true;
    if (us == core::Color::Black && m.from() == bb::E8 &&
        (m.to() == core::Square{62} || m.to() == core::Square{58}))
      isCastleMove = true;
  }

  // Detect en passant robustly
  bool isEP = m.isEnPassant();
  if (!isEP && movingPawn) {
    const int df = (int)m.to() - (int)m.from();
    const bool diag = (df == 7 || df == 9 || df == -7 || df == -9);
    if (diag && prevEP != core::NO_SQUARE && m.to() == prevEP) {
      if (!m_board.getPiece(m.to()).has_value()) isEP = true;
    }
  }

  // Detect capture
  bool isCap = m.isCapture();
  if (!isCap && !isEP) {
    auto cap = m_board.getPiece(m.to());
    if (cap && cap->color == them) isCap = true;
  }

  // Determine captured piece and store in state
  if (isEP) {
    const core::Square capSq = (us == core::Color::White) ? static_cast<core::Square>(m.to() - 8)
                                                          : static_cast<core::Square>(m.to() + 8);
    auto cap = m_board.getPiece(capSq);
    st.captured = cap.value_or(bb::Piece{core::PieceType::Pawn, them});
    st.captured.type = core::PieceType::Pawn;
  } else if (isCap) {
    auto cap = m_board.getPiece(m.to());
    st.captured = cap.value_or(bb::Piece{core::PieceType::None, them});
  } else {
    st.captured = bb::Piece{core::PieceType::None, them};
  }

  // ===== fast paths =====
  bb::Piece placed = *fromPiece;
  const bool fastQuiet =
      (!isCap && !isEP && !isCastleMove && m.promotion() == core::PieceType::None);
  const bool fastCap = (isCap && !isEP && !isCastleMove && m.promotion() == core::PieceType::None &&
                        st.captured.type != core::PieceType::None);
  const bool fastEP = (isEP && m.promotion() == core::PieceType::None);

  if (fastQuiet) {
    evalAcc_.move_piece(us, placed.type, (int)m.from(), (int)m.to());
    hashXorPiece(us, placed.type, m.from());
    m_board.movePiece_noCapture(m.from(), m.to());
    hashXorPiece(us, placed.type, m.to());
  } else if (fastCap) {
    evalAcc_.remove_piece(them, st.captured.type, (int)m.to());
    evalAcc_.move_piece(us, placed.type, (int)m.from(), (int)m.to());

    hashXorPiece(them, st.captured.type, m.to());
    hashXorPiece(us, placed.type, m.from());
    m_board.movePiece_withCapture(m.from(), m.to(), m.to(), st.captured);
    hashXorPiece(us, placed.type, m.to());
  } else if (fastEP) {
    const core::Square capSq = (us == core::Color::White) ? static_cast<core::Square>(m.to() - 8)
                                                          : static_cast<core::Square>(m.to() + 8);
    evalAcc_.remove_piece(them, core::PieceType::Pawn, (int)capSq);
    evalAcc_.move_piece(us, core::PieceType::Pawn, (int)m.from(), (int)m.to());

    hashXorPiece(them, core::PieceType::Pawn, capSq);
    hashXorPiece(us, core::PieceType::Pawn, m.from());
    m_board.movePiece_withCapture(m.from(), capSq, m.to(), bb::Piece{core::PieceType::Pawn, them});
    hashXorPiece(us, core::PieceType::Pawn, m.to());
  } else {
    // general slow path (promotions and mixed cases)
    hashXorPiece(us, placed.type, m.from());
    m_board.removePiece(m.from());

    if (m.promotion() != core::PieceType::None) placed.type = m.promotion();

    if (isEP) {
      const core::Square capSq = (us == core::Color::White) ? static_cast<core::Square>(m.to() - 8)
                                                            : static_cast<core::Square>(m.to() + 8);
      evalAcc_.remove_piece(them, core::PieceType::Pawn, (int)capSq);
      hashXorPiece(them, core::PieceType::Pawn, capSq);
      m_board.removePiece(capSq);
    } else if (isCap && st.captured.type != core::PieceType::None) {
      evalAcc_.remove_piece(them, st.captured.type, (int)m.to());
      hashXorPiece(them, st.captured.type, m.to());
      m_board.removePiece(m.to());
    }

    if (m.promotion() != core::PieceType::None) {
      evalAcc_.remove_piece(us, fromPiece->type, (int)m.from());
      evalAcc_.add_piece(us, placed.type, (int)m.to());
    } else {
      evalAcc_.move_piece(us, fromPiece->type, (int)m.from(), (int)m.to());
    }

    hashXorPiece(us, placed.type, m.to());
    m_board.setPiece(m.to(), placed);
  }

  // Castle rook move (fast)
  if (isCastleMove) {
    if (us == core::Color::White) {
      if (m.to() == core::Square{6} || m.castle() == CastleSide::KingSide) {
        evalAcc_.move_piece(us, core::PieceType::Rook, (int)bb::H1, 5);
        hashXorPiece(us, core::PieceType::Rook, bb::H1);
        m_board.movePiece_noCapture(bb::H1, core::Square{5});
        hashXorPiece(us, core::PieceType::Rook, core::Square{5});
      } else {
        evalAcc_.move_piece(us, core::PieceType::Rook, (int)bb::A1, 3);
        hashXorPiece(us, core::PieceType::Rook, bb::A1);
        m_board.movePiece_noCapture(bb::A1, core::Square{3});
        hashXorPiece(us, core::PieceType::Rook, core::Square{3});
      }
    } else {
      if (m.to() == core::Square{62} || m.castle() == CastleSide::KingSide) {
        evalAcc_.move_piece(us, core::PieceType::Rook, (int)bb::H8, 61);
        hashXorPiece(us, core::PieceType::Rook, bb::H8);
        m_board.movePiece_noCapture(bb::H8, core::Square{61});
        hashXorPiece(us, core::PieceType::Rook, core::Square{61});
      } else {
        evalAcc_.move_piece(us, core::PieceType::Rook, (int)bb::A8, 59);
        hashXorPiece(us, core::PieceType::Rook, bb::A8);
        m_board.movePiece_noCapture(bb::A8, core::Square{59});
        hashXorPiece(us, core::PieceType::Rook, core::Square{59});
      }
    }
  }

  // gaveCheck (compute before side flip)
  const bb::Bitboard kThem = m_board.getPieces(them, core::PieceType::King);
  std::uint8_t gc = 0;
  if (kThem) {
    const core::Square ksqThem = static_cast<core::Square>(bb::ctz64(kThem));
    if (attackedBy(m_board, ksqThem, us, m_board.getAllPieces())) gc = 1;
  }
  st.gaveCheck = gc;

  // 50-move rule
  if (placed.type == core::PieceType::Pawn || st.captured.type != core::PieceType::None)
    m_state.halfmoveClock = 0;
  else
    ++m_state.halfmoveClock;

  // new EP square (double push)
  if (placed.type == core::PieceType::Pawn) {
    if (us == core::Color::White && bb::rank_of(m.from()) == 1 && bb::rank_of(m.to()) == 3)
      m_state.enPassantSquare = static_cast<core::Square>(m.from() + 8);
    else if (us == core::Color::Black && bb::rank_of(m.from()) == 6 && bb::rank_of(m.to()) == 4)
      m_state.enPassantSquare = static_cast<core::Square>(m.from() - 8);
  }

  // castling rights & hash
  const std::uint8_t prevCR = m_state.castlingRights;
  m_state.castlingRights &= ~(CR_CLEAR_FROM[(int)m.from()] | CR_CLEAR_TO[(int)m.to()]);
  if (prevCR != m_state.castlingRights) hashSetCastling(prevCR, m_state.castlingRights);

  // side flip & fullmove
  hashXorSide();
  m_state.sideToMove = them;
  if (them == core::Color::White) ++m_state.fullmoveNumber;

  // EP into hash
  xorEPRelevant();
}

// ===== Position::unapplyMove (optimized) =====
void Position::unapplyMove(const StateInfo& st) {
  // Flip side back
  m_state.sideToMove = ~m_state.sideToMove;
  hashXorSide();
  if (m_state.sideToMove == core::Color::Black) --m_state.fullmoveNumber;

  // Restore castling rights (+hash)
  hashSetCastling(m_state.castlingRights, st.prevCastlingRights);
  m_state.castlingRights = st.prevCastlingRights;

  // Restore EP (+hash)
  m_state.enPassantSquare = st.prevEnPassantSquare;
  xorEPRelevant();

  // Restore 50-move clock
  m_state.halfmoveClock = st.prevHalfmoveClock;

  const Move& m = st.move;
  const core::Color us = m_state.sideToMove;  // the side that made 'm'
  const core::Color them = ~us;

  // Undo castle rook (fast)
  if (m.castle() != CastleSide::None) {
    if (us == core::Color::White) {
      if (m.castle() == CastleSide::KingSide) {
        evalAcc_.move_piece(us, core::PieceType::Rook, 5, (int)bb::H1);
        hashXorPiece(us, core::PieceType::Rook, core::Square{5});
        m_board.movePiece_noCapture(core::Square{5}, bb::H1);
        hashXorPiece(us, core::PieceType::Rook, bb::H1);
      } else {
        evalAcc_.move_piece(us, core::PieceType::Rook, 3, (int)bb::A1);
        hashXorPiece(us, core::PieceType::Rook, core::Square{3});
        m_board.movePiece_noCapture(core::Square{3}, bb::A1);
        hashXorPiece(us, core::PieceType::Rook, bb::A1);
      }
    } else {
      if (m.castle() == CastleSide::KingSide) {
        evalAcc_.move_piece(us, core::PieceType::Rook, 61, (int)bb::H8);
        hashXorPiece(us, core::PieceType::Rook, core::Square{61});
        m_board.movePiece_noCapture(core::Square{61}, bb::H8);
        hashXorPiece(us, core::PieceType::Rook, bb::H8);
      } else {
        evalAcc_.move_piece(us, core::PieceType::Rook, 59, (int)bb::A8);
        hashXorPiece(us, core::PieceType::Rook, core::Square{59});
        m_board.movePiece_noCapture(core::Square{59}, bb::A8);
        hashXorPiece(us, core::PieceType::Rook, bb::A8);
      }
    }
  }

  // Fast: no capture & no promotion
  if (m.promotion() == core::PieceType::None && st.captured.type == core::PieceType::None) {
    if (auto moving = m_board.getPiece(m.to())) {
      evalAcc_.move_piece(us, moving->type, (int)m.to(), (int)m.from());
      hashXorPiece(us, moving->type, m.to());
      m_board.movePiece_noCapture(m.to(), m.from());
      hashXorPiece(us, moving->type, m.from());
    }
    return;
  }

  // Fast: non-promotion capture
  if (m.promotion() == core::PieceType::None && st.captured.type != core::PieceType::None) {
    if (auto moving = m_board.getPiece(m.to())) {
      evalAcc_.move_piece(us, moving->type, (int)m.to(), (int)m.from());
      hashXorPiece(us, moving->type, m.to());
      m_board.movePiece_noCapture(m.to(), m.from());
      hashXorPiece(us, moving->type, m.from());
    }
    if (m.isEnPassant()) {
      const core::Square capSq = (us == core::Color::White) ? static_cast<core::Square>(m.to() - 8)
                                                            : static_cast<core::Square>(m.to() + 8);
      evalAcc_.add_piece(them, st.captured.type, (int)capSq);
      hashXorPiece(them, st.captured.type, capSq);
      m_board.setPiece(capSq, st.captured);
    } else {
      evalAcc_.add_piece(them, st.captured.type, (int)m.to());
      hashXorPiece(them, st.captured.type, m.to());
      m_board.setPiece(m.to(), st.captured);
    }
    return;
  }

  // Slow: promotions / mixed cases
  if (auto moving = m_board.getPiece(m.to())) {
    m_board.removePiece(m.to());
    bb::Piece placed = *moving;
    if (m.promotion() != core::PieceType::None) placed.type = core::PieceType::Pawn;

    if (m.promotion() != core::PieceType::None) {
      evalAcc_.remove_piece(us, moving->type, (int)m.to());
      evalAcc_.add_piece(us, core::PieceType::Pawn, (int)m.from());
    } else {
      evalAcc_.move_piece(us, moving->type, (int)m.to(), (int)m.from());
    }

    hashXorPiece(us, moving->type, m.to());
    hashXorPiece(us, placed.type, m.from());
    m_board.setPiece(m.from(), placed);
  } else {
    return;
  }

  if (m.isEnPassant()) {
    const core::Square capSq = (us == core::Color::White) ? static_cast<core::Square>(m.to() - 8)
                                                          : static_cast<core::Square>(m.to() + 8);
    if (st.captured.type != core::PieceType::None) {
      evalAcc_.add_piece(them, st.captured.type, (int)capSq);
      hashXorPiece(them, st.captured.type, capSq);
      m_board.setPiece(capSq, st.captured);
    }
  } else if (st.captured.type != core::PieceType::None) {
    evalAcc_.add_piece(them, st.captured.type, (int)m.to());
    hashXorPiece(them, st.captured.type, m.to());
    m_board.setPiece(m.to(), st.captured);
  }
}

}  // namespace lilia::model
