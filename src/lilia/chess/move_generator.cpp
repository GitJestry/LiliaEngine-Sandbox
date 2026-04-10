#include "lilia/chess/move_generator.hpp"

#include <array>
#include <cassert>
#include <cstdint>

#include "lilia/chess/core/magic.hpp"
#include "lilia/chess/move_helper.hpp"
#include "lilia/chess/compiler.hpp"
#include "lilia/chess/chess_types.hpp"
#include "lilia/chess/core/bitboard.hpp"

namespace lilia::chess
{

  namespace
  {

    using PT = PieceType;

    struct SideSets
    {
      bb::Bitboard pawns, knights, bishops, rooks, queens, king, all, noKing;
    };

    enum class GenMode : std::uint8_t
    {
      // All pseudo-legal moves (but with pin filtering and king-safety filtering for king moves).
      All,
      // Captures plus ALL promotions (quiet and capture promos), plus en-passant.
      CapturesPlusPromos
    };

    LILIA_ALWAYS_INLINE SideSets side_sets(const Board &b, Color c) noexcept
    {
      const bb::Bitboard pawns = b.getPieces(c, PT::Pawn);
      const bb::Bitboard knights = b.getPieces(c, PT::Knight);
      const bb::Bitboard bishops = b.getPieces(c, PT::Bishop);
      const bb::Bitboard rooks = b.getPieces(c, PT::Rook);
      const bb::Bitboard queens = b.getPieces(c, PT::Queen);
      const bb::Bitboard king = b.getPieces(c, PT::King);
      const bb::Bitboard all = b.getPieces(c);
      return SideSets{pawns, knights, bishops, rooks, queens, king, all, all & ~king};
    }

    constexpr bb::Bitboard compute_between_single(int ai, int bi) noexcept
    {
      if (ai == bi)
        return 0ULL;
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
      for (int cur = ai + step; cur != bi; cur += step)
        mask |= bb::sq_bb(static_cast<Square>(cur));
      return mask;
    }

    constexpr std::array<std::array<bb::Bitboard, SQ_NB>, SQ_NB> build_between_table()
    {
      std::array<std::array<bb::Bitboard, SQ_NB>, SQ_NB> T{};
      for (int a = 0; a < SQ_NB; ++a)
        for (int b = 0; b < SQ_NB; ++b)
          T[a][b] = compute_between_single(a, b);
      return T;
    }

    static inline constexpr auto Between = build_between_table();

    LILIA_ALWAYS_INLINE constexpr bb::Bitboard squares_between(Square a, Square b) noexcept
    {
      return Between[(int)a][(int)b];
    }

    struct PinInfo
    {
      bb::Bitboard pinned = 0ULL;
      bb::Bitboard allow[SQ_NB]; // only valid for squares flagged in 'pinned'

      LILIA_ALWAYS_INLINE void reset() noexcept
      {
        pinned = 0ULL; // allow[] need not be touched; we guard reads with 'pinned' checks
      }
      LILIA_ALWAYS_INLINE void add(Square s, bb::Bitboard m) noexcept
      {
        pinned |= bb::sq_bb(s);
        allow[(int)s] = m;
      }
      LILIA_ALWAYS_INLINE bb::Bitboard allow_mask(Square s) const noexcept { return allow[(int)s]; }
    };

    LILIA_ALWAYS_INLINE bb::Bitboard xray_attacks(magic::Slider slider, Square from,
                                                  bb::Bitboard occ, bb::Bitboard blockers) noexcept
    {
      const bb::Bitboard attacks = magic::sliding_attacks(slider, from, occ);
      blockers &= attacks;
      return attacks ^ magic::sliding_attacks(slider, from, occ ^ blockers);
    }

    LILIA_ALWAYS_INLINE bb::Bitboard compute_checkers(const Board &b, Color us,
                                                      bb::Bitboard occ) noexcept
    {
      const bb::Bitboard kbb = b.getPieces(us, PT::King);
      if (!kbb)
        return 0ULL;

      const Square ksq = static_cast<Square>(bb::ctz64(kbb));
      const Color them = ~us;

      bb::Bitboard checkers = 0ULL;

      {
        const bb::Bitboard target = bb::sq_bb(ksq);
        const bb::Bitboard pawnFrom =
            (them == Color::White) ? (bb::sw(target) | bb::se(target))
                                   : (bb::nw(target) | bb::ne(target));
        checkers |= pawnFrom & b.getPieces(them, PT::Pawn);
      }

      checkers |= bb::knight_attacks_from(ksq) & b.getPieces(them, PT::Knight);
      checkers |= bb::king_attacks_from(ksq) & b.getPieces(them, PT::King);
      checkers |= magic::sliding_attacks(magic::Slider::Bishop, ksq, occ) &
                  (b.getPieces(them, PT::Bishop) | b.getPieces(them, PT::Queen));
      checkers |= magic::sliding_attacks(magic::Slider::Rook, ksq, occ) &
                  (b.getPieces(them, PT::Rook) | b.getPieces(them, PT::Queen));

      return checkers;
    }

