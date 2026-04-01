#pragma once
#include "chess_types.hpp"
#include "board.hpp"
#include "core/bitboard.hpp"
#include "core/magic.hpp"

namespace lilia::chess
{

  [[nodiscard]] LILIA_ALWAYS_INLINE bool attackedBy(const Board &b, Square sq, Color by,
                                                    bb::Bitboard occ) noexcept
  {
    const bb::Bitboard target = bb::sq_bb(sq);
    const bb::Bitboard occ2 = occ & ~target; // do not let the target piece block rays

    const bb::Bitboard pawns = b.getPieces(by, PieceType::Pawn);
    const bb::Bitboard pawnFrom = (by == Color::White) ? (bb::sw(target) | bb::se(target))
                                                       : (bb::nw(target) | bb::ne(target));
    if (pawnFrom & pawns)
      return true;

    const bb::Bitboard kn = b.getPieces(by, PieceType::Knight);
    if (bb::knight_attacks_from(sq) & kn)
      return true;

    const bb::Bitboard k = b.getPieces(by, PieceType::King);
    if (bb::king_attacks_from(sq) & k)
      return true;

    const bb::Bitboard q = b.getPieces(by, PieceType::Queen);

    const bb::Bitboard bq = b.getPieces(by, PieceType::Bishop) | q;
    if (bq)
    {
      const bb::Bitboard diag = magic::sliding_attacks(magic::Slider::Bishop, sq, occ2);
      if (diag & bq)
        return true;
    }

    const bb::Bitboard rq = b.getPieces(by, PieceType::Rook) | q;
    if (rq)
    {
      const bb::Bitboard ortho = magic::sliding_attacks(magic::Slider::Rook, sq, occ2);
      if (ortho & rq)
        return true;
    }

    return false;
  }

}
