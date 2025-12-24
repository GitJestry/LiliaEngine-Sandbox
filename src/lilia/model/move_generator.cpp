#include "lilia/model/move_generator.hpp"

#include <array>
#include <cassert>
#include <cstdint>

#include "lilia/model/core/magic.hpp"
#include "lilia/model/move_helper.hpp"

namespace lilia::model {

namespace {

using core::Color;
using core::PieceType;
using core::Square;

using PT = core::PieceType;

struct SideSets {
  bb::Bitboard pawns, knights, bishops, rooks, queens, king, all, noKing;
};

LILIA_ALWAYS_INLINE SideSets side_sets(const Board& b, Color c) noexcept {
  const bb::Bitboard pawns = b.getPieces(c, PT::Pawn);
  const bb::Bitboard knights = b.getPieces(c, PT::Knight);
  const bb::Bitboard bishops = b.getPieces(c, PT::Bishop);
  const bb::Bitboard rooks = b.getPieces(c, PT::Rook);
  const bb::Bitboard queens = b.getPieces(c, PT::Queen);
  const bb::Bitboard king = b.getPieces(c, PT::King);
  const bb::Bitboard all = b.getPieces(c);
  return SideSets{pawns, knights, bishops, rooks, queens, king, all, all & ~king};
}

// ---------------- Between table ----------------

constexpr bb::Bitboard compute_between_single(int ai, int bi) noexcept {
  if (ai == bi) return 0ULL;
  const int d = bi - ai;
  const int ar = ai / 8, br = bi / 8;
  const int af = ai % 8, bf = bi % 8;
  int step = 0;
  if (ar == br)
    step = (d > 0 ? 1 : -1);
  else if (af == bf)
    step = (d > 0 ? 8 : -8);
  else if (d % 9 == 0 && (br - ar) == (bf - af))
    step = (d > 0 ? 9 : -9);
  else if (d % 7 == 0 && (br - ar) == (af - bf))
    step = (d > 0 ? 7 : -7);
  else
    return 0ULL;

  bb::Bitboard mask = 0ULL;
  for (int cur = ai + step; cur != bi; cur += step) mask |= bb::sq_bb(static_cast<Square>(cur));
  return mask;
}

constexpr std::array<std::array<bb::Bitboard, 64>, 64> build_between_table() {
  std::array<std::array<bb::Bitboard, 64>, 64> T{};
  for (int a = 0; a < 64; ++a)
    for (int b = 0; b < 64; ++b) T[a][b] = compute_between_single(a, b);
  return T;
}

static inline constexpr auto Between = build_between_table();

LILIA_ALWAYS_INLINE constexpr bb::Bitboard squares_between(Square a, Square b) noexcept {
  return Between[(int)a][(int)b];
}

// Align helpers
LILIA_ALWAYS_INLINE bool aligned_diag(Square a, Square b) noexcept {
  const int af = (int)a & 7, bf = (int)b & 7;
  const int ar = (int)a >> 3, br = (int)b >> 3;
  const int df = af - bf, dr = ar - br;
  return (df == dr) || (df == -dr);
}
LILIA_ALWAYS_INLINE bool aligned_ortho(Square a, Square b) noexcept {
  return (((int)a ^ (int)b) & 7) == 0 || (((int)a ^ (int)b) & 56) == 0;
}

// ---------------- Pin info (array, O(1) lookup) ----------------

struct PinInfo {
  bb::Bitboard pinned = 0ULL;
  bb::Bitboard allow[64];  // only valid for squares flagged in 'pinned'

