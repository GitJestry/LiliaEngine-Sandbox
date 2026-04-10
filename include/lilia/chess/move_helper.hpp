#pragma once
#include "chess_types.hpp"
#include "board.hpp"
#include "core/bitboard.hpp"
#include "core/magic.hpp"

namespace lilia::chess
{
  namespace attack_detail
  {
    [[nodiscard]] LILIA_ALWAYS_INLINE bb::Bitboard pawn_attackers_to(Square sq, Color by,
                                                                     bb::Bitboard pawns) noexcept
    {
      const bb::Bitboard t = bb::sq_bb(sq);
      return by == Color::White ? ((bb::sw(t) | bb::se(t)) & pawns)
                                : ((bb::nw(t) | bb::ne(t)) & pawns);
    }
  }

  [[nodiscard]] LILIA_ALWAYS_INLINE bb::Bitboard attackersTo(const Board &b, Square sq, Color by,
                                                             bb::Bitboard occ) noexcept
  {
    const bb::Bitboard target = bb::sq_bb(sq);
    const bb::Bitboard occNoTarget = occ & ~target;

    bb::Bitboard attackers = 0;

    attackers |= attack_detail::pawn_attackers_to(sq, by, b.getPieces(by, PieceType::Pawn));
    attackers |= bb::knight_attacks_from(sq) & b.getPieces(by, PieceType::Knight);
    attackers |= bb::king_attacks_from(sq) & b.getPieces(by, PieceType::King);

    const bb::Bitboard queens = b.getPieces(by, PieceType::Queen);

    const bb::Bitboard diagSliders = b.getPieces(by, PieceType::Bishop) | queens;
    if (diagSliders)
      attackers |= magic::sliding_attacks(magic::Slider::Bishop, sq, occNoTarget) & diagSliders;

    const bb::Bitboard orthoSliders = b.getPieces(by, PieceType::Rook) | queens;
    if (orthoSliders)
      attackers |= magic::sliding_attacks(magic::Slider::Rook, sq, occNoTarget) & orthoSliders;

    return attackers;
  }

  [[nodiscard]] LILIA_ALWAYS_INLINE bool attackedBy(const Board &b, Square sq, Color by,
                                                    bb::Bitboard occ) noexcept
  {
    return attackersTo(b, sq, by, occ) != 0;
  }

}
