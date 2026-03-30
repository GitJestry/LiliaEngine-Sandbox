#pragma once
#include <cstdint>

#include "board.hpp"
#include "core/bitboard.hpp"
#include "game_state.hpp"

namespace lilia::chess
{

  namespace detail
  {

    consteval std::uint64_t splitmix64(std::uint64_t &x)
    {
      std::uint64_t z = (x += 0x9E3779B97F4A7C15ULL);
      z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
      z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
      return z ^ (z >> 31);
    }

    consteval std::uint64_t next(std::uint64_t &seed)
    {
      std::uint64_t v;
      do
      {
        v = splitmix64(seed);
      } while (v == 0);
      return v;
    }

    struct Tables
    {
      core::Bitboard piece[2][6][64];
      core::Bitboard castling[16];
      core::Bitboard epFile[8];
      core::Bitboard side;
      core::Bitboard epCaptureMask[2][64];
    };

    consteval Tables generate()
    {
      Tables t{};
      std::uint64_t seed = 0xC0FFEE123456789ULL;

      for (int c = 0; c < 2; ++c)
        for (int p = 0; p < 6; ++p)
          for (int s = 0; s < 64; ++s)
            t.piece[c][p][s] = next(seed);

      for (int i = 0; i < 16; ++i)
        t.castling[i] = next(seed);
      for (int f = 0; f < 8; ++f)
        t.epFile[f] = next(seed);
      t.side = next(seed);

      for (int s = 0; s < 64; ++s)
      {
        const core::Bitboard sq = core::sq_bb(static_cast<Square>(s));
        t.epCaptureMask[core::ci(Color::White)][s] = core::sw(sq) | core::se(sq);
        t.epCaptureMask[core::ci(Color::Black)][s] = core::nw(sq) | core::ne(sq);
      }

      return t;
    }

  } // namespace detail

  struct Zobrist
  {
    using Tables = detail::Tables;

    // Compile-time generated, ODR-safe in a header.
    static inline constexpr Tables tables = detail::generate();

    // Convenience refs (remain constant expressions).
    static constexpr auto &piece = tables.piece;
    static constexpr auto &castling = tables.castling;
    static constexpr auto &epFile = tables.epFile;
    static constexpr auto &side = tables.side;
    static constexpr auto &epCaptureMask = tables.epCaptureMask;

    static constexpr void init() noexcept {}
    static void init(std::uint64_t) = delete;

    static inline core::Bitboard epHashIfRelevant(const Board &b, const GameState &st) noexcept
    {
      const Square epSq = st.enPassantSquare;
      if (epSq == NO_SQUARE)
        return 0ULL;

      const int ep = static_cast<int>(epSq);
      const int file = ep & 7;

      const Color stm = st.sideToMove;
      const int ci = core::ci(stm);

      // If side-to-move has a pawn that can capture onto EP square, hash EP file.
      const core::Bitboard pawnsSTM = b.getPieces(stm, PieceType::Pawn);
      return (pawnsSTM & epCaptureMask[ci][ep]) ? epFile[file] : 0ULL;
    }

  private:
    // Single implementation to avoid template bloat for different PositionLike types.
    static inline core::Bitboard compute_from(const Board &b, const GameState &st) noexcept
    {
      core::Bitboard h = 0ULL;

      // Assumes PieceType values for Pawn..King map to 0..5 (your Board + Move code already relies on
      // this).
      for (int c = 0; c < 2; ++c)
      {
        const Color color = static_cast<Color>(c);
        for (int p = 0; p < 6; ++p)
        {
          const PieceType pt = static_cast<PieceType>(p);
          core::Bitboard bbp = b.getPieces(color, pt);

          while (bbp)
          {
            const Square s = core::pop_lsb_unchecked(bbp);
            h ^= piece[c][p][static_cast<int>(s)];
          }
        }
      }

      h ^= castling[st.castlingRights & 0xF];
      h ^= epHashIfRelevant(b, st);
      if (st.sideToMove == Color::Black)
        h ^= side;

      return h;
    }

  public:
    template <class PositionLike>
    static core::Bitboard compute(const PositionLike &pos) noexcept
    {
      const Board &b = pos.getBoard();
      const GameState &st = pos.getState();
      return compute_from(b, st);
    }

    static core::Bitboard computePawnKey(const Board &b) noexcept
    {
      core::Bitboard h = 0ULL;
      constexpr int pawnIdx = 0; // Pawn == 0

      for (int c = 0; c < 2; ++c)
      {
        const Color color = static_cast<Color>(c);
        core::Bitboard pawns = b.getPieces(color, PieceType::Pawn);

        while (pawns)
        {
          const Square s = core::pop_lsb_unchecked(pawns);
          h ^= piece[c][pawnIdx][static_cast<int>(s)];
        }
      }
      return h;
    }
  };

} // namespace lilia::model