    LILIA_ALWAYS_INLINE void compute_pins(const Board &b, Color us, const bb::Bitboard occ,
                                          PinInfo &out) noexcept
    {
      out.reset();

      const bb::Bitboard kbb = b.getPieces(us, PT::King);
      if (!kbb)
        return;

      const Square ksq = static_cast<Square>(bb::ctz64(kbb));
      const bb::Bitboard ourNoK = b.getPieces(us) & ~kbb;

      const bb::Bitboard oppBQ = b.getPieces(~us, PT::Bishop) | b.getPieces(~us, PT::Queen);
      const bb::Bitboard oppRQ = b.getPieces(~us, PT::Rook) | b.getPieces(~us, PT::Queen);
      if (!(oppBQ | oppRQ))
        return;

      for (bb::Bitboard p = xray_attacks(magic::Slider::Bishop, ksq, occ, ourNoK) & oppBQ; p;)
      {
        const Square pinnerSq = bb::pop_lsb_unchecked(p);
        const bb::Bitboard between = squares_between(ksq, pinnerSq);
        const bb::Bitboard pinned = between & ourNoK;
        if (LILIA_LIKELY(pinned && !(pinned & (pinned - 1))))
          out.add(static_cast<Square>(bb::ctz64(pinned)), between | bb::sq_bb(pinnerSq));
      }

      for (bb::Bitboard p = xray_attacks(magic::Slider::Rook, ksq, occ, ourNoK) & oppRQ; p;)
      {
        const Square pinnerSq = bb::pop_lsb_unchecked(p);
        const bb::Bitboard between = squares_between(ksq, pinnerSq);
        const bb::Bitboard pinned = between & ourNoK;
        if (LILIA_LIKELY(pinned && !(pinned & (pinned - 1))))
          out.add(static_cast<Square>(bb::ctz64(pinned)), between | bb::sq_bb(pinnerSq));
      }
    }

    LILIA_ALWAYS_INLINE bool attacked_by_after_ep(const Board &b, Square sq, Color by,
                                                  bb::Bitboard occAfter,
                                                  Square removedPawnSq) noexcept
    {
      const bb::Bitboard target = bb::sq_bb(sq);

      const bb::Bitboard theirPawns =
          b.getPieces(by, PT::Pawn) & ~bb::sq_bb(removedPawnSq);

      const bb::Bitboard pawnFrom =
          (by == Color::White) ? (bb::sw(target) | bb::se(target))
                               : (bb::nw(target) | bb::ne(target));

      if (pawnFrom & theirPawns)
        return true;

      if (bb::knight_attacks_from(sq) & b.getPieces(by, PT::Knight))
        return true;

      if (bb::king_attacks_from(sq) & b.getPieces(by, PT::King))
        return true;

      const bb::Bitboard bishopsQueens =
          b.getPieces(by, PT::Bishop) | b.getPieces(by, PT::Queen);
      const bb::Bitboard rooksQueens =
          b.getPieces(by, PT::Rook) | b.getPieces(by, PT::Queen);

      if (magic::sliding_attacks(magic::Slider::Bishop, sq, occAfter) & bishopsQueens)
        return true;

      if (magic::sliding_attacks(magic::Slider::Rook, sq, occAfter) & rooksQueens)
        return true;

      return false;
    }

    LILIA_ALWAYS_INLINE bool ep_is_legal(const Board &b, Color side, Square from,
                                         Square to) noexcept
    {
      const bb::Bitboard kbb = b.getPieces(side, PT::King);
      if (LILIA_UNLIKELY(!kbb))
        return true;

      const Square ksq = static_cast<Square>(bb::ctz64(kbb));
      const Square capSq =
          static_cast<Square>((int)to + (side == Color::White ? -8 : +8));

      bb::Bitboard occAfter = b.getAllPieces();
      occAfter &= ~bb::sq_bb(from);
      occAfter &= ~bb::sq_bb(capSq);
      occAfter |= bb::sq_bb(to);

      return !attacked_by_after_ep(b, ksq, ~side, occAfter, capSq);
    }

    LILIA_ALWAYS_INLINE bb::Bitboard queen_attacks_from(Square from, bb::Bitboard occ) noexcept
    {
      return magic::sliding_attacks(magic::Slider::Bishop, from, occ) |
             magic::sliding_attacks(magic::Slider::Rook, from, occ);
    }

    template <bool Capture, bool EnPassant = false, CastleSide Castle = CastleSide::None>
    LILIA_ALWAYS_INLINE Move *emit_move(Move *LILIA_RESTRICT out,
                                        Square from, Square to,
                                        PT promo = PT::None) noexcept
    {
      *out++ = Move{from, to, promo, Capture, EnPassant, Castle};
      return out;
    }

    template <bool Capture>
    LILIA_ALWAYS_INLINE Move *emit_targets(Move *LILIA_RESTRICT out,
                                           Square from,
                                           bb::Bitboard targets) noexcept
    {
      while (targets)
        out = emit_move<Capture>(out, from, bb::pop_lsb_unchecked(targets));
      return out;
    }

    template <bool Capture>
    LILIA_ALWAYS_INLINE Move *emit_promotions(Move *LILIA_RESTRICT out,
                                              Square from, Square to) noexcept
    {
      out[0] = Move{from, to, PT::Queen, Capture, false, CastleSide::None};
      out[1] = Move{from, to, PT::Rook, Capture, false, CastleSide::None};
      out[2] = Move{from, to, PT::Bishop, Capture, false, CastleSide::None};
      out[3] = Move{from, to, PT::Knight, Capture, false, CastleSide::None};
      return out + 4;
    }

