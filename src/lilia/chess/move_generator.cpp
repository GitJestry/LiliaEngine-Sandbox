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

    LILIA_ALWAYS_INLINE bool aligned_diag(Square a, Square b) noexcept
    {
      const int af = (int)a & bb::H1, bf = (int)b & bb::H1;
      const int ar = (int)a >> bb::D1, br = (int)b >> bb::D1;
      const int df = af - bf, dr = ar - br;
      return (df == dr) || (df == -dr);
    }
    LILIA_ALWAYS_INLINE bool aligned_ortho(Square a, Square b) noexcept
    {
      return (((int)a ^ (int)b) & bb::H1) == 0 || (((int)a ^ (int)b) & bb::A8) == 0;
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

    LILIA_ALWAYS_INLINE void compute_pins(const Board &b, Color us, const bb::Bitboard occ,
                                          PinInfo &out) noexcept
    {
      out.reset();

      const bb::Bitboard kbb = b.getPieces(us, PT::King);
      if (!kbb)
        return;
      const Square ksq = static_cast<Square>(bb::ctz64(kbb));

      const bb::Bitboard ourPieces = b.getPieces(us);

      // Early out avoids building lambdas and loops in positions with no candidate pinners.
      const bb::Bitboard oppBQ = b.getPieces(~us, PT::Bishop) | b.getPieces(~us, PT::Queen);
      const bb::Bitboard oppRQ = b.getPieces(~us, PT::Rook) | b.getPieces(~us, PT::Queen);
      if ((oppBQ | oppRQ) == 0ULL)
        return;

      auto try_pinner = [&](Square pinnerSq, bool isDiag) noexcept
      {
        if (isDiag ? !aligned_diag(ksq, pinnerSq) : !aligned_ortho(ksq, pinnerSq))
          return;
        const bb::Bitboard between = squares_between(ksq, pinnerSq);
        if (!between)
          return;
        const bb::Bitboard blockers = between & occ;
        if (!blockers || (blockers & (blockers - 1)))
          return; // not exactly one
        if ((blockers & ourPieces) == 0ULL)
          return; // blocker not ours
        const Square pinnedSq = static_cast<Square>(bb::ctz64(blockers));
        out.add(pinnedSq, between | bb::sq_bb(pinnerSq));
      };

      for (bb::Bitboard s = oppBQ; s;)
        try_pinner(bb::pop_lsb_unchecked(s), true);
      for (bb::Bitboard s = oppRQ; s;)
        try_pinner(bb::pop_lsb_unchecked(s), false);
    }

    LILIA_ALWAYS_INLINE bool ep_is_legal_fast(const Board &b, Color side, Square from,
                                              Square to) noexcept
    {
      const bb::Bitboard kbb = b.getPieces(side, PT::King);
      if (LILIA_UNLIKELY(!kbb))
        return true;

      const Square ksq = static_cast<Square>(bb::ctz64(kbb));

      if (LILIA_LIKELY(bb::rank_of(ksq) != bb::rank_of(from)))
        return true;

      const int to_i = (int)to;
      const int cap_i = (side == Color::White) ? (to_i - 8) : (to_i + 8);
      const Square capSq = static_cast<Square>(cap_i);

      bb::Bitboard occ = b.getAllPieces();
      occ &= ~bb::sq_bb(from);
      occ &= ~bb::sq_bb(capSq);
      occ |= bb::sq_bb(to);

      const bb::Bitboard sliders = b.getPieces(~side, PT::Rook) | b.getPieces(~side, PT::Queen);
      if (LILIA_LIKELY(!sliders))
        return true;

      const bb::Bitboard rays = magic::sliding_attacks(magic::Slider::Rook, ksq, occ);
      return (rays & sliders) == 0ULL;
    }

    template <Color Side, GenMode Mode, class Emit>
    LILIA_ALWAYS_INLINE void genPawnMoves_T(const Board &board, const GameState &st, bb::Bitboard occ,
                                            const SideSets &our, const SideSets &opp,
                                            const PinInfo &pins, Emit &&emit,
                                            bb::Bitboard targetMask = ~0ULL) noexcept
    {
      if (!our.pawns)
        return;

      constexpr bool W = (Side == Color::White);
      constexpr PT promoOrder[4] = {PT::Queen, PT::Rook, PT::Bishop, PT::Knight};

      const bb::Bitboard empty = ~occ;
      const bb::Bitboard them = opp.noKing;

      if constexpr (W)
      {
        const bb::Bitboard one = bb::north(our.pawns) & empty;

        // Quiet pushes (non-promotions) and double pushes only in All.
        if constexpr (Mode == GenMode::All)
        {
          const bb::Bitboard quietPush = (one & ~bb::RANK_8) & targetMask;
          const bb::Bitboard dbl = (bb::north(one & bb::RANK_3) & empty) & targetMask;

          for (bb::Bitboard q = quietPush; q;)
          {
            const Square to = bb::pop_lsb_unchecked(q);
            const Square from = static_cast<Square>((int)to - 8);
            const bb::Bitboard fromBB = bb::sq_bb(from);
            if (LILIA_UNLIKELY(pins.pinned & fromBB))
            {
              if ((bb::sq_bb(to) & pins.allow_mask(from)) == 0ULL)
                continue;
            }
            emit(Move{from, to, PT::None, false, false, CastleSide::None});
          }
          for (bb::Bitboard d = dbl; d;)
          {
            const Square to = bb::pop_lsb_unchecked(d);
            const Square from = static_cast<Square>((int)to - 16);
            const bb::Bitboard fromBB = bb::sq_bb(from);
            if (LILIA_UNLIKELY(pins.pinned & fromBB))
            {
              if ((bb::sq_bb(to) & pins.allow_mask(from)) == 0ULL)
                continue;
            }
            emit(Move{from, to, PT::None, false, false, CastleSide::None});
          }
        }

        // Non-promotion captures (both modes)
        {
          const bb::Bitboard capL = ((bb::nw(our.pawns) & them) & ~bb::RANK_8) & targetMask;
          const bb::Bitboard capR = ((bb::ne(our.pawns) & them) & ~bb::RANK_8) & targetMask;

          for (bb::Bitboard c = capL; c;)
          {
            const Square to = bb::pop_lsb_unchecked(c);
            const Square from = static_cast<Square>((int)to - 7);
            const bb::Bitboard fromBB = bb::sq_bb(from);
            if (LILIA_UNLIKELY(pins.pinned & fromBB))
            {
              if ((bb::sq_bb(to) & pins.allow_mask(from)) == 0ULL)
                continue;
            }
            emit(Move{from, to, PT::None, true, false, CastleSide::None});
          }
          for (bb::Bitboard c = capR; c;)
          {
            const Square to = bb::pop_lsb_unchecked(c);
            const Square from = static_cast<Square>((int)to - 9);
            const bb::Bitboard fromBB = bb::sq_bb(from);
            if (LILIA_UNLIKELY(pins.pinned & fromBB))
            {
              if ((bb::sq_bb(to) & pins.allow_mask(from)) == 0ULL)
                continue;
            }
            emit(Move{from, to, PT::None, true, false, CastleSide::None});
          }
        }

        // Promotions: always generated in both modes (quiet promos are required in CapturesPlusPromos)
        {
          const bb::Bitboard promoPush = (one & bb::RANK_8) & targetMask;
          for (bb::Bitboard pp = promoPush; pp;)
          {
            const Square to = bb::pop_lsb_unchecked(pp);
            const Square from = static_cast<Square>((int)to - 8);
            const bb::Bitboard fromBB = bb::sq_bb(from);
            if (LILIA_UNLIKELY(pins.pinned & fromBB))
            {
              if ((bb::sq_bb(to) & pins.allow_mask(from)) == 0ULL)
                continue;
            }
            for (int i = 0; i < 4; ++i)
              emit(Move{from, to, promoOrder[i], false, false, CastleSide::None});
          }

          const bb::Bitboard capLP = ((bb::nw(our.pawns) & them) & bb::RANK_8) & targetMask;
          const bb::Bitboard capRP = ((bb::ne(our.pawns) & them) & bb::RANK_8) & targetMask;
          for (bb::Bitboard c = capLP; c;)
          {
            const Square to = bb::pop_lsb_unchecked(c);
            const Square from = static_cast<Square>((int)to - 7);
            const bb::Bitboard fromBB = bb::sq_bb(from);
            if (LILIA_UNLIKELY(pins.pinned & fromBB))
            {
              if ((bb::sq_bb(to) & pins.allow_mask(from)) == 0ULL)
                continue;
            }
            for (int i = 0; i < 4; ++i)
              emit(Move{from, to, promoOrder[i], true, false, CastleSide::None});
          }
          for (bb::Bitboard c = capRP; c;)
          {
            const Square to = bb::pop_lsb_unchecked(c);
            const Square from = static_cast<Square>((int)to - 9);
            const bb::Bitboard fromBB = bb::sq_bb(from);
            if (LILIA_UNLIKELY(pins.pinned & fromBB))
            {
              if ((bb::sq_bb(to) & pins.allow_mask(from)) == 0ULL)
                continue;
            }
            for (int i = 0; i < 4; ++i)
              emit(Move{from, to, promoOrder[i], true, false, CastleSide::None});
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
            if (LILIA_UNLIKELY(pins.pinned & fromBB))
            {
              if ((bb::sq_bb(to) & pins.allow_mask(from)) == 0ULL)
                continue;
            }
            emit(Move{from, to, PT::None, false, false, CastleSide::None});
          }
          for (bb::Bitboard d = dbl; d;)
          {
            const Square to = bb::pop_lsb_unchecked(d);
            const Square from = static_cast<Square>((int)to + 16);
            const bb::Bitboard fromBB = bb::sq_bb(from);
            if (LILIA_UNLIKELY(pins.pinned & fromBB))
            {
              if ((bb::sq_bb(to) & pins.allow_mask(from)) == 0ULL)
                continue;
            }
            emit(Move{from, to, PT::None, false, false, CastleSide::None});
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
            if (LILIA_UNLIKELY(pins.pinned & fromBB))
            {
              if ((bb::sq_bb(to) & pins.allow_mask(from)) == 0ULL)
                continue;
            }
            emit(Move{from, to, PT::None, true, false, CastleSide::None});
          }
          for (bb::Bitboard c = capR; c;)
          {
            const Square to = bb::pop_lsb_unchecked(c);
            const Square from = static_cast<Square>((int)to + 9);
            const bb::Bitboard fromBB = bb::sq_bb(from);
            if (LILIA_UNLIKELY(pins.pinned & fromBB))
            {
              if ((bb::sq_bb(to) & pins.allow_mask(from)) == 0ULL)
                continue;
            }
            emit(Move{from, to, PT::None, true, false, CastleSide::None});
          }
        }

        {
          const bb::Bitboard promoPush = (one & bb::RANK_1) & targetMask;
          for (bb::Bitboard pp = promoPush; pp;)
          {
            const Square to = bb::pop_lsb_unchecked(pp);
            const Square from = static_cast<Square>((int)to + 8);
            const bb::Bitboard fromBB = bb::sq_bb(from);
            if (LILIA_UNLIKELY(pins.pinned & fromBB))
            {
              if ((bb::sq_bb(to) & pins.allow_mask(from)) == 0ULL)
                continue;
            }
            for (int i = 0; i < 4; ++i)
              emit(Move{from, to, promoOrder[i], false, false, CastleSide::None});
          }

          const bb::Bitboard capLP = ((bb::se(our.pawns) & them) & bb::RANK_1) & targetMask;
          const bb::Bitboard capRP = ((bb::sw(our.pawns) & them) & bb::RANK_1) & targetMask;
          for (bb::Bitboard c = capLP; c;)
          {
            const Square to = bb::pop_lsb_unchecked(c);
            const Square from = static_cast<Square>((int)to + 7);
            const bb::Bitboard fromBB = bb::sq_bb(from);
            if (LILIA_UNLIKELY(pins.pinned & fromBB))
            {
              if ((bb::sq_bb(to) & pins.allow_mask(from)) == 0ULL)
                continue;
            }
            for (int i = 0; i < 4; ++i)
              emit(Move{from, to, promoOrder[i], true, false, CastleSide::None});
          }
          for (bb::Bitboard c = capRP; c;)
          {
            const Square to = bb::pop_lsb_unchecked(c);
            const Square from = static_cast<Square>((int)to + 9);
            const bb::Bitboard fromBB = bb::sq_bb(from);
            if (LILIA_UNLIKELY(pins.pinned & fromBB))
            {
              if ((bb::sq_bb(to) & pins.allow_mask(from)) == 0ULL)
                continue;
            }
            for (int i = 0; i < 4; ++i)
              emit(Move{from, to, promoOrder[i], true, false, CastleSide::None});
          }
        }
      }

      if (st.enPassantSquare != NO_SQUARE)
      {
        const Square epSq = st.enPassantSquare;
        const bb::Bitboard epBB = bb::sq_bb(epSq);
        if constexpr (W)
        {
          bb::Bitboard froms = (bb::sw(epBB) | bb::se(epBB)) & our.pawns;
          for (bb::Bitboard f = froms; f;)
          {
            const Square from = bb::pop_lsb_unchecked(f);
            const bb::Bitboard fromBB = bb::sq_bb(from);
            if (LILIA_UNLIKELY(pins.pinned & fromBB))
            {
              if ((bb::sq_bb(epSq) & pins.allow_mask(from)) == 0ULL)
                continue;
            }
            if (ep_is_legal_fast(board, Side, from, epSq))
              emit(Move{from, epSq, PT::None, true, true, CastleSide::None});
          }
        }
        else
        {
          bb::Bitboard froms = (bb::nw(epBB) | bb::ne(epBB)) & our.pawns;
          for (bb::Bitboard f = froms; f;)
          {
            const Square from = bb::pop_lsb_unchecked(f);
            const bb::Bitboard fromBB = bb::sq_bb(from);
            if (LILIA_UNLIKELY(pins.pinned & fromBB))
            {
              if ((bb::sq_bb(epSq) & pins.allow_mask(from)) == 0ULL)
                continue;
            }
            if (ep_is_legal_fast(board, Side, from, epSq))
              emit(Move{from, epSq, PT::None, true, true, CastleSide::None});
          }
        }
      }
    }

    template <GenMode Mode, class Emit>
    LILIA_ALWAYS_INLINE void genKnightMoves_T(const SideSets &our, const SideSets &opp,
                                              bb::Bitboard occ, const PinInfo &pins, Emit &&emit,
                                              bb::Bitboard targetMask = ~0ULL) noexcept
    {
      if (!our.knights)
        return;

      const bb::Bitboard enemyNoK = opp.noKing;

      for (bb::Bitboard n = our.knights; n;)
      {
        const Square from = bb::pop_lsb_unchecked(n);
        const bb::Bitboard fromBB = bb::sq_bb(from);
        const bb::Bitboard pinMask = (pins.pinned & fromBB) ? pins.allow_mask(from) : ~0ULL;

        const bb::Bitboard atk = (bb::knight_attacks_from(from) & targetMask) & pinMask;

        for (bb::Bitboard caps = atk & enemyNoK; caps;)
        {
          emit(Move{from, bb::pop_lsb_unchecked(caps), PT::None, true, false, CastleSide::None});
        }

        if constexpr (Mode == GenMode::All)
        {
          for (bb::Bitboard quiet = atk & ~occ; quiet;)
          {
            emit(Move{from, bb::pop_lsb_unchecked(quiet), PT::None, false, false, CastleSide::None});
          }
        }
      }
    }

    template <GenMode Mode, class Emit>
    LILIA_ALWAYS_INLINE void genBishopMoves_T(const SideSets &our, const SideSets &opp,
                                              bb::Bitboard occ, const PinInfo &pins, Emit &&emit,
                                              bb::Bitboard targetMask = ~0ULL) noexcept
    {
      if (!our.bishops)
        return;

      const bb::Bitboard enemyNoK = opp.noKing;

      for (bb::Bitboard bbs = our.bishops; bbs;)
      {
        const Square from = bb::pop_lsb_unchecked(bbs);
        const bb::Bitboard fromBB = bb::sq_bb(from);
        const bb::Bitboard pinMask = (pins.pinned & fromBB) ? pins.allow_mask(from) : ~0ULL;

        const bb::Bitboard atk =
            (magic::sliding_attacks(magic::Slider::Bishop, from, occ) & targetMask) & pinMask;

        for (bb::Bitboard caps = atk & enemyNoK; caps;)
        {
          emit(Move{from, bb::pop_lsb_unchecked(caps), PT::None, true, false, CastleSide::None});
        }

        if constexpr (Mode == GenMode::All)
        {
          for (bb::Bitboard quiet = atk & ~occ; quiet;)
          {
            emit(Move{from, bb::pop_lsb_unchecked(quiet), PT::None, false, false, CastleSide::None});
          }
        }
      }
    }

    template <GenMode Mode, class Emit>
    LILIA_ALWAYS_INLINE void genRookMoves_T(const SideSets &our, const SideSets &opp, bb::Bitboard occ,
                                            const PinInfo &pins, Emit &&emit,
                                            bb::Bitboard targetMask = ~0ULL) noexcept
    {
      if (!our.rooks)
        return;

      const bb::Bitboard enemyNoK = opp.noKing;

      for (bb::Bitboard r = our.rooks; r;)
      {
        const Square from = bb::pop_lsb_unchecked(r);
        const bb::Bitboard fromBB = bb::sq_bb(from);
        const bb::Bitboard pinMask = (pins.pinned & fromBB) ? pins.allow_mask(from) : ~0ULL;

        const bb::Bitboard atk =
            (magic::sliding_attacks(magic::Slider::Rook, from, occ) & targetMask) & pinMask;

        for (bb::Bitboard caps = atk & enemyNoK; caps;)
        {
          emit(Move{from, bb::pop_lsb_unchecked(caps), PT::None, true, false, CastleSide::None});
        }

        if constexpr (Mode == GenMode::All)
        {
          for (bb::Bitboard quiet = atk & ~occ; quiet;)
          {
            emit(Move{from, bb::pop_lsb_unchecked(quiet), PT::None, false, false, CastleSide::None});
          }
        }
      }
    }

    template <GenMode Mode, class Emit>
    LILIA_ALWAYS_INLINE void genQueenMoves_T(const SideSets &our, const SideSets &opp, bb::Bitboard occ,
                                             const PinInfo &pins, Emit &&emit,
                                             bb::Bitboard targetMask = ~0ULL) noexcept
    {
      if (!our.queens)
        return;

      const bb::Bitboard enemyNoK = opp.noKing;

      for (bb::Bitboard q = our.queens; q;)
      {
        const Square from = bb::pop_lsb_unchecked(q);
        const bb::Bitboard fromBB = bb::sq_bb(from);
        const bb::Bitboard pinMask = (pins.pinned & fromBB) ? pins.allow_mask(from) : ~0ULL;

        const bb::Bitboard atk = ((magic::sliding_attacks(magic::Slider::Bishop, from, occ) |
                                   magic::sliding_attacks(magic::Slider::Rook, from, occ)) &
                                  targetMask) &
                                 pinMask;

        for (bb::Bitboard caps = atk & enemyNoK; caps;)
        {
          emit(Move{from, bb::pop_lsb_unchecked(caps), PT::None, true, false, CastleSide::None});
        }

        if constexpr (Mode == GenMode::All)
        {
          for (bb::Bitboard quiet = atk & ~occ; quiet;)
          {
            emit(Move{from, bb::pop_lsb_unchecked(quiet), PT::None, false, false, CastleSide::None});
          }
        }
      }
    }

    template <GenMode Mode, class Emit>
    LILIA_ALWAYS_INLINE void genKingMoves_T(const Board &board, const GameState &st, Color side,
                                            const SideSets &our, const SideSets &opp, bb::Bitboard occ,
                                            Emit &&emit) noexcept
    {
      const bb::Bitboard king = our.king;
      if (!king)
        return;
      const Square from = static_cast<Square>(bb::ctz64(king));

      const bb::Bitboard enemyNoK = opp.noKing;
      const bb::Bitboard atk = bb::king_attacks_from(from);
      const bb::Bitboard fromBB = bb::sq_bb(from);
      const bb::Bitboard occ_no_king = occ & ~fromBB;

      for (bb::Bitboard caps = atk & enemyNoK; caps;)
      {
        const Square to = bb::pop_lsb_unchecked(caps);
        const bb::Bitboard occ2 = occ_no_king & ~bb::sq_bb(to);
        if (!attackedBy(board, to, ~side, occ2))
          emit(Move{from, to, PT::None, true, false, CastleSide::None});
      }

      if constexpr (Mode == GenMode::All)
      {
        for (bb::Bitboard quiet = atk & ~occ; quiet;)
        {
          const Square to = bb::pop_lsb_unchecked(quiet);
          if (!attackedBy(board, to, ~side, occ_no_king))
            emit(Move{from, to, PT::None, false, false, CastleSide::None});
        }
        const Color enemySide = ~side;
        if (side == Color::White)
        {
          if ((st.castlingRights & CastlingRights::WhiteKingSide) && (our.rooks & bb::sq_bb(bb::H1)) &&
              !(occ & (bb::sq_bb(bb::F1) | bb::sq_bb(bb::G1))))
          {
            if (!attackedBy(board, bb::E1, enemySide, occ) &&
                !attackedBy(board, bb::F1, enemySide, occ) &&
                !attackedBy(board, bb::G1, enemySide, occ))
            {
              emit(Move{bb::E1, bb::G1, PT::None, false, false, CastleSide::KingSide});
            }
          }
          if ((st.castlingRights & CastlingRights::WhiteQueenSide) && (our.rooks & bb::sq_bb(bb::A1)) &&
              !(occ & (bb::sq_bb(bb::D1) | bb::sq_bb(bb::C1) | bb::sq_bb(bb::B1))))
          {
            if (!attackedBy(board, bb::E1, enemySide, occ) &&
                !attackedBy(board, bb::D1, enemySide, occ) &&
                !attackedBy(board, bb::C1, enemySide, occ))
            {
              emit(Move{bb::E1, bb::C1, PT::None, false, false, CastleSide::QueenSide});
            }
          }
        }
        else
        {
          if ((st.castlingRights & CastlingRights::BlackKingSide) && (our.rooks & bb::sq_bb(bb::H8)) &&
              !(occ & (bb::sq_bb(bb::F8) | bb::sq_bb(bb::G8))))
          {
            if (!attackedBy(board, bb::E8, enemySide, occ) &&
                !attackedBy(board, bb::F8, enemySide, occ) &&
                !attackedBy(board, bb::G8, enemySide, occ))
            {
              emit(Move{bb::E8, bb::G8, PT::None, false, false, CastleSide::KingSide});
            }
          }
          if ((st.castlingRights & CastlingRights::BlackQueenSide) && (our.rooks & bb::sq_bb(bb::A8)) &&
              !(occ & (bb::sq_bb(bb::D8) | bb::sq_bb(bb::C8) | bb::sq_bb(bb::B8))))
          {
            if (!attackedBy(board, bb::E8, enemySide, occ) &&
                !attackedBy(board, bb::D8, enemySide, occ) &&
                !attackedBy(board, bb::C8, enemySide, occ))
            {
              emit(Move{bb::E8, bb::C8, PT::None, false, false, CastleSide::QueenSide});
            }
          }
        }
      }
    }

    template <class Emit>
    LILIA_ALWAYS_INLINE void generateEvasions_T(const Board &b, const GameState &st,
                                                const PinInfo &pins, Emit &&emit) noexcept
    {
      const Color us = st.sideToMove;
      const Color them = ~us;

      const bb::Bitboard kbb = b.getPieces(us, PT::King);
      if (!kbb)
        return;
      const Square ksq = static_cast<Square>(bb::ctz64(kbb));

      const bb::Bitboard occ = b.getAllPieces();

      bb::Bitboard checkers = 0ULL;
      {
        const bb::Bitboard target = bb::sq_bb(ksq);
        const bb::Bitboard pawnFrom = (them == Color::White) ? (bb::sw(target) | bb::se(target))
                                                             : (bb::nw(target) | bb::ne(target));
        checkers |= pawnFrom & b.getPieces(them, PT::Pawn);
      }
      checkers |= bb::knight_attacks_from(ksq) & b.getPieces(them, PT::Knight);
      checkers |= magic::sliding_attacks(magic::Slider::Bishop, ksq, occ) &
                  (b.getPieces(them, PT::Bishop) | b.getPieces(them, PT::Queen));
      checkers |= magic::sliding_attacks(magic::Slider::Rook, ksq, occ) &
                  (b.getPieces(them, PT::Rook) | b.getPieces(them, PT::Queen));

      const int numCheckers = bb::popcount(checkers);

      // King moves first (legal only)
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
            emit(Move{ksq, to, PT::None, true, false, CastleSide::None});
        }
        for (bb::Bitboard quiet = atk & ~occ; quiet;)
        {
          const Square to = bb::pop_lsb_unchecked(quiet);
          if (!attackedBy(b, to, them, occ_no_king))
            emit(Move{ksq, to, PT::None, false, false, CastleSide::None});
        }
      }

      if (LILIA_UNLIKELY(numCheckers >= 2))
        return; // only king moves allowed

      // Block/capture single checker
      bb::Bitboard blockMask = 0ULL;
      if (numCheckers == 1)
      {
        const Square checkerSq = static_cast<Square>(bb::ctz64(checkers));
        blockMask = squares_between(ksq, checkerSq);
      }
      const bb::Bitboard evasionTargets = checkers | blockMask;

      const SideSets our = side_sets(b, us);
      const SideSets opp = side_sets(b, them);

      if (us == Color::White)
        genPawnMoves_T<Color::White, GenMode::All>(b, st, occ, our, opp, pins, emit, evasionTargets);
      else
        genPawnMoves_T<Color::Black, GenMode::All>(b, st, occ, our, opp, pins, emit, evasionTargets);

      genKnightMoves_T<GenMode::All>(our, opp, occ, pins, emit, evasionTargets);
      genBishopMoves_T<GenMode::All>(our, opp, occ, pins, emit, evasionTargets);
      genRookMoves_T<GenMode::All>(our, opp, occ, pins, emit, evasionTargets);
      genQueenMoves_T<GenMode::All>(our, opp, occ, pins, emit, evasionTargets);

      // the EP capture must resolve the check.
      if (st.enPassantSquare != NO_SQUARE)
      {
        const Square epSq = st.enPassantSquare;
        const bb::Bitboard epBB = bb::sq_bb(epSq);

        // EP only helps if capturing on epSq is part of evasionTargets.
        if ((evasionTargets & epBB) == 0ULL)
          return;

        if (us == Color::White)
        {
          for (bb::Bitboard f = ((bb::sw(epBB) | bb::se(epBB)) & our.pawns); f;)
          {
            const Square from = bb::pop_lsb_unchecked(f);
            const bb::Bitboard fromBB = bb::sq_bb(from);
            if (LILIA_UNLIKELY(pins.pinned & fromBB))
            {
              if ((epBB & pins.allow_mask(from)) == 0ULL)
                continue;
            }
            const Square capSq = static_cast<Square>((int)epSq - 8);
            const bb::Bitboard occAfter = (occ & ~fromBB & ~bb::sq_bb(capSq)) | epBB;
            if (!attackedBy(b, ksq, them, occAfter))
              emit(Move{from, epSq, PT::None, true, true, CastleSide::None});
          }
        }
        else
        {
          for (bb::Bitboard f = ((bb::nw(epBB) | bb::ne(epBB)) & our.pawns); f;)
          {
            const Square from = bb::pop_lsb_unchecked(f);
            const bb::Bitboard fromBB = bb::sq_bb(from);
            if (LILIA_UNLIKELY(pins.pinned & fromBB))
            {
              if ((epBB & pins.allow_mask(from)) == 0ULL)
                continue;
            }
            const Square capSq = static_cast<Square>((int)epSq + 8);
            const bb::Bitboard occAfter = (occ & ~fromBB & ~bb::sq_bb(capSq)) | epBB;
            if (!attackedBy(b, ksq, them, occAfter))
              emit(Move{from, epSq, PT::None, true, true, CastleSide::None});
          }
        }
      }
    }

    template <GenMode Mode, class Emit>
    LILIA_ALWAYS_INLINE void generate_all_regular(const Board &b, const GameState &st,
                                                  Emit &&emit) noexcept
    {
      const Color side = st.sideToMove;

      const bb::Bitboard occ = b.getAllPieces();
      const SideSets our = side_sets(b, side);
      const SideSets opp = side_sets(b, ~side);

      PinInfo pins;
      compute_pins(b, side, occ, pins);

      if (side == Color::White)
        genPawnMoves_T<Color::White, Mode>(b, st, occ, our, opp, pins, emit);
      else
        genPawnMoves_T<Color::Black, Mode>(b, st, occ, our, opp, pins, emit);

      genKnightMoves_T<Mode>(our, opp, occ, pins, emit);
      genBishopMoves_T<Mode>(our, opp, occ, pins, emit);
      genRookMoves_T<Mode>(our, opp, occ, pins, emit);
      genQueenMoves_T<Mode>(our, opp, occ, pins, emit);
      genKingMoves_T<Mode>(b, st, side, our, opp, occ, emit);
    }

  }

  void MoveGenerator::generateNonCapturePromotions(const Board &b, const GameState &st,
                                                   std::vector<Move> &out) const
  {
    if (out.capacity() < 16)
      out.reserve(16);
    out.clear();

    using PT = PieceType;
    constexpr PT promoOrder[4] = {PT::Queen, PT::Rook, PT::Bishop, PT::Knight};

    const Color side = st.sideToMove;
    const bb::Bitboard occ = b.getAllPieces();
    const bb::Bitboard pawns = b.getPieces(side, PT::Pawn);
    const bb::Bitboard empty = ~occ;

    PinInfo pins;
    compute_pins(b, side, occ, pins);

    if (side == Color::White)
    {
      const bb::Bitboard one = bb::north(pawns) & empty;
      bb::Bitboard promoPush = one & bb::RANK_8;
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
        for (int i = 0; i < 4; ++i)
          out.push_back(Move{from, to, promoOrder[i], false, false, CastleSide::None});
      }
    }
    else
    {
      const bb::Bitboard one = bb::south(pawns) & empty;
      bb::Bitboard promoPush = one & bb::RANK_1;
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
        for (int i = 0; i < 4; ++i)
          out.push_back(Move{from, to, promoOrder[i], false, false, CastleSide::None});
      }
    }
  }

  int MoveGenerator::generateNonCapturePromotions(const Board &b, const GameState &st,
                                                  MoveBuffer &buf)
  {
    using PT = PieceType;
    constexpr PT promoOrder[4] = {PT::Queen, PT::Rook, PT::Bishop, PT::Knight};

    const int before = buf.n;

    const Color side = st.sideToMove;
    const bb::Bitboard occ = b.getAllPieces();
    const bb::Bitboard pawns = b.getPieces(side, PT::Pawn);
    const bb::Bitboard empty = ~occ;

    PinInfo pins;
    compute_pins(b, side, occ, pins);

    if (side == Color::White)
    {
      const bb::Bitboard one = bb::north(pawns) & empty;
      bb::Bitboard promoPush = one & bb::RANK_8;
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
        for (int i = 0; i < 4; ++i)
          buf.push_unchecked(Move{from, to, promoOrder[i], false, false, CastleSide::None});
      }
    }
    else
    {
      const bb::Bitboard one = bb::south(pawns) & empty;
      bb::Bitboard promoPush = one & bb::RANK_1;
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
        for (int i = 0; i < 4; ++i)
          buf.push_unchecked(Move{from, to, promoOrder[i], false, false, CastleSide::None});
      }
    }

    return buf.n - before;
  }

  void MoveGenerator::generatePseudoLegalMoves(const Board &b, const GameState &st,
                                               std::vector<Move> &out) const
  {
    if (out.capacity() < 128)
      out.reserve(128);
    out.clear();
    auto sink = [&](const Move &m)
    { out.push_back(m); };
    generate_all_regular<GenMode::All>(b, st, sink);
  }

  int MoveGenerator::generatePseudoLegalMoves(const Board &b, const GameState &st,
                                              MoveBuffer &buf)
  {
    auto sink = [&](const Move &m)
    { buf.push_unchecked(m); };
    generate_all_regular<GenMode::All>(b, st, sink);
    return buf.n;
  }

  void MoveGenerator::generateCapturesOnly(const Board &b, const GameState &st,
                                           std::vector<Move> &out) const
  {
    if (out.capacity() < SQ_NB)
      out.reserve(SQ_NB);
    out.clear();
    auto sink = [&](const Move &m)
    { out.push_back(m); };
    generate_all_regular<GenMode::CapturesPlusPromos>(b, st, sink);
  }

  int MoveGenerator::generateCapturesOnly(const Board &b, const GameState &st,
                                          MoveBuffer &buf)
  {
    auto sink = [&](const Move &m)
    { buf.push_unchecked(m); };
    generate_all_regular<GenMode::CapturesPlusPromos>(b, st, sink);
    return buf.n;
  }

  void MoveGenerator::generateEvasions(const Board &b, const GameState &st,
                                       std::vector<Move> &out) const
  {
    if (out.capacity() < 48)
      out.reserve(48);
    out.clear();

    const Color side = st.sideToMove;
    const bb::Bitboard occ = b.getAllPieces();
    PinInfo pins;
    compute_pins(b, side, occ, pins);

    auto emitV = [&](const Move &m) noexcept
    { out.push_back(m); };
    generateEvasions_T(b, st, pins, emitV);
  }

  int MoveGenerator::generateEvasions(const Board &b, const GameState &st, MoveBuffer &buf)
  {
    const Color side = st.sideToMove;
    const bb::Bitboard occ = b.getAllPieces();
    PinInfo pins;
    compute_pins(b, side, occ, pins);

    auto emitB = [&](const Move &m) noexcept
    { buf.push_unchecked(m); };
    generateEvasions_T(b, st, pins, emitB);
    return buf.n;
  }

}
