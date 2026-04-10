#include "lilia/engine/see.hpp"
#include "lilia/chess/core/bitboard.hpp"

namespace lilia::engine::see
{
  namespace
  {
    using BB = chess::bb::Bitboard;

    constexpr std::array<int, 6> SEE_VALUES = {
        100,  // Pawn
        320,  // Knight
        330,  // Bishop
        500,  // Rook
        900,  // Queen
        20000 // King
    };

    [[nodiscard]] LILIA_ALWAYS_INLINE int see_value(chess::PieceType pt) noexcept
    {
      return SEE_VALUES[chess::bb::type_index(pt)];
    }

    [[nodiscard]] LILIA_ALWAYS_INLINE BB pawn_attackers_to(chess::Square sq, chess::Color by,
                                                           BB pawns) noexcept
    {
      const BB t = chess::bb::sq_bb(sq);
      return by == chess::Color::White ? ((chess::bb::sw(t) | chess::bb::se(t)) & pawns)
                                       : ((chess::bb::nw(t) | chess::bb::ne(t)) & pawns);
    }

    [[nodiscard]] LILIA_ALWAYS_INLINE chess::Square king_square(chess::Color c,
                                                                const BB pcs[2][6]) noexcept
    {
      const BB kbb = pcs[chess::bb::ci(c)][chess::bb::type_index((chess::PieceType::King))];
      return kbb ? static_cast<chess::Square>(chess::bb::ctz64(kbb))
                 : chess::NO_SQUARE;
    }

    [[nodiscard]] LILIA_ALWAYS_INLINE BB dyn_attackers_to(chess::Square sq, chess::Color by,
                                                          BB occ,
                                                          const BB pcs[2][6]) noexcept
    {
      const BB target = chess::bb::sq_bb(sq);
      const BB occNoTarget = occ & ~target;
      const int ci = chess::bb::ci(by);

      BB attackers = 0;

      attackers |= pawn_attackers_to(sq, by, pcs[ci][chess::bb::type_index((chess::PieceType::Pawn))]);
      attackers |= chess::bb::knight_attacks_from(sq) & pcs[ci][chess::bb::type_index((chess::PieceType::Knight))];
      attackers |= chess::bb::king_attacks_from(sq) & pcs[ci][chess::bb::type_index((chess::PieceType::King))];

      const BB diagSliders =
          pcs[ci][chess::bb::type_index((chess::PieceType::Bishop))] | pcs[ci][chess::bb::type_index((chess::PieceType::Queen))];
      if (diagSliders)
        attackers |= chess::magic::sliding_attacks(chess::magic::Slider::Bishop, sq, occNoTarget) &
                     diagSliders;

      const BB orthoSliders =
          pcs[ci][chess::bb::type_index((chess::PieceType::Rook))] | pcs[ci][chess::bb::type_index((chess::PieceType::Queen))];
      if (orthoSliders)
        attackers |= chess::magic::sliding_attacks(chess::magic::Slider::Rook, sq, occNoTarget) &
                     orthoSliders;

      return attackers;
    }

    [[nodiscard]] LILIA_ALWAYS_INLINE bool dyn_attacked_by(chess::Square sq, chess::Color by,
                                                           BB occ,
                                                           const BB pcs[2][6]) noexcept
    {
      return dyn_attackers_to(sq, by, occ, pcs) != 0;
    }

    [[nodiscard]] LILIA_ALWAYS_INLINE BB pieces_of(chess::Color c,
                                                   const BB pcs[2][6]) noexcept
    {
      const int ci = chess::bb::ci(c);
      return pcs[ci][chess::bb::type_index(chess::PieceType::Pawn)] |
             pcs[ci][chess::bb::type_index(chess::PieceType::Knight)] |
             pcs[ci][chess::bb::type_index(chess::PieceType::Bishop)] |
             pcs[ci][chess::bb::type_index(chess::PieceType::Rook)] |
             pcs[ci][chess::bb::type_index(chess::PieceType::Queen)] |
             pcs[ci][chess::bb::type_index(chess::PieceType::King)];
    }

