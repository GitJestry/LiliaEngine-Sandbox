#pragma once
#include "lilia/engine/see.hpp"

namespace lilia::engine::see
{
  namespace
  {
    constexpr std::array<int, 6> SEE_VALUES = {
        100,  // Pawn
        320,  // Knight
        330,  // Bishop
        500,  // Rook
        900,  // Queen
        20000 // King
    };

    LILIA_ALWAYS_INLINE chess::bb::Bitboard pawn_attackers_to(chess::Square sq, chess::Color by, chess::bb::Bitboard pawns)
    {
      const chess::bb::Bitboard t = chess::bb::sq_bb(sq);
      return by == chess::Color::White ? ((chess::bb::sw(t) | chess::bb::se(t)) & pawns)
                                       : ((chess::bb::nw(t) | chess::bb::ne(t)) & pawns);
    }
  }

  bool non_negative(const chess::Position &pos, const chess::Move &m)
  {

    if (!m.isCapture() && !m.isEnPassant())
      return true;
    auto boardRef = pos.getBoard();
    const auto fromP = boardRef.getPiece(m.from());
    if (!fromP)
      return true;

    const chess::Color us = fromP->color;
    const chess::Color them = chess::Color(~us);
    const chess::Square to = m.to();

    // Snapshot occupancy and piece sets
    chess::bb::Bitboard occ = boardRef.getAllPieces();

    chess::bb::Bitboard pcs[2][6];
    for (int c = 0; c < 2; ++c)
      for (int pt = 0; pt < 6; ++pt)
        pcs[c][pt] = boardRef.getPieces((chess::Color)c, (chess::PieceType)pt);

    auto alive = [&](chess::Color c, chess::PieceType pt) -> chess::bb::Bitboard
    { return pcs[(int)c][(int)pt] & occ; };
    auto val = [&](chess::PieceType pt) -> int
    { return SEE_VALUES[(int)pt]; };

    auto king_sq = [&](chess::Color c) -> chess::Square
    {
      chess::bb::Bitboard kbb = pcs[(int)c][(int)chess::PieceType::King];
      return (chess::Square)chess::bb::ctz64(kbb);
    };

    // Helpers to get all attackers of side 'c' to 'to' given current 'occ'
    auto diag_rays = [&](chess::bb::Bitboard o)
    {
      return chess::magic::sliding_attacks(chess::magic::Slider::Bishop, to, o);
    };
    auto ortho_rays = [&](chess::bb::Bitboard o)
    {
      return chess::magic::sliding_attacks(chess::magic::Slider::Rook, to, o);
    };

    auto pawn_atk = [&](chess::Color c)
    { return pawn_attackers_to(to, c, alive(c, chess::PieceType::Pawn)); };
    auto knight_atk = [&](chess::Color c)
    {
      return chess::bb::knight_attacks_from(to) & alive(c, chess::PieceType::Knight);
    };
    auto bishop_atk = [&](chess::Color c, chess::bb::Bitboard o)
    {
      return diag_rays(o) & alive(c, chess::PieceType::Bishop);
    };
    auto rook_atk = [&](chess::Color c, chess::bb::Bitboard o)
    {
      return ortho_rays(o) & alive(c, chess::PieceType::Rook);
    };
    auto queen_atk = [&](chess::Color c, chess::bb::Bitboard o)
    {
      const chess::bb::Bitboard rays = diag_rays(o) | ortho_rays(o);
      return rays & alive(c, chess::PieceType::Queen);
    };

    // Pin/legal guard: only check when we actually consider that specific attacker
    auto illegal_due_to_pin = [&](chess::Color c, chess::Square fromSq, chess::bb::Bitboard oNow) -> bool
    {
      // Move piece from 'fromSq' to 'to' and see if own king becomes attacked.
      chess::bb::Bitboard kbb = pcs[(int)c][(int)chess::PieceType::King] & oNow;
      if (!kbb)
        return false;
      const chess::Square ksq = (chess::Square)chess::bb::ctz64(kbb);

      // Remove from, add on 'to' (chess::square 'to' remains occupied after a capture)
      chess::bb::Bitboard occTest = (oNow & ~chess::bb::sq_bb(fromSq)) | chess::bb::sq_bb(to);
      return chess::attackedBy(boardRef, ksq, chess::Color(~c), occTest);
    };

    // Pick the least valuable legal attacker for side 'c'. Returns false if none.
    auto pick_lva = [&](chess::Color c, chess::Square &fromSq, chess::PieceType &pt, chess::bb::Bitboard oNow) -> bool
    {
      // Compute rays once for current occupancy
      const chess::bb::Bitboard diag = diag_rays(oNow);
      const chess::bb::Bitboard ortho = ortho_rays(oNow);

      // Order: Pawn, Knight, Bishop, Rook, Queen, King
      chess::bb::Bitboard cand;

      // Pawns
      cand = pawn_atk(c) & alive(c, chess::PieceType::Pawn);
      while (cand)
      {
        chess::Square f = chess::bb::pop_lsb(cand);
        if (!illegal_due_to_pin(c, f, oNow))
        {
          fromSq = f;
          pt = chess::PieceType::Pawn;
          return true;
        }
      }

      // Knights
      cand = knight_atk(c) & alive(c, chess::PieceType::Knight);
      while (cand)
      {
        chess::Square f = chess::bb::pop_lsb(cand);
        if (!illegal_due_to_pin(c, f, oNow))
        {
          fromSq = f;
          pt = chess::PieceType::Knight;
          return true;
        }
      }

      // Bishops
      cand = (diag & alive(c, chess::PieceType::Bishop));
      while (cand)
      {
        chess::Square f = chess::bb::pop_lsb(cand);
        if (!illegal_due_to_pin(c, f, oNow))
        {
          fromSq = f;
          pt = chess::PieceType::Bishop;
          return true;
        }
      }

      // Rooks
      cand = (ortho & alive(c, chess::PieceType::Rook));
      while (cand)
      {
        chess::Square f = chess::bb::pop_lsb(cand);
        if (!illegal_due_to_pin(c, f, oNow))
        {
          fromSq = f;
          pt = chess::PieceType::Rook;
          return true;
        }
      }

      // Queens
      cand = ((diag | ortho) & alive(c, chess::PieceType::Queen));
      while (cand)
      {
        chess::Square f = chess::bb::pop_lsb(cand);
        if (!illegal_due_to_pin(c, f, oNow))
        {
          fromSq = f;
          pt = chess::PieceType::Queen;
          return true;
        }
      }

      // King (check target not covered after king moves there)
      chess::bb::Bitboard kbb = alive(c, chess::PieceType::King);
      if (kbb)
      {
        const chess::Square kf = (chess::Square)chess::bb::ctz64(kbb);
        if (chess::bb::king_attacks_from(kf) & chess::bb::sq_bb(to))
        {
          chess::bb::Bitboard occK = (oNow & ~chess::bb::sq_bb(kf)) | chess::bb::sq_bb(to);
          if (!chess::attackedBy(boardRef, to, chess::Color(~c), occK))
          {
            fromSq = kf;
            pt = chess::PieceType::King;
            return true;
          }
        }
      }
      return false;
    };

    // Identify captured piece and adjust occupancy for initial position at the node
    chess::PieceType captured = chess::PieceType::None;
    if (m.isEnPassant())
    {
      captured = chess::PieceType::Pawn;
      const chess::Square capSq = (us == chess::Color::White) ? chess::Square(int(to) - 8) : chess::Square(int(to) + 8);
      occ &= ~chess::bb::sq_bb(capSq); // remove the pawn that will be taken EP
    }
    else if (auto cap = boardRef.getPiece(to))
    {
      captured = cap->type;
      occ &= ~chess::bb::sq_bb(to);
    }

    // Move our piece to 'to' (occupancy stays with 'to' occupied from this point on)
    occ &= ~chess::bb::sq_bb(m.from());
    chess::PieceType pieceOnTo = (m.promotion() != chess::PieceType::None) ? m.promotion() : fromP->type;
    occ |= chess::bb::sq_bb(to);

    // Swap list
    int gain[32];
    int d = 0;
    gain[d++] = val(captured);

    // Alternate sides starting with the opponent
    chess::Color side = them;

    // Iteratively “exchange” on 'to'
    for (;;)
    {
      chess::Square from2 = chess::NO_SQUARE;
      chess::PieceType pt2 = chess::PieceType::None;

      if (!pick_lva(side, from2, pt2, occ))
        break;

      // They take what's on 'to'
      gain[d] = val(pieceOnTo) - gain[d - 1];
      ++d;

      // Prune: if this side is failing already, no need to go deeper
      if (gain[d - 1] < 0)
        break;

      // Move attacker onto 'to' (remove it from its square; 'to' stays occupied)
      occ &= ~chess::bb::sq_bb(from2);

      // New piece now sits on 'to'
      pieceOnTo = pt2;
      side = chess::Color(~side);

      if (d >= 31)
        break; // sanity guard
    }

    // Negamax backpropagation
    while (--d)
      gain[d - 1] = std::max(-gain[d], gain[d - 1]);

    return gain[0] >= 0;
  }
}
