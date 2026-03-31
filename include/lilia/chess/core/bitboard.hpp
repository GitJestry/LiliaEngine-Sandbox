#pragma once
#include <array>
#include <bit>
#include <cstdint>

#include "lilia/chess/chess_types.hpp"
#include "lilia/chess/compiler.hpp"

namespace lilia::chess::bb
{
  using Bitboard = std::uint64_t;

  [[nodiscard]] LILIA_ALWAYS_INLINE constexpr int ci(Color c) noexcept
  {
    return c == Color::White ? 0 : 1;
  }

  [[nodiscard]] LILIA_ALWAYS_INLINE constexpr int file_of(Square s) noexcept { return s & 7; }
  [[nodiscard]] LILIA_ALWAYS_INLINE constexpr int rank_of(Square s) noexcept { return s >> 3; }

  [[nodiscard]] LILIA_ALWAYS_INLINE constexpr Bitboard sq_bb(Square s) noexcept
  {
    return Bitboard{1} << static_cast<unsigned>(s);
  }

  [[nodiscard]] LILIA_ALWAYS_INLINE constexpr int type_index(PieceType t) noexcept
  {
    const int ti = static_cast<int>(t);
    return (ti >= 0 && ti < 6) ? ti : -1;
  }

  constexpr Bitboard FILE_A = 0x0101010101010101ULL;
  constexpr Bitboard FILE_B = 0x0202020202020202ULL;
  constexpr Bitboard FILE_C = 0x0404040404040404ULL;
  constexpr Bitboard FILE_D = 0x0808080808080808ULL;
  constexpr Bitboard FILE_E = 0x1010101010101010ULL;
  constexpr Bitboard FILE_F = 0x2020202020202020ULL;
  constexpr Bitboard FILE_G = 0x4040404040404040ULL;
  constexpr Bitboard FILE_H = 0x8080808080808080ULL;

  constexpr Bitboard RANK_1 = 0x00000000000000FFULL;
  constexpr Bitboard RANK_2 = 0x000000000000FF00ULL;
  constexpr Bitboard RANK_3 = 0x0000000000FF0000ULL;
  constexpr Bitboard RANK_4 = 0x00000000FF000000ULL;
  constexpr Bitboard RANK_5 = 0x000000FF00000000ULL;
  constexpr Bitboard RANK_6 = 0x0000FF0000000000ULL;
  constexpr Bitboard RANK_7 = 0x00FF000000000000ULL;
  constexpr Bitboard RANK_8 = 0xFF00000000000000ULL;

  constexpr Square A1 = 0, D1 = 3, E1 = 4, F1 = 5, H1 = 7;
  constexpr Square A8 = 56, D8 = 59, E8 = 60, F8 = 61, H8 = 63;

  [[nodiscard]] LILIA_ALWAYS_INLINE constexpr bool any(Bitboard b) noexcept { return b != 0; }
  [[nodiscard]] LILIA_ALWAYS_INLINE constexpr bool none(Bitboard b) noexcept { return b == 0; }
  [[nodiscard]] LILIA_ALWAYS_INLINE constexpr int popcount(Bitboard b) noexcept { return std::popcount(b); }
  [[nodiscard]] LILIA_ALWAYS_INLINE constexpr int ctz64(std::uint64_t x) noexcept { return static_cast<int>(std::countr_zero(x)); }
  [[nodiscard]] LILIA_ALWAYS_INLINE constexpr int clz64(std::uint64_t x) noexcept { return static_cast<int>(std::countl_zero(x)); }

  [[nodiscard]] LILIA_ALWAYS_INLINE Square pop_lsb_unchecked(Bitboard &b) noexcept
  {
    LILIA_ASSUME(b != 0);
    const int i = ctz64(b);
    b &= (b - 1);
    return static_cast<Square>(i);
  }

  [[nodiscard]] LILIA_ALWAYS_INLINE Square pop_lsb(Bitboard &b) noexcept
  {
    if (LILIA_UNLIKELY(!b))
      return NO_SQUARE;
    return pop_lsb_unchecked(b);
  }