    [[nodiscard]] LILIA_ALWAYS_INLINE bool aligned(chess::Square a,
                                                   chess::Square b,
                                                   chess::Square c) noexcept
    {
      const int af = chess::bb::file_of(a), ar = chess::bb::rank_of(a);
      const int bf = chess::bb::file_of(b), br = chess::bb::rank_of(b);
      const int cf = chess::bb::file_of(c), cr = chess::bb::rank_of(c);

      return (af == bf && bf == cf) ||
             (ar == br && br == cr) ||
             ((af - ar) == (bf - br) && (bf - br) == (cf - cr)) ||
             ((af + ar) == (bf + br) && (bf + br) == (cf + cr));
    }

    [[nodiscard]] LILIA_ALWAYS_INLINE BB between_exclusive(chess::Square a,
                                                           chess::Square b) noexcept
    {
      const int af = chess::bb::file_of(a), ar = chess::bb::rank_of(a);
      const int bf = chess::bb::file_of(b), br = chess::bb::rank_of(b);

      int step = 0;

      if (af == bf)
        step = (br > ar) ? 8 : -8;
      else if (ar == br)
        step = (bf > af) ? 1 : -1;
      else if ((bf - af) == (br - ar))
        step = (bf > af) ? 9 : -9;
      else if ((bf - af) == -(br - ar))
        step = (bf > af) ? -7 : 7;
      else
        return 0;

      BB btb = 0;
      for (int s = int(a) + step; s != int(b); s += step)
        btb |= chess::bb::sq_bb(static_cast<chess::Square>(s));

      return btb;
    }

    [[nodiscard]] LILIA_ALWAYS_INLINE BB pinned_mask(chess::Color side,
                                                     BB occ,
                                                     const BB pcs[2][6],
                                                     chess::Square ksq) noexcept
    {
      if (ksq == chess::NO_SQUARE)
        return 0;

      const int them = chess::bb::ci(chess::Color(~side));
      const BB own = pieces_of(side, pcs);

      BB pinned = 0;

      BB snipers =
          chess::magic::sliding_attacks(chess::magic::Slider::Bishop, ksq, 0) &
          (pcs[them][chess::bb::type_index(chess::PieceType::Bishop)] |
           pcs[them][chess::bb::type_index(chess::PieceType::Queen)]);

      while (snipers)
      {
        const chess::Square s = chess::bb::pop_lsb_unchecked(snipers);
        const BB blockers = between_exclusive(ksq, s) & occ;
        if (blockers && !(blockers & (blockers - 1)) && (blockers & own))
          pinned |= blockers;
      }

      snipers =
          chess::magic::sliding_attacks(chess::magic::Slider::Rook, ksq, 0) &
          (pcs[them][chess::bb::type_index(chess::PieceType::Rook)] |
           pcs[them][chess::bb::type_index(chess::PieceType::Queen)]);

      while (snipers)
      {
        const chess::Square s = chess::bb::pop_lsb_unchecked(snipers);
        const BB blockers = between_exclusive(ksq, s) & occ;
        if (blockers && !(blockers & (blockers - 1)) && (blockers & own))
          pinned |= blockers;
      }

      return pinned;
    }

    [[nodiscard]] LILIA_ALWAYS_INLINE bool legal_king_see_capture(chess::Color side,
                                                                  chess::Square fromSq,
                                                                  chess::Square toSq,
                                                                  BB occ,
                                                                  const BB pcs[2][6]) noexcept
    {
      const int them = chess::bb::ci(chess::Color(~side));
      const BB fromBB = chess::bb::sq_bb(fromSq);
      const BB toBB = chess::bb::sq_bb(toSq);

      const BB occAfter = (occ & ~fromBB) | toBB;
      const BB occNoTarget = occAfter & ~toBB;

      if (pawn_attackers_to(toSq, chess::Color(~side),
                            pcs[them][chess::bb::type_index(chess::PieceType::Pawn)] & ~toBB))
        return false;

      if (chess::bb::knight_attacks_from(toSq) &
          (pcs[them][chess::bb::type_index(chess::PieceType::Knight)] & ~toBB))
        return false;

      if (chess::bb::king_attacks_from(toSq) &
          (pcs[them][chess::bb::type_index(chess::PieceType::King)] & ~toBB))
        return false;

      const BB diagSliders =
          (pcs[them][chess::bb::type_index(chess::PieceType::Bishop)] |
           pcs[them][chess::bb::type_index(chess::PieceType::Queen)]) &
          ~toBB;
      if (diagSliders &&
          (chess::magic::sliding_attacks(chess::magic::Slider::Bishop, toSq, occNoTarget) &
           diagSliders))
        return false;

      const BB orthoSliders =
          (pcs[them][chess::bb::type_index(chess::PieceType::Rook)] |
           pcs[them][chess::bb::type_index(chess::PieceType::Queen)]) &
          ~toBB;
      if (orthoSliders &&
          (chess::magic::sliding_attacks(chess::magic::Slider::Rook, toSq, occNoTarget) &
           orthoSliders))
        return false;

      return true;
    }

