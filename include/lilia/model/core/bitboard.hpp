#pragma once
#include <array>
#include <bit>
#include <cstdint>

#include "model_types.hpp"

#if defined(_MSC_VER)
#define LILIA_ALWAYS_INLINE __forceinline
#elif defined(__GNUC__) || defined(__clang__)
#define LILIA_ALWAYS_INLINE inline __attribute__((always_inline))
#else
#define LILIA_ALWAYS_INLINE inline
#endif

namespace lilia::model::bb {

constexpr inline bool any(Bitboard b) noexcept {
  return b != 0;
}
constexpr inline bool none(Bitboard b) noexcept {
  return b == 0;
}

constexpr inline int popcount(Bitboard b) noexcept {
  return std::popcount(b);
}

LILIA_ALWAYS_INLINE int ctz64(std::uint64_t x) noexcept {
  return static_cast<int>(std::countr_zero(x));
}

LILIA_ALWAYS_INLINE int clz64(std::uint64_t x) noexcept {
  return static_cast<int>(std::countl_zero(x));
}

LILIA_ALWAYS_INLINE core::Square pop_lsb(Bitboard& b) noexcept {
  if (!b) return core::NO_SQUARE;
  const int idx = ctz64(b);
  b &= (b - 1);
  return static_cast<core::Square>(idx);
}

constexpr inline Bitboard north(Bitboard b) noexcept {
  return b << 8;
}
constexpr inline Bitboard south(Bitboard b) noexcept {
  return b >> 8;
}
constexpr inline Bitboard east(Bitboard b) noexcept {
  return (b & ~FILE_H) << 1;
}
constexpr inline Bitboard west(Bitboard b) noexcept {
  return (b & ~FILE_A) >> 1;
}
constexpr inline Bitboard ne(Bitboard b) noexcept {
  return (b & ~FILE_H) << 9;
}
constexpr inline Bitboard nw(Bitboard b) noexcept {
  return (b & ~FILE_A) << 7;
}
constexpr inline Bitboard se(Bitboard b) noexcept {
  return (b & ~FILE_H) >> 7;
}
constexpr inline Bitboard sw(Bitboard b) noexcept {
  return (b & ~FILE_A) >> 9;
}

namespace detail {

template <Bitboard (*Step)(Bitboard)>
LILIA_ALWAYS_INLINE constexpr Bitboard ray_attack_dir_fast(Bitboard from, Bitboard occ) noexcept {
  Bitboard atk = 0;
  Bitboard r = Step(from);
  while (r) {
    atk |= r;
    if (r & occ) break;
    r = Step(r);
  }
  return atk;
}

constexpr Bitboard knight_from_sq(core::Square s) noexcept {
  Bitboard b = sq_bb(s);
  Bitboard l1 = (b & ~FILE_A) >> 1;
  Bitboard l2 = (b & ~(FILE_A | FILE_B)) >> 2;
  Bitboard r1 = (b & ~FILE_H) << 1;
  Bitboard r2 = (b & ~(FILE_H | FILE_G)) << 2;
  return (l2 << 8) | (l2 >> 8) | (r2 << 8) | (r2 >> 8) | (l1 << 16) | (l1 >> 16) | (r1 << 16) |
         (r1 >> 16);
}

constexpr Bitboard king_from_sq(core::Square s) noexcept {
  Bitboard b = sq_bb(s);
  return east(b) | west(b) | north(b) | south(b) | ne(b) | nw(b) | se(b) | sw(b);
}

constexpr auto build_knight_table() noexcept {
  std::array<Bitboard, 64> t{};
  for (int i = 0; i < 64; ++i) t[i] = knight_from_sq(static_cast<core::Square>(i));
  return t;
}

constexpr auto build_king_table() noexcept {
  std::array<Bitboard, 64> t{};
  for (int i = 0; i < 64; ++i) t[i] = king_from_sq(static_cast<core::Square>(i));
  return t;
}

inline constexpr auto KNIGHT_ATTACKS = build_knight_table();
inline constexpr auto KING_ATTACKS = build_king_table();

}  // namespace detail

constexpr inline Bitboard knight_attacks_from(core::Square s) noexcept {
  return detail::KNIGHT_ATTACKS[static_cast<int>(s)];
}

constexpr inline Bitboard king_attacks_from(core::Square s) noexcept {
  return detail::KING_ATTACKS[static_cast<int>(s)];
}

constexpr inline Bitboard ray_attack_dir(Bitboard from, Bitboard occ, Bitboard (*step)(Bitboard)) {
  if (step == ne) return detail::ray_attack_dir_fast<ne>(from, occ);
  if (step == nw) return detail::ray_attack_dir_fast<nw>(from, occ);
  if (step == se) return detail::ray_attack_dir_fast<se>(from, occ);
  if (step == sw) return detail::ray_attack_dir_fast<sw>(from, occ);
  if (step == north) return detail::ray_attack_dir_fast<north>(from, occ);
  if (step == south) return detail::ray_attack_dir_fast<south>(from, occ);
  if (step == east) return detail::ray_attack_dir_fast<east>(from, occ);
  if (step == west) return detail::ray_attack_dir_fast<west>(from, occ);

  Bitboard atk = 0, r = step(from);
  while (r) {
    atk |= r;
    if (r & occ) break;
    r = step(r);
  }
  return atk;
}

constexpr inline Bitboard bishop_attacks(core::Square s, Bitboard occ) noexcept {
  Bitboard from = sq_bb(s);
  return detail::ray_attack_dir_fast<ne>(from, occ) | detail::ray_attack_dir_fast<nw>(from, occ) |
         detail::ray_attack_dir_fast<se>(from, occ) | detail::ray_attack_dir_fast<sw>(from, occ);
}

constexpr inline Bitboard rook_attacks(core::Square s, Bitboard occ) noexcept {
  Bitboard from = sq_bb(s);
  return detail::ray_attack_dir_fast<north>(from, occ) |
         detail::ray_attack_dir_fast<south>(from, occ) |
         detail::ray_attack_dir_fast<east>(from, occ) |
         detail::ray_attack_dir_fast<west>(from, occ);
}

constexpr inline Bitboard queen_attacks(core::Square s, Bitboard occ) noexcept {
  return bishop_attacks(s, occ) | rook_attacks(s, occ);
}

constexpr inline Bitboard white_pawn_attacks(Bitboard pawns) noexcept {
  return (nw(pawns) | ne(pawns));
}
constexpr inline Bitboard black_pawn_attacks(Bitboard pawns) noexcept {
  return (sw(pawns) | se(pawns));
}

}  // namespace lilia::model::bb
