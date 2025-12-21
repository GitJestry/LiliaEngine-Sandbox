
#pragma once
#include "../chess_types.hpp"
#include "board.hpp"
#include "core/bitboard.hpp"
#include "core/magic.hpp"

namespace lilia::model {
// ---------------- Angriffsabfrage ----------------

static inline bool attackedBy(const Board& b, core::Square sq, core::Color by,
                              bb::Bitboard occ) noexcept {
  const bb::Bitboard target = bb::sq_bb(sq);
  occ &= ~target;

  // Pawns
  const bb::Bitboard pawns = b.getPieces(by, core::PieceType::Pawn);
  const bb::Bitboard pawnAtkToSq = (by == core::Color::White) ? (bb::sw(target) | bb::se(target))
                                                              : (bb::nw(target) | bb::ne(target));
  if (pawnAtkToSq & pawns) return true;

  // Knights
  const bb::Bitboard kn = b.getPieces(by, core::PieceType::Knight);
  if (kn && (bb::knight_attacks_from(sq) & kn)) return true;

  // Diagonal sliders (B/Q)
  const bb::Bitboard bq =
      b.getPieces(by, core::PieceType::Bishop) | b.getPieces(by, core::PieceType::Queen);
  if (bq) {
    const bb::Bitboard diag = magic::sliding_attacks(magic::Slider::Bishop, sq, occ);
    if (diag & bq) return true;
  }

  // Orthogonal sliders (R/Q)
  const bb::Bitboard rq =
      b.getPieces(by, core::PieceType::Rook) | b.getPieces(by, core::PieceType::Queen);
  if (rq) {
    const bb::Bitboard ortho = magic::sliding_attacks(magic::Slider::Rook, sq, occ);
    if (ortho & rq) return true;
  }

  // King
  const bb::Bitboard k = b.getPieces(by, core::PieceType::King);
  return k && (bb::king_attacks_from(sq) & k);
}

}  // namespace lilia::model