    [[nodiscard]] LILIA_ALWAYS_INLINE bool pick_lva_legal(chess::Color side,
                                                          chess::Square toSq,
                                                          BB occ,
                                                          const BB pcs[2][6],
                                                          const chess::Square kingSq[2],
                                                          chess::Square &fromSq,
                                                          chess::PieceType &pt) noexcept
    {
      const int ci = chess::bb::ci(side);
      const BB target = chess::bb::sq_bb(toSq);
      const BB occNoTarget = occ & ~target;
      const BB pinned = pinned_mask(side, occ, pcs, kingSq[ci]);

      auto legal_nonking = [&](chess::Square s) noexcept -> bool
      {
        return !(pinned & chess::bb::sq_bb(s)) ||
               aligned(s, toSq, kingSq[ci]);
      };

      BB cand = pawn_attackers_to(
          toSq, side,
          pcs[ci][chess::bb::type_index(chess::PieceType::Pawn)]);
      while (cand)
      {
        const chess::Square s = chess::bb::pop_lsb_unchecked(cand);
        if (legal_nonking(s))
        {
          fromSq = s;
          pt = chess::PieceType::Pawn;
          return true;
        }
      }

      cand = chess::bb::knight_attacks_from(toSq) &
             pcs[ci][chess::bb::type_index(chess::PieceType::Knight)];
      while (cand)
      {
        const chess::Square s = chess::bb::pop_lsb_unchecked(cand);
        if (legal_nonking(s))
        {
          fromSq = s;
          pt = chess::PieceType::Knight;
          return true;
        }
      }

      cand = chess::magic::sliding_attacks(chess::magic::Slider::Bishop, toSq, occNoTarget) &
             pcs[ci][chess::bb::type_index(chess::PieceType::Bishop)];
      while (cand)
      {
        const chess::Square s = chess::bb::pop_lsb_unchecked(cand);
        if (legal_nonking(s))
        {
          fromSq = s;
          pt = chess::PieceType::Bishop;
          return true;
        }
      }

      cand = chess::magic::sliding_attacks(chess::magic::Slider::Rook, toSq, occNoTarget) &
             pcs[ci][chess::bb::type_index(chess::PieceType::Rook)];
      while (cand)
      {
        const chess::Square s = chess::bb::pop_lsb_unchecked(cand);
        if (legal_nonking(s))
        {
          fromSq = s;
          pt = chess::PieceType::Rook;
          return true;
        }
      }

      cand = (chess::magic::sliding_attacks(chess::magic::Slider::Bishop, toSq, occNoTarget) |
              chess::magic::sliding_attacks(chess::magic::Slider::Rook, toSq, occNoTarget)) &
             pcs[ci][chess::bb::type_index(chess::PieceType::Queen)];
      while (cand)
      {
        const chess::Square s = chess::bb::pop_lsb_unchecked(cand);
        if (legal_nonking(s))
        {
          fromSq = s;
          pt = chess::PieceType::Queen;
          return true;
        }
      }

      cand = chess::bb::king_attacks_from(toSq) &
             pcs[ci][chess::bb::type_index(chess::PieceType::King)];
      while (cand)
      {
        const chess::Square s = chess::bb::pop_lsb_unchecked(cand);
        if (legal_king_see_capture(side, s, toSq, occ, pcs))
        {
          fromSq = s;
          pt = chess::PieceType::King;
          return true;
        }
      }

      return false;
    }