  [[nodiscard]] LILIA_ALWAYS_INLINE constexpr Bitboard north(Bitboard b) noexcept { return b << 8; }
  [[nodiscard]] LILIA_ALWAYS_INLINE constexpr Bitboard south(Bitboard b) noexcept { return b >> 8; }
  [[nodiscard]] LILIA_ALWAYS_INLINE constexpr Bitboard east(Bitboard b) noexcept { return (b & ~FILE_H) << 1; }
  [[nodiscard]] LILIA_ALWAYS_INLINE constexpr Bitboard west(Bitboard b) noexcept { return (b & ~FILE_A) >> 1; }
  [[nodiscard]] LILIA_ALWAYS_INLINE constexpr Bitboard ne(Bitboard b) noexcept { return (b & ~FILE_H) << 9; }
  [[nodiscard]] LILIA_ALWAYS_INLINE constexpr Bitboard nw(Bitboard b) noexcept { return (b & ~FILE_A) << 7; }
  [[nodiscard]] LILIA_ALWAYS_INLINE constexpr Bitboard se(Bitboard b) noexcept { return (b & ~FILE_H) >> 7; }
  [[nodiscard]] LILIA_ALWAYS_INLINE constexpr Bitboard sw(Bitboard b) noexcept { return (b & ~FILE_A) >> 9; }

  namespace detail
  {
    template <Bitboard (*Step)(Bitboard)>
    [[nodiscard]] LILIA_ALWAYS_INLINE constexpr Bitboard ray_attack_dir_fast(Bitboard from, Bitboard occ) noexcept
    {
      Bitboard atk = 0;
      Bitboard r = Step(from);
      while (r)
      {
        atk |= r;
        if (r & occ)
          break;
        r = Step(r);
      }
      return atk;
    }

    constexpr Bitboard knight_from_sq(Square s) noexcept
    {
      Bitboard b = sq_bb(s);
      Bitboard l1 = (b & ~FILE_A) >> 1;
      Bitboard l2 = (b & ~(FILE_A | FILE_B)) >> 2;
      Bitboard r1 = (b & ~FILE_H) << 1;
      Bitboard r2 = (b & ~(FILE_H | FILE_G)) << 2;
      return (l2 << 8) | (l2 >> 8) | (r2 << 8) | (r2 >> 8) |
             (l1 << 16) | (l1 >> 16) | (r1 << 16) | (r1 >> 16);
    }

    constexpr Bitboard king_from_sq(Square s) noexcept
    {
      Bitboard b = sq_bb(s);
      return east(b) | west(b) | north(b) | south(b) | ne(b) | nw(b) | se(b) | sw(b);
    }

    constexpr auto build_knight_table() noexcept
    {
      std::array<Bitboard, 64> t{};
      for (int i = 0; i < 64; ++i)
        t[i] = knight_from_sq(static_cast<Square>(i));
      return t;
    }

    constexpr auto build_king_table() noexcept
    {
      std::array<Bitboard, 64> t{};
      for (int i = 0; i < 64; ++i)
        t[i] = king_from_sq(static_cast<Square>(i));
      return t;
    }

    inline constexpr auto KNIGHT_ATTACKS = build_knight_table();
    inline constexpr auto KING_ATTACKS = build_king_table();
  }

  [[nodiscard]] LILIA_ALWAYS_INLINE constexpr Bitboard knight_attacks_from(Square s) noexcept
  {
    return detail::KNIGHT_ATTACKS[static_cast<int>(s)];
  }

  [[nodiscard]] LILIA_ALWAYS_INLINE constexpr Bitboard king_attacks_from(Square s) noexcept
  {
    return detail::KING_ATTACKS[static_cast<int>(s)];
  }

  [[nodiscard]] LILIA_ALWAYS_INLINE constexpr Bitboard bishop_attacks(Square s, Bitboard occ) noexcept
  {
    const Bitboard from = sq_bb(s);
    return detail::ray_attack_dir_fast<ne>(from, occ) |
           detail::ray_attack_dir_fast<nw>(from, occ) |
           detail::ray_attack_dir_fast<se>(from, occ) |
           detail::ray_attack_dir_fast<sw>(from, occ);
  }

  [[nodiscard]] LILIA_ALWAYS_INLINE constexpr Bitboard rook_attacks(Square s, Bitboard occ) noexcept
  {
    const Bitboard from = sq_bb(s);
    return detail::ray_attack_dir_fast<north>(from, occ) |
           detail::ray_attack_dir_fast<south>(from, occ) |
           detail::ray_attack_dir_fast<east>(from, occ) |
           detail::ray_attack_dir_fast<west>(from, occ);
  }

  [[nodiscard]] LILIA_ALWAYS_INLINE constexpr Bitboard queen_attacks(Square s, Bitboard occ) noexcept
  {
    return bishop_attacks(s, occ) | rook_attacks(s, occ);
  }

  [[nodiscard]] LILIA_ALWAYS_INLINE constexpr Bitboard white_pawn_attacks(Bitboard pawns) noexcept
  {
    return nw(pawns) | ne(pawns);
  }

  [[nodiscard]] LILIA_ALWAYS_INLINE constexpr Bitboard black_pawn_attacks(Bitboard pawns) noexcept
  {
    return sw(pawns) | se(pawns);
  }
} // namespace lilia::chess::bb