    template <Color Side, GenMode Mode, bool IncludeEP>
    LILIA_ALWAYS_INLINE Move *genPawnMoves_T(Move *LILIA_RESTRICT out,
                                             const Board &board, const GameState &st,
                                             bb::Bitboard occ,
                                             const SideSets &our, const SideSets &opp,
                                             const PinInfo &pins,
                                             bb::Bitboard targetMask = ~0ULL) noexcept
    {
      if (!our.pawns)
        return out;

      constexpr bool W = (Side == Color::White);
      const bb::Bitboard empty = ~occ;
      const bb::Bitboard them = opp.noKing;
      const bb::Bitboard pinned = pins.pinned;

      if constexpr (W)
      {
        const bb::Bitboard one = bb::north(our.pawns) & empty;

        if constexpr (Mode == GenMode::All)
        {
          const bb::Bitboard quietPush = (one & ~bb::RANK_8) & targetMask;
          const bb::Bitboard dbl = (bb::north(one & bb::RANK_3) & empty) & targetMask;

          for (bb::Bitboard q = quietPush; q;)
          {
            const Square to = bb::pop_lsb_unchecked(q);
            const Square from = static_cast<Square>((int)to - 8);
            const bb::Bitboard fromBB = bb::sq_bb(from);
            if (LILIA_UNLIKELY(pinned & fromBB))
            {
              if ((bb::sq_bb(to) & pins.allow_mask(from)) == 0ULL)
                continue;
            }
            out = emit_move<false>(out, from, to);
          }

          for (bb::Bitboard d = dbl; d;)
          {
            const Square to = bb::pop_lsb_unchecked(d);
            const Square from = static_cast<Square>((int)to - 16);
            const bb::Bitboard fromBB = bb::sq_bb(from);
            if (LILIA_UNLIKELY(pinned & fromBB))
            {
              if ((bb::sq_bb(to) & pins.allow_mask(from)) == 0ULL)
                continue;
            }
            out = emit_move<false>(out, from, to);
          }
        }

        {
          const bb::Bitboard capL = ((bb::nw(our.pawns) & them) & ~bb::RANK_8) & targetMask;
          const bb::Bitboard capR = ((bb::ne(our.pawns) & them) & ~bb::RANK_8) & targetMask;

          for (bb::Bitboard c = capL; c;)
          {
            const Square to = bb::pop_lsb_unchecked(c);
            const Square from = static_cast<Square>((int)to - 7);
            const bb::Bitboard fromBB = bb::sq_bb(from);
            if (LILIA_UNLIKELY(pinned & fromBB))
            {
              if ((bb::sq_bb(to) & pins.allow_mask(from)) == 0ULL)
                continue;
            }
            out = emit_move<true>(out, from, to);
          }

          for (bb::Bitboard c = capR; c;)
          {
            const Square to = bb::pop_lsb_unchecked(c);
            const Square from = static_cast<Square>((int)to - 9);
            const bb::Bitboard fromBB = bb::sq_bb(from);
            if (LILIA_UNLIKELY(pinned & fromBB))
            {
              if ((bb::sq_bb(to) & pins.allow_mask(from)) == 0ULL)
                continue;
            }
            out = emit_move<true>(out, from, to);
          }
        }

        {
          const bb::Bitboard promoPush = (one & bb::RANK_8) & targetMask;
          for (bb::Bitboard pp = promoPush; pp;)
          {
            const Square to = bb::pop_lsb_unchecked(pp);
            const Square from = static_cast<Square>((int)to - 8);
            const bb::Bitboard fromBB = bb::sq_bb(from);
            if (LILIA_UNLIKELY(pinned & fromBB))
            {
              if ((bb::sq_bb(to) & pins.allow_mask(from)) == 0ULL)
                continue;
            }
            out = emit_promotions<false>(out, from, to);
          }

          const bb::Bitboard capLP = ((bb::nw(our.pawns) & them) & bb::RANK_8) & targetMask;
          const bb::Bitboard capRP = ((bb::ne(our.pawns) & them) & bb::RANK_8) & targetMask;

          for (bb::Bitboard c = capLP; c;)
          {
            const Square to = bb::pop_lsb_unchecked(c);
            const Square from = static_cast<Square>((int)to - 7);
            const bb::Bitboard fromBB = bb::sq_bb(from);
            if (LILIA_UNLIKELY(pinned & fromBB))
            {
              if ((bb::sq_bb(to) & pins.allow_mask(from)) == 0ULL)
                continue;
            }
            out = emit_promotions<true>(out, from, to);
          }

          for (bb::Bitboard c = capRP; c;)
          {
            const Square to = bb::pop_lsb_unchecked(c);
            const Square from = static_cast<Square>((int)to - 9);
            const bb::Bitboard fromBB = bb::sq_bb(from);
            if (LILIA_UNLIKELY(pinned & fromBB))
            {
              if ((bb::sq_bb(to) & pins.allow_mask(from)) == 0ULL)
                continue;
            }
            out = emit_promotions<true>(out, from, to);
          }
        }
      }
      else
      {
        const bb::Bitboard one = bb::south(our.pawns) & empty;

        if constexpr (Mode == GenMode::All)
        {
          const bb::Bitboard quietPush = (one & ~bb::RANK_1) & targetMask;
          const bb::Bitboard dbl = (bb::south(one & bb::RANK_6) & empty) & targetMask;

          for (bb::Bitboard q = quietPush; q;)
          {
            const Square to = bb::pop_lsb_unchecked(q);
            const Square from = static_cast<Square>((int)to + 8);
            const bb::Bitboard fromBB = bb::sq_bb(from);
            if (LILIA_UNLIKELY(pinned & fromBB))
            {
              if ((bb::sq_bb(to) & pins.allow_mask(from)) == 0ULL)
                continue;
            }
            out = emit_move<false>(out, from, to);
          }

          for (bb::Bitboard d = dbl; d;)
          {
            const Square to = bb::pop_lsb_unchecked(d);
            const Square from = static_cast<Square>((int)to + 16);
            const bb::Bitboard fromBB = bb::sq_bb(from);
            if (LILIA_UNLIKELY(pinned & fromBB))
            {
              if ((bb::sq_bb(to) & pins.allow_mask(from)) == 0ULL)
                continue;
            }
            out = emit_move<false>(out, from, to);
          }
        }

        {
          const bb::Bitboard capL = ((bb::se(our.pawns) & them) & ~bb::RANK_1) & targetMask;
          const bb::Bitboard capR = ((bb::sw(our.pawns) & them) & ~bb::RANK_1) & targetMask;

          for (bb::Bitboard c = capL; c;)
          {
            const Square to = bb::pop_lsb_unchecked(c);
            const Square from = static_cast<Square>((int)to + 7);
            const bb::Bitboard fromBB = bb::sq_bb(from);
            if (LILIA_UNLIKELY(pinned & fromBB))
            {
              if ((bb::sq_bb(to) & pins.allow_mask(from)) == 0ULL)
                continue;
            }
            out = emit_move<true>(out, from, to);
          }

          for (bb::Bitboard c = capR; c;)
          {
            const Square to = bb::pop_lsb_unchecked(c);
            const Square from = static_cast<Square>((int)to + 9);
            const bb::Bitboard fromBB = bb::sq_bb(from);
            if (LILIA_UNLIKELY(pinned & fromBB))
            {
              if ((bb::sq_bb(to) & pins.allow_mask(from)) == 0ULL)
                continue;
            }
            out = emit_move<true>(out, from, to);
          }
        }

        {
          const bb::Bitboard promoPush = (one & bb::RANK_1) & targetMask;
          for (bb::Bitboard pp = promoPush; pp;)
          {
            const Square to = bb::pop_lsb_unchecked(pp);
            const Square from = static_cast<Square>((int)to + 8);
            const bb::Bitboard fromBB = bb::sq_bb(from);
            if (LILIA_UNLIKELY(pinned & fromBB))
            {
              if ((bb::sq_bb(to) & pins.allow_mask(from)) == 0ULL)
                continue;
            }
            out = emit_promotions<false>(out, from, to);
          }

          const bb::Bitboard capLP = ((bb::se(our.pawns) & them) & bb::RANK_1) & targetMask;
          const bb::Bitboard capRP = ((bb::sw(our.pawns) & them) & bb::RANK_1) & targetMask;

          for (bb::Bitboard c = capLP; c;)
          {
            const Square to = bb::pop_lsb_unchecked(c);
            const Square from = static_cast<Square>((int)to + 7);
            const bb::Bitboard fromBB = bb::sq_bb(from);
            if (LILIA_UNLIKELY(pinned & fromBB))
            {
              if ((bb::sq_bb(to) & pins.allow_mask(from)) == 0ULL)
                continue;
            }
            out = emit_promotions<true>(out, from, to);
          }

          for (bb::Bitboard c = capRP; c;)
          {
            const Square to = bb::pop_lsb_unchecked(c);
            const Square from = static_cast<Square>((int)to + 9);
            const bb::Bitboard fromBB = bb::sq_bb(from);
            if (LILIA_UNLIKELY(pinned & fromBB))
            {
              if ((bb::sq_bb(to) & pins.allow_mask(from)) == 0ULL)
                continue;
            }
            out = emit_promotions<true>(out, from, to);
          }
        }
      }

      if constexpr (IncludeEP)
      {
        if (st.enPassantSquare != NO_SQUARE)
        {
          const Square epSq = st.enPassantSquare;
          const bb::Bitboard epBB = bb::sq_bb(epSq);

          if constexpr (W)
          {
            bb::Bitboard froms = (bb::sw(epBB) | bb::se(epBB)) & our.pawns;
            while (froms)
            {
              const Square from = bb::pop_lsb_unchecked(froms);
              const bb::Bitboard fromBB = bb::sq_bb(from);
              if (LILIA_UNLIKELY(pinned & fromBB))
              {
                if ((epBB & pins.allow_mask(from)) == 0ULL)
                  continue;
              }
              if (ep_is_legal(board, Side, from, epSq))
                out = emit_move<true, true>(out, from, epSq);
            }
          }
          else
          {
            bb::Bitboard froms = (bb::nw(epBB) | bb::ne(epBB)) & our.pawns;
            while (froms)
            {
              const Square from = bb::pop_lsb_unchecked(froms);
              const bb::Bitboard fromBB = bb::sq_bb(from);
              if (LILIA_UNLIKELY(pinned & fromBB))
              {
                if ((epBB & pins.allow_mask(from)) == 0ULL)
                  continue;
              }
              if (ep_is_legal(board, Side, from, epSq))
                out = emit_move<true, true>(out, from, epSq);
            }
          }
        }
      }

      return out;
    }