    [[nodiscard]] LILIA_ALWAYS_INLINE bool is_promotion_square(chess::Color side,
                                                               chess::Square sq) noexcept
    {
      const int rank = chess::bb::rank_of(sq);
      return side == chess::Color::White ? (rank == 7) : (rank == 0);
    }
    [[nodiscard]] int see_reply_gain(chess::Color side,
                                     chess::Square toSq,
                                     chess::PieceType pieceOnTo,
                                     BB occ,
                                     const BB pcs[2][6],
                                     const chess::Square kingSq[2]) noexcept
    {
      // A king on the target square cannot be legally captured in the next ply.
      if (pieceOnTo == chess::PieceType::King)
        return 0;

      chess::Square fromSq = chess::NO_SQUARE;
      chess::PieceType pt = chess::PieceType::None;

      if (!pick_lva_legal(side, toSq, occ, pcs, kingSq, fromSq, pt))
        return 0;

      const BB fromBB = chess::bb::sq_bb(fromSq);
      const BB toBB = chess::bb::sq_bb(toSq);

      auto eval_with = [&](chess::PieceType placedType, int promoBonus) noexcept -> int
      {
        BB nextPcs[2][6];
        for (int c = 0; c < 2; ++c)
          for (int t = 0; t < 6; ++t)
            nextPcs[c][t] = pcs[c][t];

        chess::Square nextKingSq[2] = {kingSq[0], kingSq[1]};
        BB nextOcc = occ & ~fromBB;

        // Remove the piece currently sitting on toSq.
        nextPcs[chess::bb::ci(chess::Color(~side))]
               [chess::bb::type_index((pieceOnTo))] &= ~toBB;

        // Move attacker from fromSq to toSq, with promotion if applicable.
        nextPcs[chess::bb::ci(side)]
               [chess::bb::type_index((pt))] &= ~fromBB;
        nextPcs[chess::bb::ci(side)]
               [chess::bb::type_index((placedType))] |= toBB;

        if (pt == chess::PieceType::King)
          nextKingSq[chess::bb::ci(side)] = toSq;

        const int score =
            see_value(pieceOnTo) + promoBonus -
            see_reply_gain(chess::Color(~side), toSq, placedType, nextOcc, nextPcs, nextKingSq);

        return score > 0 ? score : 0;
      };

      if (pt == chess::PieceType::Pawn && is_promotion_square(side, toSq))
      {
        int best = 0;

        int cand = eval_with(chess::PieceType::Knight,
                             see_value(chess::PieceType::Knight) - see_value(chess::PieceType::Pawn));
        if (cand > best)
          best = cand;

        cand = eval_with(chess::PieceType::Bishop,
                         see_value(chess::PieceType::Bishop) - see_value(chess::PieceType::Pawn));
        if (cand > best)
          best = cand;

        cand = eval_with(chess::PieceType::Rook,
                         see_value(chess::PieceType::Rook) - see_value(chess::PieceType::Pawn));
        if (cand > best)
          best = cand;

        cand = eval_with(chess::PieceType::Queen,
                         see_value(chess::PieceType::Queen) - see_value(chess::PieceType::Pawn));
        if (cand > best)
          best = cand;

        return best;
      }

      return eval_with(pt, 0);
    }

    [[nodiscard]] int see_reply_gain_fast(chess::Color side,
                                          chess::Square toSq,
                                          chess::PieceType pieceOnTo,
                                          BB occ,
                                          const BB pcsIn[2][6],
                                          const chess::Square kingSq[2]) noexcept
    {
      if (pieceOnTo == chess::PieceType::King)
        return 0;

      BB pcs[2][6];
      for (int c = 0; c < 2; ++c)
        for (int t = 0; t < 6; ++t)
          pcs[c][t] = pcsIn[c][t];

      int gain[32];
      int d = 0;

      chess::Color stm = side;
      chess::PieceType victim = pieceOnTo;
      const BB toBB = chess::bb::sq_bb(toSq);

      while (true)
      {
        chess::Square fromSq = chess::NO_SQUARE;
        chess::PieceType pt = chess::PieceType::None;

        if (!pick_lva_legal(stm, toSq, occ, pcs, kingSq, fromSq, pt))
          break;

        gain[d] = see_value(victim) - (d ? gain[d - 1] : 0);

        // A legal king capture ends the exchange immediately.
        if (pt == chess::PieceType::King)
        {
          ++d;
          break;
        }

        const BB fromBB = chess::bb::sq_bb(fromSq);

        pcs[chess::bb::ci(chess::Color(~stm))]
           [chess::bb::type_index(victim)] &= ~toBB;

        pcs[chess::bb::ci(stm)]
           [chess::bb::type_index(pt)] &= ~fromBB;
        pcs[chess::bb::ci(stm)]
           [chess::bb::type_index(pt)] |= toBB;

        occ &= ~fromBB;

        victim = pt;
        stm = chess::Color(~stm);

        ++d;
        if (d == 32)
          break;
      }

      if (!d)
        return 0;

      while (--d)
      {
        const int a = -gain[d - 1];
        const int b = gain[d];
        gain[d - 1] = -(a > b ? a : b);
      }

      return gain[0] > 0 ? gain[0] : 0;
    }
  }