  LILIA_ALWAYS_INLINE void reset() noexcept {
    pinned = 0ULL;  // allow[] need not be touched; we guard reads with 'pinned' checks
  }
  LILIA_ALWAYS_INLINE void add(Square s, bb::Bitboard m) noexcept {
    pinned |= bb::sq_bb(s);
    allow[(int)s] = m;
  }
  LILIA_ALWAYS_INLINE bb::Bitboard allow_mask(Square s) const noexcept { return allow[(int)s]; }
};

// Robust pins: iterate enemy sliders aligned with king; exactly-one-blocker rule.
LILIA_ALWAYS_INLINE void compute_pins(const Board& b, Color us, const bb::Bitboard occ,
                                      PinInfo& out) noexcept {
  out.reset();

  const bb::Bitboard kbb = b.getPieces(us, PT::King);
  if (!kbb) return;
  const Square ksq = static_cast<Square>(bb::ctz64(kbb));

  const bb::Bitboard ourPieces = b.getPieces(us);
  const bb::Bitboard oppBishQueens = b.getPieces(~us, PT::Bishop) | b.getPieces(~us, PT::Queen);
  const bb::Bitboard oppRookQueens = b.getPieces(~us, PT::Rook) | b.getPieces(~us, PT::Queen);

  auto try_pinner = [&](Square pinnerSq, bool isDiag) noexcept {
    if (isDiag ? !aligned_diag(ksq, pinnerSq) : !aligned_ortho(ksq, pinnerSq)) return;
    const bb::Bitboard between = squares_between(ksq, pinnerSq);
    if (!between) return;
    const bb::Bitboard blockers = between & occ;
    if (!blockers || (blockers & (blockers - 1))) return;  // not exactly one
    if (!(blockers & ourPieces)) return;                   // not our piece
    const Square pinnedSq = static_cast<Square>(bb::ctz64(blockers));
    out.add(pinnedSq, between | bb::sq_bb(pinnerSq));
  };

  for (bb::Bitboard s = oppBishQueens; s;) try_pinner(bb::pop_lsb(s), true);
  for (bb::Bitboard s = oppRookQueens; s;) try_pinner(bb::pop_lsb(s), false);
}

// --------- Fast EP legality (tolerant on malformed setups) ---------
LILIA_ALWAYS_INLINE bool ep_is_legal_fast(const Board& b, Color side, Square from,
                                          Square to) noexcept {
  const bb::Bitboard kbb = b.getPieces(side, PT::King);
  if (!kbb) return true;

  const Square ksq = static_cast<Square>(bb::ctz64(kbb));
  if (bb::rank_of(ksq) != bb::rank_of(from)) return true;

  const int to_i = (int)to;
  const int cap_i = (side == Color::White) ? (to_i - 8) : (to_i + 8);
  const Square capSq = static_cast<Square>(cap_i);

  bb::Bitboard occ = b.getAllPieces();
  occ &= ~bb::sq_bb(from);
  occ &= ~bb::sq_bb(capSq);
  occ |= bb::sq_bb(to);

  const bb::Bitboard sliders = b.getPieces(~side, PT::Rook) | b.getPieces(~side, PT::Queen);
  const bb::Bitboard rays = magic::sliding_attacks(magic::Slider::Rook, ksq, occ);
  return (rays & sliders) == 0ULL;
}

// ---------------- Templated generators ----------------

template <core::Color Side, class Emit>
LILIA_ALWAYS_INLINE void genPawnMoves_T(const Board& board, const GameState& st, bb::Bitboard occ,
                                        const SideSets& our, const SideSets& opp,
                                        const PinInfo* pins, Emit&& emit,
                                        bb::Bitboard targetMask = ~0ULL) noexcept {
  if (!our.pawns) return;

  const bb::Bitboard empty = ~occ;
  const bb::Bitboard enemyAll = opp.all;
  const bb::Bitboard them = enemyAll & ~opp.king;

  constexpr bool W = (Side == core::Color::White);

  if constexpr (W) {
    const bb::Bitboard one = bb::north(our.pawns) & empty;
    const bb::Bitboard dbl = bb::north(one & bb::RANK_3) & empty;

    const bb::Bitboard promoPush = (one & bb::RANK_8) & targetMask;
    const bb::Bitboard quietPush = (one & ~bb::RANK_8) & targetMask;

    const bb::Bitboard capL = ((bb::nw(our.pawns) & them) & ~bb::RANK_8) & targetMask;
    const bb::Bitboard capR = ((bb::ne(our.pawns) & them) & ~bb::RANK_8) & targetMask;

    for (bb::Bitboard q = quietPush; q;) {
      const core::Square to = bb::pop_lsb(q);
      const core::Square from = static_cast<core::Square>(to - 8);
      const bb::Bitboard fromMask = bb::sq_bb(from);
      const bool pinned = pins && (pins->pinned & fromMask);
      const bb::Bitboard allowMask = pinned ? pins->allow_mask(from) : ~0ULL;
      if (bb::sq_bb(to) & allowMask) emit(Move{from, to, PT::None, false, false, CastleSide::None});
    }
    for (bb::Bitboard d = (dbl & targetMask); d;) {
      const core::Square to = bb::pop_lsb(d);
      const core::Square from = static_cast<core::Square>(to - 16);
      const bb::Bitboard fromMask = bb::sq_bb(from);
      const bool pinned = pins && (pins->pinned & fromMask);
      const bb::Bitboard allowMask = pinned ? pins->allow_mask(from) : ~0ULL;
      if (bb::sq_bb(to) & allowMask) emit(Move{from, to, PT::None, false, false, CastleSide::None});
    }
    for (bb::Bitboard c = capL; c;) {
      const core::Square to = bb::pop_lsb(c);
      const core::Square from = static_cast<core::Square>(to - 7);
      const bb::Bitboard fromMask = bb::sq_bb(from);
      const bool pinned = pins && (pins->pinned & fromMask);
      const bb::Bitboard allowMask = pinned ? pins->allow_mask(from) : ~0ULL;
      if (bb::sq_bb(to) & allowMask) emit(Move{from, to, PT::None, true, false, CastleSide::None});
    }
    for (bb::Bitboard c = capR; c;) {
      const core::Square to = bb::pop_lsb(c);
      const core::Square from = static_cast<core::Square>(to - 9);
      const bb::Bitboard fromMask = bb::sq_bb(from);
      const bool pinned = pins && (pins->pinned & fromMask);
      const bb::Bitboard allowMask = pinned ? pins->allow_mask(from) : ~0ULL;
      if (bb::sq_bb(to) & allowMask) emit(Move{from, to, PT::None, true, false, CastleSide::None});
    }

    // Promotions
    constexpr PT promoOrder[4] = {PT::Queen, PT::Rook, PT::Bishop, PT::Knight};
    for (bb::Bitboard pp = promoPush; pp;) {
      const core::Square to = bb::pop_lsb(pp);
      const core::Square from = static_cast<core::Square>(to - 8);
      const bb::Bitboard fromMask = bb::sq_bb(from);
      const bool pinned = pins && (pins->pinned & fromMask);
      const bb::Bitboard allowMask = pinned ? pins->allow_mask(from) : ~0ULL;
      if (!(bb::sq_bb(to) & allowMask)) continue;
      for (int i = 0; i < 4; ++i)
        emit(Move{from, to, promoOrder[i], false, false, CastleSide::None});
    }
    const bb::Bitboard capLP = ((bb::nw(our.pawns) & them) & bb::RANK_8) & targetMask;
    const bb::Bitboard capRP = ((bb::ne(our.pawns) & them) & bb::RANK_8) & targetMask;
    for (bb::Bitboard c = capLP; c;) {
      const core::Square to = bb::pop_lsb(c);
      const core::Square from = static_cast<core::Square>(to - 7);
      const bb::Bitboard fromMask = bb::sq_bb(from);
      const bool pinned = pins && (pins->pinned & fromMask);
      const bb::Bitboard allowMask = pinned ? pins->allow_mask(from) : ~0ULL;
      if (!(bb::sq_bb(to) & allowMask)) continue;
      for (int i = 0; i < 4; ++i)
        emit(Move{from, to, promoOrder[i], true, false, CastleSide::None});
    }
    for (bb::Bitboard c = capRP; c;) {
      const core::Square to = bb::pop_lsb(c);
      const core::Square from = static_cast<core::Square>(to - 9);
      const bb::Bitboard fromMask = bb::sq_bb(from);
      const bool pinned = pins && (pins->pinned & fromMask);
      const bb::Bitboard allowMask = pinned ? pins->allow_mask(from) : ~0ULL;
      if (!(bb::sq_bb(to) & allowMask)) continue;
      for (int i = 0; i < 4; ++i)
        emit(Move{from, to, promoOrder[i], true, false, CastleSide::None});
    }

  } else {  // Black
    const bb::Bitboard one = bb::south(our.pawns) & empty;
    const bb::Bitboard dbl = bb::south(one & bb::RANK_6) & empty;

    const bb::Bitboard promoPush = (one & bb::RANK_1) & targetMask;
    const bb::Bitboard quietPush = (one & ~bb::RANK_1) & targetMask;

    const bb::Bitboard capL = ((bb::se(our.pawns) & them) & ~bb::RANK_1) & targetMask;
    const bb::Bitboard capR = ((bb::sw(our.pawns) & them) & ~bb::RANK_1) & targetMask;

    for (bb::Bitboard q = quietPush; q;) {
      const core::Square to = bb::pop_lsb(q);
      const core::Square from = static_cast<core::Square>(to + 8);
      const bb::Bitboard fromMask = bb::sq_bb(from);
      const bool pinned = pins && (pins->pinned & fromMask);
      const bb::Bitboard allowMask = pinned ? pins->allow_mask(from) : ~0ULL;
      if (bb::sq_bb(to) & allowMask) emit(Move{from, to, PT::None, false, false, CastleSide::None});
    }
    for (bb::Bitboard d = (dbl & targetMask); d;) {
      const core::Square to = bb::pop_lsb(d);
      const core::Square from = static_cast<core::Square>(to + 16);
      const bb::Bitboard fromMask = bb::sq_bb(from);
      const bool pinned = pins && (pins->pinned & fromMask);
      const bb::Bitboard allowMask = pinned ? pins->allow_mask(from) : ~0ULL;
      if (bb::sq_bb(to) & allowMask) emit(Move{from, to, PT::None, false, false, CastleSide::None});
    }
    for (bb::Bitboard c = capL; c;) {
      const core::Square to = bb::pop_lsb(c);
      const core::Square from = static_cast<core::Square>(to + 7);
      const bb::Bitboard fromMask = bb::sq_bb(from);
      const bool pinned = pins && (pins->pinned & fromMask);
      const bb::Bitboard allowMask = pinned ? pins->allow_mask(from) : ~0ULL;
      if (bb::sq_bb(to) & allowMask) emit(Move{from, to, PT::None, true, false, CastleSide::None});
    }
    for (bb::Bitboard c = capR; c;) {
      const core::Square to = bb::pop_lsb(c);
      const core::Square from = static_cast<core::Square>(to + 9);
      const bb::Bitboard fromMask = bb::sq_bb(from);
      const bool pinned = pins && (pins->pinned & fromMask);
      const bb::Bitboard allowMask = pinned ? pins->allow_mask(from) : ~0ULL;
      if (bb::sq_bb(to) & allowMask) emit(Move{from, to, PT::None, true, false, CastleSide::None});
    }

    constexpr PT promoOrder[4] = {PT::Queen, PT::Rook, PT::Bishop, PT::Knight};
    for (bb::Bitboard pp = promoPush; pp;) {
      const core::Square to = bb::pop_lsb(pp);
      const core::Square from = static_cast<core::Square>(to + 8);
      const bb::Bitboard fromMask = bb::sq_bb(from);
      const bool pinned = pins && (pins->pinned & fromMask);
      const bb::Bitboard allowMask = pinned ? pins->allow_mask(from) : ~0ULL;
      if (!(bb::sq_bb(to) & allowMask)) continue;
      for (int i = 0; i < 4; ++i)
        emit(Move{from, to, promoOrder[i], false, false, CastleSide::None});
    }
    const bb::Bitboard capLP = ((bb::se(our.pawns) & them) & bb::RANK_1) & targetMask;
    const bb::Bitboard capRP = ((bb::sw(our.pawns) & them) & bb::RANK_1) & targetMask;
    for (bb::Bitboard c = capLP; c;) {
      const core::Square to = bb::pop_lsb(c);
      const core::Square from = static_cast<core::Square>(to + 7);
      const bb::Bitboard fromMask = bb::sq_bb(from);
      const bool pinned = pins && (pins->pinned & fromMask);
      const bb::Bitboard allowMask = pinned ? pins->allow_mask(from) : ~0ULL;
      if (!(bb::sq_bb(to) & allowMask)) continue;
      for (int i = 0; i < 4; ++i)
        emit(Move{from, to, promoOrder[i], true, false, CastleSide::None});
    }
    for (bb::Bitboard c = capRP; c;) {
      const core::Square to = bb::pop_lsb(c);
      const core::Square from = static_cast<core::Square>(to + 9);
      const bb::Bitboard fromMask = bb::sq_bb(from);
      const bool pinned = pins && (pins->pinned & fromMask);
      const bb::Bitboard allowMask = pinned ? pins->allow_mask(from) : ~0ULL;
      if (!(bb::sq_bb(to) & allowMask)) continue;
      for (int i = 0; i < 4; ++i)
        emit(Move{from, to, promoOrder[i], true, false, CastleSide::None});
    }
  }

  // En passant (not filtered by targetMask; correctness first). Respect pins.
  if (st.enPassantSquare != core::NO_SQUARE) {
    const core::Square epSq = st.enPassantSquare;
    const bb::Bitboard ep = bb::sq_bb(epSq);
    if constexpr (W) {
      bb::Bitboard froms = (bb::sw(ep) | bb::se(ep)) & our.pawns;
      for (bb::Bitboard f = froms; f;) {
        const core::Square from = bb::pop_lsb(f);
        const bb::Bitboard fromMask = bb::sq_bb(from);
        const bool pinned = pins && (pins->pinned & fromMask);
        const bb::Bitboard allowMask = pinned ? pins->allow_mask(from) : ~0ULL;
        if (!(bb::sq_bb(epSq) & allowMask)) continue;
        if (ep_is_legal_fast(board, Side, from, epSq))
          emit(Move{from, epSq, PT::None, true, true, CastleSide::None});
      }
    } else {
      bb::Bitboard froms = (bb::nw(ep) | bb::ne(ep)) & our.pawns;
      for (bb::Bitboard f = froms; f;) {
        const core::Square from = bb::pop_lsb(f);
        const bb::Bitboard fromMask = bb::sq_bb(from);
        const bool pinned = pins && (pins->pinned & fromMask);
        const bb::Bitboard allowMask = pinned ? pins->allow_mask(from) : ~0ULL;
        if (!(bb::sq_bb(epSq) & allowMask)) continue;
        if (ep_is_legal_fast(board, Side, from, epSq))
          emit(Move{from, epSq, PT::None, true, true, CastleSide::None});
      }
    }
  }
}

template <class Emit>
LILIA_ALWAYS_INLINE void genKnightMoves_T(const SideSets& our, const SideSets& opp,
                                          bb::Bitboard occ, const PinInfo* pins, Emit&& emit,
                                          bb::Bitboard targetMask = ~0ULL) noexcept {
  if (!our.knights) return;

  const bb::Bitboard enemyNoK = opp.noKing;

  for (bb::Bitboard n = our.knights; n;) {
    const core::Square from = bb::pop_lsb(n);
    const bb::Bitboard pinMask =
        pins && (pins->pinned & bb::sq_bb(from)) ? pins->allow_mask(from) : ~0ULL;
    const bb::Bitboard atk = (bb::knight_attacks_from(from) & targetMask) & pinMask;
    for (bb::Bitboard caps = atk & enemyNoK; caps;) {
      emit(Move{from, bb::pop_lsb(caps), PT::None, true, false, CastleSide::None});
    }
    for (bb::Bitboard quiet = atk & ~occ; quiet;) {
      emit(Move{from, bb::pop_lsb(quiet), PT::None, false, false, CastleSide::None});
    }
  }
}

template <class Emit>
LILIA_ALWAYS_INLINE void genBishopMoves_T(const SideSets& our, const SideSets& opp,
                                          bb::Bitboard occ, const PinInfo* pins, Emit&& emit,
                                          bb::Bitboard targetMask = ~0ULL) noexcept {
  if (!our.bishops) return;

  const bb::Bitboard enemyNoK = opp.noKing;

  for (bb::Bitboard bbs = our.bishops; bbs;) {
    const core::Square from = bb::pop_lsb(bbs);
    const bb::Bitboard pinMask =
        pins && (pins->pinned & bb::sq_bb(from)) ? pins->allow_mask(from) : ~0ULL;
    const bb::Bitboard atk =
        (magic::sliding_attacks(magic::Slider::Bishop, from, occ) & targetMask) & pinMask;
    for (bb::Bitboard caps = atk & enemyNoK; caps;) {
      emit(Move{from, bb::pop_lsb(caps), PT::None, true, false, CastleSide::None});
    }
    for (bb::Bitboard quiet = atk & ~occ; quiet;) {
      emit(Move{from, bb::pop_lsb(quiet), PT::None, false, false, CastleSide::None});
    }
  }
}

template <class Emit>
LILIA_ALWAYS_INLINE void genRookMoves_T(const SideSets& our, const SideSets& opp, bb::Bitboard occ,
                                        const PinInfo* pins, Emit&& emit,
                                        bb::Bitboard targetMask = ~0ULL) noexcept {
  if (!our.rooks) return;

  const bb::Bitboard enemyNoK = opp.noKing;

  for (bb::Bitboard r = our.rooks; r;) {
    const core::Square from = bb::pop_lsb(r);
    const bb::Bitboard pinMask =
        pins && (pins->pinned & bb::sq_bb(from)) ? pins->allow_mask(from) : ~0ULL;
    const bb::Bitboard atk =
        (magic::sliding_attacks(magic::Slider::Rook, from, occ) & targetMask) & pinMask;
    for (bb::Bitboard caps = atk & enemyNoK; caps;) {
      emit(Move{from, bb::pop_lsb(caps), PT::None, true, false, CastleSide::None});
    }
    for (bb::Bitboard quiet = atk & ~occ; quiet;) {
      emit(Move{from, bb::pop_lsb(quiet), PT::None, false, false, CastleSide::None});
    }
  }
}

template <class Emit>
LILIA_ALWAYS_INLINE void genQueenMoves_T(const SideSets& our, const SideSets& opp, bb::Bitboard occ,
                                         const PinInfo* pins, Emit&& emit,
                                         bb::Bitboard targetMask = ~0ULL) noexcept {
  if (!our.queens) return;

  const bb::Bitboard enemyNoK = opp.noKing;

  for (bb::Bitboard q = our.queens; q;) {
    const core::Square from = bb::pop_lsb(q);
    const bb::Bitboard pinMask =
        pins && (pins->pinned & bb::sq_bb(from)) ? pins->allow_mask(from) : ~0ULL;
    const bb::Bitboard atk = ((magic::sliding_attacks(magic::Slider::Bishop, from, occ) |
                               magic::sliding_attacks(magic::Slider::Rook, from, occ)) &
                              targetMask) &
                             pinMask;
    for (bb::Bitboard caps = atk & enemyNoK; caps;) {
      emit(Move{from, bb::pop_lsb(caps), PT::None, true, false, CastleSide::None});
    }
    for (bb::Bitboard quiet = atk & ~occ; quiet;) {
      emit(Move{from, bb::pop_lsb(quiet), PT::None, false, false, CastleSide::None});
    }
  }
}

template <class Emit>
LILIA_ALWAYS_INLINE void genKingMoves_T(const Board& board, const GameState& st, Color side,
                                        const SideSets& our, const SideSets& opp, bb::Bitboard occ,
                                        Emit&& emit) noexcept {
  const bb::Bitboard king = our.king;
  if (!king) return;
  const Square from = static_cast<Square>(bb::ctz64(king));

  const bb::Bitboard enemyNoK = opp.noKing;
  const bb::Bitboard atk = bb::king_attacks_from(from);
  const bb::Bitboard fromBB = bb::sq_bb(from);
  const bb::Bitboard occ_no_king = occ & ~fromBB;

  for (bb::Bitboard caps = atk & enemyNoK; caps;) {
    const Square to = bb::pop_lsb(caps);
    const bb::Bitboard occ2 = occ_no_king & ~bb::sq_bb(to);
    if (!attackedBy(board, to, ~side, occ2))
      emit(Move{from, to, PT::None, true, false, CastleSide::None});
  }

  for (bb::Bitboard quiet = atk & ~occ; quiet;) {
    const Square to = bb::pop_lsb(quiet);
    if (!attackedBy(board, to, ~side, occ_no_king))
      emit(Move{from, to, PT::None, false, false, CastleSide::None});
  }

  const Color enemySide = ~side;
  if (side == Color::White) {
    if ((st.castlingRights & bb::Castling::WK) && (our.rooks & bb::sq_bb(bb::H1)) &&
        !(occ & (bb::sq_bb(Square{5}) | bb::sq_bb(Square{6})))) {
      if (!attackedBy(board, Square{4}, enemySide, occ) &&
          !attackedBy(board, Square{5}, enemySide, occ) &&
          !attackedBy(board, Square{6}, enemySide, occ)) {
        emit(Move{bb::E1, Square{6}, PT::None, false, false, CastleSide::KingSide});
      }
    }
    if ((st.castlingRights & bb::Castling::WQ) && (our.rooks & bb::sq_bb(bb::A1)) &&
        !(occ & (bb::sq_bb(Square{3}) | bb::sq_bb(Square{2}) | bb::sq_bb(Square{1})))) {
      if (!attackedBy(board, Square{4}, enemySide, occ) &&
          !attackedBy(board, Square{3}, enemySide, occ) &&
          !attackedBy(board, Square{2}, enemySide, occ)) {
        emit(Move{bb::E1, Square{2}, PT::None, false, false, CastleSide::QueenSide});
      }
    }
  } else {
    if ((st.castlingRights & bb::Castling::BK) && (our.rooks & bb::sq_bb(bb::H8)) &&
        !(occ & (bb::sq_bb(Square{61}) | bb::sq_bb(Square{62})))) {
      if (!attackedBy(board, Square{60}, enemySide, occ) &&
          !attackedBy(board, Square{61}, enemySide, occ) &&
          !attackedBy(board, Square{62}, enemySide, occ)) {
        emit(Move{bb::E8, Square{62}, PT::None, false, false, CastleSide::KingSide});
      }
    }
    if ((st.castlingRights & bb::Castling::BQ) && (our.rooks & bb::sq_bb(bb::A8)) &&
        !(occ & (bb::sq_bb(Square{59}) | bb::sq_bb(Square{58}) | bb::sq_bb(Square{57})))) {
      if (!attackedBy(board, Square{60}, enemySide, occ) &&
          !attackedBy(board, Square{59}, enemySide, occ) &&
          !attackedBy(board, Square{58}, enemySide, occ)) {
        emit(Move{bb::E8, Square{58}, PT::None, false, false, CastleSide::QueenSide});
      }
    }
  }
}

// ---------- Evasions (robust + early pin filtering inside gens) ----------
template <class Emit>
LILIA_ALWAYS_INLINE void generateEvasions_T(const Board& b, const GameState& st,
                                            const PinInfo* pins, Emit&& emit) noexcept {
  const core::Color us = st.sideToMove;
  const core::Color them = ~us;

  const bb::Bitboard kbb = b.getPieces(us, core::PieceType::King);
  if (!kbb) return;
  const core::Square ksq = static_cast<core::Square>(bb::ctz64(kbb));

  const bb::Bitboard occ = b.getAllPieces();

  // Compute checkers explicitly (independent of magic inclusion)
  bb::Bitboard checkers = 0ULL;

  if (us == core::Color::White) {
    // black pawns attack the white king from NE/NW
    checkers |= (bb::nw(bb::sq_bb(ksq)) | bb::ne(bb::sq_bb(ksq))) &
                b.getPieces(core::Color::Black, core::PieceType::Pawn);
  } else {
    // white pawns attack the black king from SW/SE
    checkers |= (bb::sw(bb::sq_bb(ksq)) | bb::se(bb::sq_bb(ksq))) &
                b.getPieces(core::Color::White, core::PieceType::Pawn);
  }

  checkers |= bb::knight_attacks_from(ksq) & b.getPieces(them, core::PieceType::Knight);

  const bb::Bitboard oppBQ = b.getPieces(them, PT::Bishop) | b.getPieces(them, PT::Queen);
  for (bb::Bitboard bs = oppBQ; bs;) {
    const Square s = bb::pop_lsb(bs);
    if (!aligned_diag(ksq, s)) continue;
    if ((squares_between(ksq, s) & occ) == 0ULL) checkers |= bb::sq_bb(s);
  }
  const bb::Bitboard oppRQ = b.getPieces(them, PT::Rook) | b.getPieces(them, PT::Queen);
  for (bb::Bitboard rs = oppRQ; rs;) {
    const Square s = bb::pop_lsb(rs);
    if (!aligned_ortho(ksq, s)) continue;
    if ((squares_between(ksq, s) & occ) == 0ULL) checkers |= bb::sq_bb(s);
  }

  const int numCheckers = bb::popcount(checkers);

  // King moves first
  {
    const bb::Bitboard enemyNoK = b.getPieces(them) & ~b.getPieces(them, PT::King);
    const bb::Bitboard atk = bb::king_attacks_from(ksq);
    const bb::Bitboard fromBB = bb::sq_bb(ksq);
    const bb::Bitboard occ_no_king = (occ & ~fromBB);

    for (bb::Bitboard caps = atk & enemyNoK; caps;) {
      const core::Square to = bb::pop_lsb(caps);
      const bb::Bitboard occ2 = (occ_no_king & ~bb::sq_bb(to));
      if (!attackedBy(b, to, them, occ2))
        emit(Move{ksq, to, PT::None, true, false, CastleSide::None});
    }
    for (bb::Bitboard quiet = atk & ~occ; quiet;) {
      const core::Square to = bb::pop_lsb(quiet);
      if (!attackedBy(b, to, them, occ_no_king))
        emit(Move{ksq, to, PT::None, false, false, CastleSide::None});
    }
  }

  if (numCheckers >= 2) return;  // only king moves allowed

  // Block/capture single checker
  bb::Bitboard blockMask = 0ULL;
  if (numCheckers == 1) {
    const core::Square checkerSq = static_cast<core::Square>(bb::ctz64(checkers));
    blockMask = squares_between(ksq, checkerSq);
  }
  const bb::Bitboard evasionTargets = checkers | blockMask;

  // Masked generation for non-king moves (with pins applied inside gens)
  const SideSets our = side_sets(b, us);
  const SideSets opp = side_sets(b, them);

  if (us == core::Color::White)
    genPawnMoves_T<core::Color::White>(
        b, st, occ, our, opp, pins,
        [&](const Move& m) noexcept {
          if (bb::sq_bb(m.to()) & evasionTargets) emit(m);
        },
        evasionTargets);
  else
    genPawnMoves_T<core::Color::Black>(
        b, st, occ, our, opp, pins,
        [&](const Move& m) noexcept {
          if (bb::sq_bb(m.to()) & evasionTargets) emit(m);
        },
        evasionTargets);

  genKnightMoves_T(
      our, opp, occ, pins,
      [&](const Move& m) noexcept {
        if (bb::sq_bb(m.to()) & evasionTargets) emit(m);
      },
      evasionTargets);
  genBishopMoves_T(
      our, opp, occ, pins,
      [&](const Move& m) noexcept {
        if (bb::sq_bb(m.to()) & evasionTargets) emit(m);
      },
      evasionTargets);
  genRookMoves_T(
      our, opp, occ, pins,
      [&](const Move& m) noexcept {
        if (bb::sq_bb(m.to()) & evasionTargets) emit(m);
      },
      evasionTargets);
  genQueenMoves_T(
      our, opp, occ, pins,
      [&](const Move& m) noexcept {
        if (bb::sq_bb(m.to()) & evasionTargets) emit(m);
      },
      evasionTargets);

  // EP-evasion (strict)
  if (st.enPassantSquare != core::NO_SQUARE) {
    const core::Square epSq = st.enPassantSquare;
    const bb::Bitboard ep = bb::sq_bb(epSq);
    if (us == core::Color::White) {
      for (bb::Bitboard f = ((bb::sw(ep) | bb::se(ep)) & our.pawns); f;) {
        const core::Square from = bb::pop_lsb(f);
        const bb::Bitboard fromMask = bb::sq_bb(from);
        const bool pinned = pins && (pins->pinned & fromMask);
        const bb::Bitboard allowMask = pinned ? pins->allow_mask(from) : ~0ULL;
        if (!(bb::sq_bb(epSq) & allowMask)) continue;
        const core::Square capSq = static_cast<core::Square>((int)epSq - 8);
        bb::Bitboard occAfter = (occ & ~bb::sq_bb(from) & ~bb::sq_bb(capSq)) | bb::sq_bb(epSq);
        if (!attackedBy(b, ksq, them, occAfter))
          emit(Move{from, epSq, PT::None, true, true, CastleSide::None});
      }
    } else {
      for (bb::Bitboard f = ((bb::nw(ep) | bb::ne(ep)) & our.pawns); f;) {
        const core::Square from = bb::pop_lsb(f);
        const bb::Bitboard fromMask = bb::sq_bb(from);
        const bool pinned = pins && (pins->pinned & fromMask);
        const bb::Bitboard allowMask = pinned ? pins->allow_mask(from) : ~0ULL;
        if (!(bb::sq_bb(epSq) & allowMask)) continue;
        const core::Square capSq = static_cast<core::Square>((int)epSq + 8);
        bb::Bitboard occAfter = (occ & ~bb::sq_bb(from) & ~bb::sq_bb(capSq)) | bb::sq_bb(epSq);
        if (!attackedBy(b, ksq, them, occAfter))
          emit(Move{from, epSq, PT::None, true, true, CastleSide::None});
      }
    }
  }
}

// ---------------- Accept filters ----------------

struct AcceptAny {
  LILIA_ALWAYS_INLINE bool operator()(const Move&) const noexcept { return true; }
};
struct AcceptCaptures {
  LILIA_ALWAYS_INLINE bool operator()(const Move& m) const noexcept {
    return m.isCapture() || m.promotion() != PT::None;
  }
};

template <class Emit, class Accept>
LILIA_ALWAYS_INLINE void generate_all_regular(const Board& b, const GameState& st, Emit&& emit,
                                              Accept&& accept) noexcept {
  const Color side = st.sideToMove;

  const bb::Bitboard occ = b.getAllPieces();
  const SideSets our = side_sets(b, side);
  const SideSets opp = side_sets(b, ~side);

  PinInfo pins;
  compute_pins(b, side, occ, pins);

  const bb::Bitboard kbb = our.king;
  const Square ksq = kbb ? static_cast<Square>(bb::ctz64(kbb)) : core::NO_SQUARE;

  auto gated_emit = [&](const Move& m) noexcept {
    // King moves are never filtered by pins; others already pin-filtered in gens.
    if (m.from() == ksq || (pins.pinned & bb::sq_bb(m.from())) == 0ULL ||
        (pins.allow_mask(m.from()) & bb::sq_bb(m.to())))
      if (accept(m)) emit(m);
  };

  if (side == Color::White)
    genPawnMoves_T<Color::White>(b, st, occ, our, opp, &pins, gated_emit);
  else
    genPawnMoves_T<Color::Black>(b, st, occ, our, opp, &pins, gated_emit);

  genKnightMoves_T(our, opp, occ, &pins, gated_emit);
  genBishopMoves_T(our, opp, occ, &pins, gated_emit);
  genRookMoves_T(our, opp, occ, &pins, gated_emit);
  genQueenMoves_T(our, opp, occ, &pins, gated_emit);
  genKingMoves_T(b, st, side, our, opp, occ, gated_emit);
}

}  // namespace

//-- -- -- -- -- -- -- --Public APIs-- -- -- -- -- -- -- --

void MoveGenerator::generateNonCapturePromotions(const Board& b, const GameState& st,
                                                 std::vector<model::Move>& out) const {
  if (out.capacity() < 16) out.reserve(16);
  out.clear();

  using PT = core::PieceType;
  constexpr PT promoOrder[4] = {PT::Queen, PT::Rook, PT::Bishop, PT::Knight};

  const core::Color side = st.sideToMove;
  const bb::Bitboard occ = b.getAllPieces();
  const bb::Bitboard pawns = b.getPieces(side, PT::Pawn);
  const bb::Bitboard empty = ~occ;

  // Pin mask (same mechanism you use elsewhere)
  PinInfo pins;
  compute_pins(b, side, occ, pins);

  if (side == core::Color::White) {
    const bb::Bitboard one = bb::north(pawns) & empty;
    bb::Bitboard promoPush = one & bb::RANK_8;
    while (promoPush) {
      const core::Square to = bb::pop_lsb(promoPush);
      const core::Square from = static_cast<core::Square>(to - 8);
      const bb::Bitboard fromMask = bb::sq_bb(from);
      const bool pinned = (pins.pinned & fromMask) != 0ULL;
      const bb::Bitboard allowMask = pinned ? pins.allow_mask(from) : ~0ULL;
      if (!(bb::sq_bb(to) & allowMask)) continue;

      for (int i = 0; i < 4; ++i)
        out.push_back(
            Move{from, to, promoOrder[i], /*isCap*/ false, /*isEP*/ false, CastleSide::None});
    }
  } else {
    const bb::Bitboard one = bb::south(pawns) & empty;
    bb::Bitboard promoPush = one & bb::RANK_1;
    while (promoPush) {
      const core::Square to = bb::pop_lsb(promoPush);
      const core::Square from = static_cast<core::Square>(to + 8);
      const bb::Bitboard fromMask = bb::sq_bb(from);
      const bool pinned = (pins.pinned & fromMask) != 0ULL;
      const bb::Bitboard allowMask = pinned ? pins.allow_mask(from) : ~0ULL;
      if (!(bb::sq_bb(to) & allowMask)) continue;

      for (int i = 0; i < 4; ++i)
        out.push_back(
            Move{from, to, promoOrder[i], /*isCap*/ false, /*isEP*/ false, CastleSide::None});
    }
  }
}

int MoveGenerator::generateNonCapturePromotions(const Board& b, const GameState& st,
                                                engine::MoveBuffer& buf) {
  using PT = core::PieceType;
  constexpr PT promoOrder[4] = {PT::Queen, PT::Rook, PT::Bishop, PT::Knight};

  const int before = buf.n;

  const core::Color side = st.sideToMove;
  const bb::Bitboard occ = b.getAllPieces();
  const bb::Bitboard pawns = b.getPieces(side, PT::Pawn);
  const bb::Bitboard empty = ~occ;

  PinInfo pins;
  compute_pins(b, side, occ, pins);

  if (side == core::Color::White) {
    const bb::Bitboard one = bb::north(pawns) & empty;
    bb::Bitboard promoPush = one & bb::RANK_8;
    while (promoPush) {
      const core::Square to = bb::pop_lsb(promoPush);
      const core::Square from = static_cast<core::Square>(to - 8);
      const bb::Bitboard fromMask = bb::sq_bb(from);
      const bool pinned = (pins.pinned & fromMask) != 0ULL;
      const bb::Bitboard allowMask = pinned ? pins.allow_mask(from) : ~0ULL;
      if (!(bb::sq_bb(to) & allowMask)) continue;

      for (int i = 0; i < 4; ++i)
        buf.push_unchecked(
            Move{from, to, promoOrder[i], /*isCap*/ false, /*isEP*/ false, CastleSide::None});
    }
  } else {
    const bb::Bitboard one = bb::south(pawns) & empty;
    bb::Bitboard promoPush = one & bb::RANK_1;
    while (promoPush) {
      const core::Square to = bb::pop_lsb(promoPush);
      const core::Square from = static_cast<core::Square>(to + 8);
      const bb::Bitboard fromMask = bb::sq_bb(from);
      const bool pinned = (pins.pinned & fromMask) != 0ULL;
      const bb::Bitboard allowMask = pinned ? pins.allow_mask(from) : ~0ULL;
      if (!(bb::sq_bb(to) & allowMask)) continue;

      for (int i = 0; i < 4; ++i)
        buf.push_unchecked(
            Move{from, to, promoOrder[i], /*isCap*/ false, /*isEP*/ false, CastleSide::None});
    }
  }

  return buf.n - before;
}

void MoveGenerator::generatePseudoLegalMoves(const Board& b, const GameState& st,
                                             std::vector<model::Move>& out) const {
  if (out.capacity() < 128) out.reserve(128);
  out.clear();
  auto sink = [&](const Move& m) { out.push_back(m); };
  generate_all_regular(b, st, sink, AcceptAny{});
}
int MoveGenerator::generatePseudoLegalMoves(const Board& b, const GameState& st,
                                            engine::MoveBuffer& buf) {
  auto sink = [&](const Move& m) { buf.push_unchecked(m); };
  generate_all_regular(b, st, sink, AcceptAny{});
  return buf.n;
}
void MoveGenerator::generateCapturesOnly(const Board& b, const GameState& st,
                                         std::vector<model::Move>& out) const {
  if (out.capacity() < 64) out.reserve(64);
  out.clear();
  auto sink = [&](const Move& m) { out.push_back(m); };
  generate_all_regular(b, st, sink, AcceptCaptures{});
}
int MoveGenerator::generateCapturesOnly(const Board& b, const GameState& st,
                                        engine::MoveBuffer& buf) {
  auto sink = [&](const Move& m) { buf.push_unchecked(m); };
  generate_all_regular(b, st, sink, AcceptCaptures{});
  return buf.n;
}
void MoveGenerator::generateEvasions(const Board& b, const GameState& st,
                                     std::vector<model::Move>& out) const {
  if (out.capacity() < 48) out.reserve(48);
  out.clear();
  const Color side = st.sideToMove;
  const bb::Bitboard occ = b.getAllPieces();
  PinInfo pins;
  compute_pins(b, side, occ, pins);
  auto emitV = [&](const Move& m) noexcept {
    // gens already pin - filter non - king moves via pins
    out.push_back(m);
  };
  generateEvasions_T(b, st, &pins, emitV);
}
int MoveGenerator::generateEvasions(const Board& b, const GameState& st, engine::MoveBuffer& buf) {
  const Color side = st.sideToMove;
  const bb::Bitboard occ = b.getAllPieces();
  PinInfo pins;
  compute_pins(b, side, occ, pins);
  auto emitB = [&](const Move& m) noexcept { buf.push_unchecked(m); };
  generateEvasions_T(b, st, &pins, emitB);
  return buf.n;
}

}  // namespace lilia::model