    template <GenMode Mode>
    LILIA_ALWAYS_INLINE Move *genKnightMoves_T(Move *LILIA_RESTRICT out,
                                               const SideSets &our, const SideSets &opp,
                                               bb::Bitboard occ, const PinInfo &pins,
                                               bb::Bitboard targetMask = ~0ULL) noexcept
    {
      bb::Bitboard knights = our.knights & ~pins.pinned;
      if (!knights)
        return out;

      const bb::Bitboard enemyNoK = opp.noKing;
      const bb::Bitboard quietMask = (~occ) & targetMask;

      while (knights)
      {
        const Square from = bb::pop_lsb_unchecked(knights);
        const bb::Bitboard atk = bb::knight_attacks_from(from) & targetMask;
        out = emit_targets<true>(out, from, atk & enemyNoK);
        if constexpr (Mode == GenMode::All)
          out = emit_targets<false>(out, from, atk & quietMask);
      }

      return out;
    }

    template <GenMode Mode>
    LILIA_ALWAYS_INLINE Move *genBishopMoves_T(Move *LILIA_RESTRICT out,
                                               const SideSets &our, const SideSets &opp,
                                               bb::Bitboard occ, const PinInfo &pins,
                                               bb::Bitboard targetMask = ~0ULL) noexcept
    {
      if (!our.bishops)
        return out;

      const bb::Bitboard enemyNoK = opp.noKing;
      const bb::Bitboard quietMask = ~occ;

      bb::Bitboard freeBishops = our.bishops & ~pins.pinned;
      while (freeBishops)
      {
        const Square from = bb::pop_lsb_unchecked(freeBishops);
        const bb::Bitboard atk = magic::sliding_attacks(magic::Slider::Bishop, from, occ) & targetMask;
        out = emit_targets<true>(out, from, atk & enemyNoK);
        if constexpr (Mode == GenMode::All)
          out = emit_targets<false>(out, from, atk & quietMask);
      }

      bb::Bitboard pinnedBishops = our.bishops & pins.pinned;
      while (pinnedBishops)
      {
        const Square from = bb::pop_lsb_unchecked(pinnedBishops);
        const bb::Bitboard atk =
            magic::sliding_attacks(magic::Slider::Bishop, from, occ) &
            targetMask &
            pins.allow_mask(from);
        out = emit_targets<true>(out, from, atk & enemyNoK);
        if constexpr (Mode == GenMode::All)
          out = emit_targets<false>(out, from, atk & quietMask);
      }

      return out;
    }