  [[nodiscard]] bool see_ge_impl(const chess::Position &pos,
                                 const chess::Move &m,
                                 int threshold) noexcept
  {
    const auto &board = pos.getBoard();
    const auto &st = pos.getState();

    const auto fromP = board.getPiece(m.from());
    if (!fromP)
      return threshold <= 0;

    const chess::Color us = fromP->color;
    const chess::Color them = chess::Color(~us);
    const chess::Square to = m.to();

    const auto toP = board.getPiece(to);

    bool isEP = m.isEnPassant();
    bool isCap = m.isCapture();

    if (!isEP && fromP->type == chess::PieceType::Pawn)
    {
      const chess::Square epSq = st.enPassantSquare;
      if (epSq != chess::NO_SQUARE && to == epSq && !toP)
      {
        const int df = int(to) - int(m.from());
        isEP = (us == chess::Color::White) ? (df == 7 || df == 9)
                                           : (df == -7 || df == -9);
      }
    }

    if (!isCap && !isEP && toP && toP->color == them)
      isCap = true;

    if (!isCap && !isEP)
      return threshold <= 0;

    BB pcs[2][6];
    for (int c = 0; c < 2; ++c)
      for (int pt = 0; pt < 6; ++pt)
        pcs[c][pt] = board.getPieces(static_cast<chess::Color>(c),
                                     static_cast<chess::PieceType>(pt));

    chess::Square kingSq[2] = {
        king_square(chess::Color::White, pcs),
        king_square(chess::Color::Black, pcs)};

    BB occ = board.getAllPieces();

    const BB fromBB = chess::bb::sq_bb(m.from());
    const BB toBB = chess::bb::sq_bb(to);

    chess::PieceType captured = chess::PieceType::None;
    BB capBB = toBB;

    if (isEP)
    {
      captured = chess::PieceType::Pawn;
      const chess::Square capSq =
          (us == chess::Color::White) ? chess::Square(int(to) - 8)
                                      : chess::Square(int(to) + 8);
      capBB = chess::bb::sq_bb(capSq);

      if ((pcs[chess::bb::ci(them)][chess::bb::type_index((chess::PieceType::Pawn))] & capBB) == 0)
        return false;
    }
    else if (toP && toP->color == them)
    {
      captured = toP->type;
    }
    else
    {
      return false;
    }

    chess::PieceType pieceOnTo =
        (m.promotion() != chess::PieceType::None) ? m.promotion() : fromP->type;

    const int promoBonus =
        (m.promotion() != chess::PieceType::None)
            ? (see_value(m.promotion()) - see_value(chess::PieceType::Pawn))
            : 0;

    // Make the first capture on the dynamic piece sets.
    pcs[chess::bb::ci(us)][chess::bb::type_index((fromP->type))] &= ~fromBB;
    pcs[chess::bb::ci(us)][chess::bb::type_index((pieceOnTo))] |= toBB;
    pcs[chess::bb::ci(them)][chess::bb::type_index((captured))] &= ~capBB;

    occ &= ~fromBB;
    occ &= ~capBB;
    occ |= toBB;

    if (fromP->type == chess::PieceType::King)
      kingSq[chess::bb::ci(us)] = to;

    // Initial move itself must be legal.
    if (kingSq[chess::bb::ci(us)] != chess::NO_SQUARE &&
        dyn_attacked_by(kingSq[chess::bb::ci(us)], them, occ, pcs))
      return false;

    const int immediate = see_value(captured) + promoBonus;
    if (immediate < threshold)
      return false;

    // If even losing the moved piece immediately still clears the threshold,
    // we can accept without deeper SEE.
    if (immediate - see_value(pieceOnTo) >= threshold)
      return true;

    // Promotions and EP are rare
    const bool useSlowPath =
        isEP || chess::bb::rank_of(to) == 0 || chess::bb::rank_of(to) == 7;

    const int reply =
        useSlowPath ? see_reply_gain(them, to, pieceOnTo, occ, pcs, kingSq)
                    : see_reply_gain_fast(them, to, pieceOnTo, occ, pcs, kingSq);

    return immediate - reply >= threshold;
  }
}
