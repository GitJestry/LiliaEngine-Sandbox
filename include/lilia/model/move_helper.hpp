#pragma once
#include "../chess_types.hpp"
#include "board.hpp"
#include "core/bitboard.hpp"
#include "core/magic.hpp"

#if defined(__GNUC__) || defined(__clang__)
#define LILIA_ALWAYS_INLINE inline __attribute__((always_inline))
#else
#define LILIA_ALWAYS_INLINE inline
#endif

namespace lilia::model {

// ---------------- Attack query ----------------
static LILIA_ALWAYS_INLINE bool attackedBy(const Board& b, core::Square sq, core::Color by,
                                           bb::Bitboard occ) noexcept {
  const bb::Bitboard target = bb::sq_bb(sq);
  const bb::Bitboard occ2 = occ & ~target;  // do not let the target piece block rays

  // Pawns: squares from which a pawn of 'by' attacks 'sq'
  const bb::Bitboard pawns = b.getPieces(by, core::PieceType::Pawn);
  const bb::Bitboard pawnFrom = (by == core::Color::White) ? (bb::sw(target) | bb::se(target))
                                                           : (bb::nw(target) | bb::ne(target));
  if (pawnFrom & pawns) return true;

  // Knights
  const bb::Bitboard kn = b.getPieces(by, core::PieceType::Knight);
  if (bb::knight_attacks_from(sq) & kn) return true;

  // King (cheap; helps king-move legality and castling checks)
  const bb::Bitboard k = b.getPieces(by, core::PieceType::King);
  if (bb::king_attacks_from(sq) & k) return true;

  // Sliders: fetch queen once, reuse
  const bb::Bitboard q = b.getPieces(by, core::PieceType::Queen);

  // Diagonal sliders (B/Q)
  const bb::Bitboard bq = b.getPieces(by, core::PieceType::Bishop) | q;
  if (bq) {
    const bb::Bitboard diag = magic::sliding_attacks(magic::Slider::Bishop, sq, occ2);
    if (diag & bq) return true;
  }

  // Orthogonal sliders (R/Q)
  const bb::Bitboard rq = b.getPieces(by, core::PieceType::Rook) | q;
  if (rq) {
    const bb::Bitboard ortho = magic::sliding_attacks(magic::Slider::Rook, sq, occ2);
    if (ortho & rq) return true;
  }

  return false;
}

}  // namespace lilia::model