    template <GenMode Mode>
    LILIA_ALWAYS_INLINE Move *genRookMoves_T(Move *LILIA_RESTRICT out,
                                             const SideSets &our, const SideSets &opp,
                                             bb::Bitboard occ, const PinInfo &pins,
                                             bb::Bitboard targetMask = ~0ULL) noexcept
    {
      if (!our.rooks)
        return out;

      const bb::Bitboard enemyNoK = opp.noKing;
      const bb::Bitboard quietMask = ~occ;

      bb::Bitboard freeRooks = our.rooks & ~pins.pinned;
      while (freeRooks)
      {
        const Square from = bb::pop_lsb_unchecked(freeRooks);
        const bb::Bitboard atk = magic::sliding_attacks(magic::Slider::Rook, from, occ) & targetMask;
        out = emit_targets<true>(out, from, atk & enemyNoK);
        if constexpr (Mode == GenMode::All)
          out = emit_targets<false>(out, from, atk & quietMask);
      }

      bb::Bitboard pinnedRooks = our.rooks & pins.pinned;
      while (pinnedRooks)
      {
        const Square from = bb::pop_lsb_unchecked(pinnedRooks);
        const bb::Bitboard atk =
            magic::sliding_attacks(magic::Slider::Rook, from, occ) &
            targetMask &
            pins.allow_mask(from);
        out = emit_targets<true>(out, from, atk & enemyNoK);
        if constexpr (Mode == GenMode::All)
          out = emit_targets<false>(out, from, atk & quietMask);
      }

      return out;
    }

    template <GenMode Mode>
    LILIA_ALWAYS_INLINE Move *genQueenMoves_T(Move *LILIA_RESTRICT out,
                                              const SideSets &our, const SideSets &opp,
                                              bb::Bitboard occ, const PinInfo &pins,
                                              bb::Bitboard targetMask = ~0ULL) noexcept
    {
      if (!our.queens)
        return out;

      const bb::Bitboard enemyNoK = opp.noKing;
      const bb::Bitboard quietMask = ~occ;

      bb::Bitboard freeQueens = our.queens & ~pins.pinned;
      while (freeQueens)
      {
        const Square from = bb::pop_lsb_unchecked(freeQueens);
        const bb::Bitboard atk = queen_attacks_from(from, occ) & targetMask;
        out = emit_targets<true>(out, from, atk & enemyNoK);
        if constexpr (Mode == GenMode::All)
          out = emit_targets<false>(out, from, atk & quietMask);
      }

      bb::Bitboard pinnedQueens = our.queens & pins.pinned;
      while (pinnedQueens)
      {
        const Square from = bb::pop_lsb_unchecked(pinnedQueens);
        const bb::Bitboard atk =
            queen_attacks_from(from, occ) &
            targetMask &
            pins.allow_mask(from);
        out = emit_targets<true>(out, from, atk & enemyNoK);
        if constexpr (Mode == GenMode::All)
          out = emit_targets<false>(out, from, atk & quietMask);
      }

      return out;
    }

