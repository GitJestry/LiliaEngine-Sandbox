#pragma once
#include "chess_types.hpp"
#include "board.hpp"
#include "core/bitboard.hpp"
#include "core/magic.hpp"

namespace lilia::chess
{

  // ---------------- Attack query ----------------
  static LILIA_ALWAYS_INLINE bool attackedBy(const Board &b, Square sq, Color by,
                                             core::Bitboard occ) noexcept
  {
    const core::Bitboard target = core::sq_bb(sq);
    const core::Bitboard occ2 = occ & ~target; // do not let the target piece block rays

    // Pawns: squares from which a pawn of 'by' attacks 'sq'
    const core::Bitboard pawns = b.getPieces(by, PieceType::Pawn);
    const core::Bitboard pawnFrom = (by == Color::White) ? (core::sw(target) | core::se(target))
                                                         : (core::nw(target) | core::ne(target));
    if (pawnFrom & pawns)
      return true;

    // Knights
    const core::Bitboard kn = b.getPieces(by, PieceType::Knight);
    if (core::knight_attacks_from(sq) & kn)
      return true;

    // King (cheap; helps king-move legality and castling checks)
    const core::Bitboard k = b.getPieces(by, PieceType::King);
    if (core::king_attacks_from(sq) & k)
      return true;

    // Sliders: fetch queen once, reuse
    const core::Bitboard q = b.getPieces(by, PieceType::Queen);

    // Diagonal sliders (B/Q)
    const core::Bitboard bq = b.getPieces(by, PieceType::Bishop) | q;
    if (bq)
    {
      const core::Bitboard diag = magic::sliding_attacks(magic::Slider::Bishop, sq, occ2);
      if (diag & bq)
        return true;
    }

    // Orthogonal sliders (R/Q)
    const core::Bitboard rq = b.getPieces(by, PieceType::Rook) | q;
    if (rq)
    {
      const core::Bitboard ortho = magic::sliding_attacks(magic::Slider::Rook, sq, occ2);
      if (ortho & rq)
        return true;
    }

    return false;
  }

} // namespace lilia::model
