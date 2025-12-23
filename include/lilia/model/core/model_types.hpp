#pragma once
#include <cstdint>

#include "../../chess_types.hpp"

#if defined(_MSC_VER)
#define LILIA_ALWAYS_INLINE __forceinline
#elif defined(__GNUC__) || defined(__clang__)
#define LILIA_ALWAYS_INLINE inline __attribute__((always_inline))
#else
#define LILIA_ALWAYS_INLINE inline
#endif

namespace lilia::model::bb {

using Bitboard = std::uint64_t;

struct Piece {
  core::PieceType type = core::PieceType::None;
  core::Color color = core::Color::White;
  [[nodiscard]] constexpr bool isNone() const noexcept { return type == core::PieceType::None; }
};

[[nodiscard]] LILIA_ALWAYS_INLINE constexpr int ci(core::Color c) noexcept {
  return c == core::Color::White ? 0 : 1;
}

[[nodiscard]] LILIA_ALWAYS_INLINE constexpr int file_of(core::Square s) noexcept {
  return s & 7;
}
[[nodiscard]] LILIA_ALWAYS_INLINE constexpr int rank_of(core::Square s) noexcept {
  return s >> 3;
}

[[nodiscard]] LILIA_ALWAYS_INLINE constexpr Bitboard sq_bb(core::Square s) noexcept {
  // Caller responsibility: s must be 0..63.
  return Bitboard{1} << static_cast<unsigned>(s);
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

constexpr core::Square A1 = 0, D1 = 3, E1 = 4, F1 = 5, H1 = 7;
constexpr core::Square A8 = 56, D8 = 59, E8 = 60, F8 = 61, H8 = 63;

enum Castling : std::uint8_t { WK = 1 << 0, WQ = 1 << 1, BK = 1 << 2, BQ = 1 << 3 };

}  // namespace lilia::model::bb