    template <Color Side, GenMode Mode>
    LILIA_ALWAYS_INLINE Move *genKingMoves_T(Move *LILIA_RESTRICT out,
                                             const Board &board, const GameState &st,
                                             const SideSets &our, const SideSets &opp,
                                             bb::Bitboard occ) noexcept
    {
      const bb::Bitboard king = our.king;
      if (!king)
        return out;

      const Square from = static_cast<Square>(bb::ctz64(king));
      const bb::Bitboard enemyNoK = opp.noKing;
      const bb::Bitboard atk = bb::king_attacks_from(from);
      const bb::Bitboard fromBB = bb::sq_bb(from);
      const bb::Bitboard occ_no_king = occ & ~fromBB;
      const Color them = ~Side;

      for (bb::Bitboard caps = atk & enemyNoK; caps;)
      {
        const Square to = bb::pop_lsb_unchecked(caps);
        const bb::Bitboard occ2 = occ_no_king & ~bb::sq_bb(to);
        if (!attackedBy(board, to, them, occ2))
          out = emit_move<true>(out, from, to);
      }

      if constexpr (Mode == GenMode::All)
      {
        for (bb::Bitboard quiet = atk & ~occ; quiet;)
        {
          const Square to = bb::pop_lsb_unchecked(quiet);
          if (!attackedBy(board, to, them, occ_no_king))
            out = emit_move<false>(out, from, to);
        }

        if constexpr (Side == Color::White)
        {
          if (from == bb::E1)
          {
            const bool e1Safe = !attackedBy(board, from, them, occ);

            if (e1Safe &&
                (st.castlingRights & CastlingRights::WhiteKingSide) &&
                (our.rooks & bb::sq_bb(bb::H1)) &&
                !(occ & (bb::sq_bb(bb::F1) | bb::sq_bb(bb::G1))) &&
                !attackedBy(board, bb::F1, them, occ) &&
                !attackedBy(board, bb::G1, them, occ))
            {
              out = emit_move<false, false, CastleSide::KingSide>(out, from, bb::G1);
            }

            if (e1Safe &&
                (st.castlingRights & CastlingRights::WhiteQueenSide) &&
                (our.rooks & bb::sq_bb(bb::A1)) &&
                !(occ & (bb::sq_bb(bb::D1) | bb::sq_bb(bb::C1) | bb::sq_bb(bb::B1))) &&
                !attackedBy(board, bb::D1, them, occ) &&
                !attackedBy(board, bb::C1, them, occ))
            {
              out = emit_move<false, false, CastleSide::QueenSide>(out, from, bb::C1);
            }
          }
        }
        else
        {
          if (from == bb::E8)
          {
            const bool e8Safe = !attackedBy(board, from, them, occ);

            if (e8Safe &&
                (st.castlingRights & CastlingRights::BlackKingSide) &&
                (our.rooks & bb::sq_bb(bb::H8)) &&
                !(occ & (bb::sq_bb(bb::F8) | bb::sq_bb(bb::G8))) &&
                !attackedBy(board, bb::F8, them, occ) &&
                !attackedBy(board, bb::G8, them, occ))
            {
              out = emit_move<false, false, CastleSide::KingSide>(out, from, bb::G8);
            }

            if (e8Safe &&
                (st.castlingRights & CastlingRights::BlackQueenSide) &&
                (our.rooks & bb::sq_bb(bb::A8)) &&
                !(occ & (bb::sq_bb(bb::D8) | bb::sq_bb(bb::C8) | bb::sq_bb(bb::B8))) &&
                !attackedBy(board, bb::D8, them, occ) &&
                !attackedBy(board, bb::C8, them, occ))
            {
              out = emit_move<false, false, CastleSide::QueenSide>(out, from, bb::C8);
            }
          }
        }
      }

      return out;
    }

    template <Color Side>
    LILIA_ALWAYS_INLINE Move *generateEvasions_T(Move *LILIA_RESTRICT out,
                                                 const Board &b, const GameState &st,
                                                 bb::Bitboard occ,
                                                 bb::Bitboard checkers) noexcept
    {
      if (LILIA_UNLIKELY(!checkers))
        return out;

      const Color them = ~Side;
      const bb::Bitboard kbb = b.getPieces(Side, PT::King);
      if (!kbb)
        return out;

      const Square ksq = static_cast<Square>(bb::ctz64(kbb));
      const bool doubleCheck = (checkers & (checkers - 1)) != 0ULL;

      {
        const bb::Bitboard enemyNoK = b.getPieces(them) & ~b.getPieces(them, PT::King);
        const bb::Bitboard atk = bb::king_attacks_from(ksq);
        const bb::Bitboard fromBB = bb::sq_bb(ksq);
        const bb::Bitboard occ_no_king = occ & ~fromBB;

        for (bb::Bitboard caps = atk & enemyNoK; caps;)
        {
          const Square to = bb::pop_lsb_unchecked(caps);
          const bb::Bitboard occ2 = occ_no_king & ~bb::sq_bb(to);
          if (!attackedBy(b, to, them, occ2))
            out = emit_move<true>(out, ksq, to);
        }

        for (bb::Bitboard quiet = atk & ~occ; quiet;)
        {
          const Square to = bb::pop_lsb_unchecked(quiet);
          if (!attackedBy(b, to, them, occ_no_king))
            out = emit_move<false>(out, ksq, to);
        }
      }

      if (LILIA_UNLIKELY(doubleCheck))
        return out;

      const Square checkerSq = static_cast<Square>(bb::ctz64(checkers));
      const bb::Bitboard evasionTargets = checkers | squares_between(ksq, checkerSq);

      PinInfo pins;
      compute_pins(b, Side, occ, pins);

      const SideSets our = side_sets(b, Side);
      const SideSets opp = side_sets(b, them);

      out = genPawnMoves_T<Side, GenMode::All, false>(out, b, st, occ, our, opp, pins, evasionTargets);
      out = genKnightMoves_T<GenMode::All>(out, our, opp, occ, pins, evasionTargets);
      out = genBishopMoves_T<GenMode::All>(out, our, opp, occ, pins, evasionTargets);
      out = genRookMoves_T<GenMode::All>(out, our, opp, occ, pins, evasionTargets);
      out = genQueenMoves_T<GenMode::All>(out, our, opp, occ, pins, evasionTargets);

      if (st.enPassantSquare != NO_SQUARE)
      {
        const Square epSq = st.enPassantSquare;
        const bb::Bitboard epBB = bb::sq_bb(epSq);
        const Square capSq = static_cast<Square>((int)epSq + (Side == Color::White ? -8 : +8));

        bb::Bitboard froms =
            ((Side == Color::White)
                 ? (bb::sw(epBB) | bb::se(epBB))
                 : (bb::nw(epBB) | bb::ne(epBB))) &
            our.pawns;

        while (froms)
        {
          const Square from = bb::pop_lsb_unchecked(froms);
          const bb::Bitboard fromBB = bb::sq_bb(from);

          if ((pins.pinned & fromBB) && ((epBB & pins.allow_mask(from)) == 0ULL))
            continue;

          const bb::Bitboard occAfter = (occ & ~fromBB & ~bb::sq_bb(capSq)) | epBB;
          if (!attacked_by_after_ep(b, ksq, them, occAfter, capSq))
            out = emit_move<true, true>(out, from, epSq);
        }
      }

      return out;
    }

    template <Color Side>
    LILIA_ALWAYS_INLINE Move *genNonCapturePromotions_T(Move *LILIA_RESTRICT out,
                                                        const Board &b,
                                                        bb::Bitboard targetMask = ~0ULL) noexcept
    {
      const bb::Bitboard occ = b.getAllPieces();
      const bb::Bitboard pawns = b.getPieces(Side, PT::Pawn);
      if (!pawns)
        return out;

      const bb::Bitboard empty = ~occ;

      PinInfo pins;
      compute_pins(b, Side, occ, pins);

      if constexpr (Side == Color::White)
      {
        bb::Bitboard promoPush = bb::north(pawns) & empty & bb::RANK_8 & targetMask;
        while (promoPush)
        {
          const Square to = bb::pop_lsb_unchecked(promoPush);
          const Square from = static_cast<Square>((int)to - 8);
          const bb::Bitboard fromBB = bb::sq_bb(from);
          if (LILIA_UNLIKELY(pins.pinned & fromBB))
          {
            if ((bb::sq_bb(to) & pins.allow_mask(from)) == 0ULL)
              continue;
          }
          out = emit_promotions<false>(out, from, to);
        }
      }
      else
      {
        bb::Bitboard promoPush = bb::south(pawns) & empty & bb::RANK_1 & targetMask;
        while (promoPush)
        {
          const Square to = bb::pop_lsb_unchecked(promoPush);
          const Square from = static_cast<Square>((int)to + 8);
          const bb::Bitboard fromBB = bb::sq_bb(from);
          if (LILIA_UNLIKELY(pins.pinned & fromBB))
          {
            if ((bb::sq_bb(to) & pins.allow_mask(from)) == 0ULL)
              continue;
          }
          out = emit_promotions<false>(out, from, to);
        }
      }

      return out;
    }

    template <Color Side, GenMode Mode>
    LILIA_ALWAYS_INLINE Move *generate_all_regular_T(Move *LILIA_RESTRICT out,
                                                     const Board &b, const GameState &st,
                                                     bb::Bitboard occ) noexcept
    {
      const SideSets our = side_sets(b, Side);
      const SideSets opp = side_sets(b, ~Side);

      PinInfo pins;
      compute_pins(b, Side, occ, pins);

      out = genPawnMoves_T<Side, Mode, true>(out, b, st, occ, our, opp, pins);
      out = genKnightMoves_T<Mode>(out, our, opp, occ, pins);
      out = genBishopMoves_T<Mode>(out, our, opp, occ, pins);
      out = genRookMoves_T<Mode>(out, our, opp, occ, pins);
      out = genQueenMoves_T<Mode>(out, our, opp, occ, pins);
      out = genKingMoves_T<Side, Mode>(out, b, st, our, opp, occ);

      return out;
    }
  }

  void MoveGenerator::generateNonCapturePromotions(const Board &b, const GameState &st,
                                                   std::vector<Move> &out) const
  {
    std::array<Move, 32> tmp{};
    MoveBuffer buf(tmp.data(), static_cast<int>(tmp.size()));

    const bb::Bitboard occ = b.getAllPieces();
    const bb::Bitboard checkers = compute_checkers(b, st.sideToMove, occ);

    Move *ptr = buf.current();

    bb::Bitboard targetMask = ~0ULL;

    if (checkers)
    {
      // Quiet promotions can never evade double check.
      if (checkers & (checkers - 1))
      {
        out.clear();
        return;
      }

      const bb::Bitboard kbb = b.getPieces(st.sideToMove, PT::King);
      if (!kbb)
      {
        out.clear();
        return;
      }

      const Square ksq = static_cast<Square>(bb::ctz64(kbb));
      const Square checkerSq = static_cast<Square>(bb::ctz64(checkers));

      // Non-capture promotions can only evade by interposing.
      targetMask = squares_between(ksq, checkerSq);
    }

    if (st.sideToMove == Color::White)
      ptr = genNonCapturePromotions_T<Color::White>(ptr, b, targetMask);
    else
      ptr = genNonCapturePromotions_T<Color::Black>(ptr, b, targetMask);

    buf.advance_to(ptr);
    out.assign(tmp.data(), tmp.data() + buf.size());
  }

  int MoveGenerator::generateNonCapturePromotions(const Board &b, const GameState &st,
                                                  MoveBuffer &buf)
  {
    const int before = buf.size();
    const bb::Bitboard occ = b.getAllPieces();
    const bb::Bitboard checkers = compute_checkers(b, st.sideToMove, occ);

    Move *ptr = buf.current();

    bb::Bitboard targetMask = ~0ULL;

    if (checkers)
    {
      // Quiet promotions can never evade double check.
      if (checkers & (checkers - 1))
        return 0;

      const bb::Bitboard kbb = b.getPieces(st.sideToMove, PT::King);
      if (!kbb)
        return 0;

      const Square ksq = static_cast<Square>(bb::ctz64(kbb));
      const Square checkerSq = static_cast<Square>(bb::ctz64(checkers));

      // Non-capture promotions can only evade by interposing.
      targetMask = squares_between(ksq, checkerSq);
    }

    if (st.sideToMove == Color::White)
      ptr = genNonCapturePromotions_T<Color::White>(ptr, b, targetMask);
    else
      ptr = genNonCapturePromotions_T<Color::Black>(ptr, b, targetMask);

    buf.advance_to(ptr);
    return buf.size() - before;
  }

  void MoveGenerator::generatePseudoLegalMoves(const Board &b, const GameState &st,
                                               std::vector<Move> &out) const
  {
    std::array<Move, MAX_MOVES> tmp{};
    MoveBuffer buf(tmp.data(), MAX_MOVES);

    const bb::Bitboard occ = b.getAllPieces();
    const bb::Bitboard checkers = compute_checkers(b, st.sideToMove, occ);

    Move *ptr = buf.current();
    if (st.sideToMove == Color::White)
      ptr = checkers ? generateEvasions_T<Color::White>(ptr, b, st, occ, checkers)
                     : generate_all_regular_T<Color::White, GenMode::All>(ptr, b, st, occ);
    else
      ptr = checkers ? generateEvasions_T<Color::Black>(ptr, b, st, occ, checkers)
                     : generate_all_regular_T<Color::Black, GenMode::All>(ptr, b, st, occ);

    buf.advance_to(ptr);
    out.assign(tmp.data(), tmp.data() + buf.size());
  }

  int MoveGenerator::generatePseudoLegalMoves(const Board &b, const GameState &st,
                                              MoveBuffer &buf)
  {
    const int before = buf.size();
    const bb::Bitboard occ = b.getAllPieces();
    const bb::Bitboard checkers = compute_checkers(b, st.sideToMove, occ);

    Move *ptr = buf.current();
    if (st.sideToMove == Color::White)
      ptr = checkers ? generateEvasions_T<Color::White>(ptr, b, st, occ, checkers)
                     : generate_all_regular_T<Color::White, GenMode::All>(ptr, b, st, occ);
    else
      ptr = checkers ? generateEvasions_T<Color::Black>(ptr, b, st, occ, checkers)
                     : generate_all_regular_T<Color::Black, GenMode::All>(ptr, b, st, occ);

    buf.advance_to(ptr);
    return buf.size() - before;
  }

  void MoveGenerator::generateTacticalMoves(const Board &b, const GameState &st,
                                            std::vector<Move> &out) const
  {
    std::array<Move, MAX_MOVES> tmp{};
    MoveBuffer buf(tmp.data(), MAX_MOVES);

    const bb::Bitboard occ = b.getAllPieces();
    const bb::Bitboard checkers = compute_checkers(b, st.sideToMove, occ);

    Move *ptr = buf.current();
    if (st.sideToMove == Color::White)
      ptr = checkers ? generateEvasions_T<Color::White>(ptr, b, st, occ, checkers)
                     : generate_all_regular_T<Color::White, GenMode::CapturesPlusPromos>(ptr, b, st, occ);
    else
      ptr = checkers ? generateEvasions_T<Color::Black>(ptr, b, st, occ, checkers)
                     : generate_all_regular_T<Color::Black, GenMode::CapturesPlusPromos>(ptr, b, st, occ);

    buf.advance_to(ptr);
    out.assign(tmp.data(), tmp.data() + buf.size());
  }

  int MoveGenerator::generateTacticalMoves(const Board &b, const GameState &st,
                                           MoveBuffer &buf)
  {
    const int before = buf.size();
    const bb::Bitboard occ = b.getAllPieces();
    const bb::Bitboard checkers = compute_checkers(b, st.sideToMove, occ);

    Move *ptr = buf.current();
    if (st.sideToMove == Color::White)
      ptr = checkers ? generateEvasions_T<Color::White>(ptr, b, st, occ, checkers)
                     : generate_all_regular_T<Color::White, GenMode::CapturesPlusPromos>(ptr, b, st, occ);
    else
      ptr = checkers ? generateEvasions_T<Color::Black>(ptr, b, st, occ, checkers)
                     : generate_all_regular_T<Color::Black, GenMode::CapturesPlusPromos>(ptr, b, st, occ);

    buf.advance_to(ptr);
    return buf.size() - before;
  }

  void MoveGenerator::generateEvasions(const Board &b, const GameState &st,
                                       std::vector<Move> &out) const
  {
    std::array<Move, MAX_MOVES> tmp{};
    MoveBuffer buf(tmp.data(), MAX_MOVES);

    const bb::Bitboard occ = b.getAllPieces();
    const bb::Bitboard checkers = compute_checkers(b, st.sideToMove, occ);

    if (!checkers)
    {
      out.clear();
      return;
    }

    Move *ptr = buf.current();
    if (st.sideToMove == Color::White)
      ptr = generateEvasions_T<Color::White>(ptr, b, st, occ, checkers);
    else
      ptr = generateEvasions_T<Color::Black>(ptr, b, st, occ, checkers);

    buf.advance_to(ptr);
    out.assign(tmp.data(), tmp.data() + buf.size());
  }

  int MoveGenerator::generateEvasions(const Board &b, const GameState &st, MoveBuffer &buf)
  {
    const int before = buf.size();
    const bb::Bitboard occ = b.getAllPieces();
    const bb::Bitboard checkers = compute_checkers(b, st.sideToMove, occ);

    if (!checkers)
      return 0;

    Move *ptr = buf.current();
    if (st.sideToMove == Color::White)
      ptr = generateEvasions_T<Color::White>(ptr, b, st, occ, checkers);
    else
      ptr = generateEvasions_T<Color::Black>(ptr, b, st, occ, checkers);

    buf.advance_to(ptr);
    return buf.size() - before;
  }

}
