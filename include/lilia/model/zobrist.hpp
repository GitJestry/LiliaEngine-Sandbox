#pragma once
#include <cstdint>

#include "board.hpp"
#include "core/bitboard.hpp"
#include "game_state.hpp"

namespace lilia::model {

namespace detail {
consteval std::uint64_t splitmix64(std::uint64_t& x) {
  std::uint64_t z = (x += 0x9E3779B97F4A7C15ULL);
  z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
  z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
  return z ^ (z >> 31);
}

consteval std::uint64_t next(std::uint64_t& seed) {
  std::uint64_t v;
  do {
    v = splitmix64(seed);
  } while (v == 0);
  return v;
}

struct Tables {
  bb::Bitboard piece[2][6][64];
  bb::Bitboard castling[16];
  bb::Bitboard epFile[8];
  bb::Bitboard side;
  bb::Bitboard epCaptureMask[2][64];
};

consteval Tables generate() {
  Tables t{};
  std::uint64_t seed = 0xC0FFEE123456789ULL;

  for (int c = 0; c < 2; ++c)
    for (int p = 0; p < 6; ++p)
      for (int s = 0; s < 64; ++s)
        t.piece[c][p][s] = next(seed);

  for (int i = 0; i < 16; ++i) t.castling[i] = next(seed);
  for (int f = 0; f < 8; ++f) t.epFile[f] = next(seed);
  t.side = next(seed);

  for (int s = 0; s < 64; ++s) {
    const bb::Bitboard sq = bb::sq_bb(static_cast<core::Square>(s));
    t.epCaptureMask[bb::ci(core::Color::White)][s] = bb::sw(sq) | bb::se(sq);
    t.epCaptureMask[bb::ci(core::Color::Black)][s] = bb::nw(sq) | bb::ne(sq);
  }

  return t;
}
} // namespace detail

struct Zobrist {
  using Tables = detail::Tables;

  static inline constinit Tables tables = detail::generate();
  static constexpr auto& piece = tables.piece;
  static constexpr auto& castling = tables.castling;
  static constexpr auto& epFile = tables.epFile;
  static constexpr auto& side = tables.side;
  static constexpr auto& epCaptureMask = tables.epCaptureMask;

  static constexpr void init() noexcept {}
  static void init(std::uint64_t) = delete;

  static inline bb::Bitboard epHashIfRelevant(const Board& b, const GameState& st) noexcept {
    if (st.enPassantSquare == core::NO_SQUARE) return 0;
    const int ep = static_cast<int>(st.enPassantSquare);
    const int file = ep & 7;

    const auto stm = st.sideToMove;
    const int ci = bb::ci(stm);
    const bb::Bitboard pawnsSTM = b.getPieces(stm, core::PieceType::Pawn);
    if (!pawnsSTM) return 0;

    // enpessant?
    if (pawnsSTM & epCaptureMask[ci][ep]) return epFile[file];
    return 0;
  }

  template <class PositionLike>
  static bb::Bitboard compute(const PositionLike& pos) noexcept {
    bb::Bitboard h = 0;

    static constexpr core::PieceType PTs[6] = {core::PieceType::Pawn,   core::PieceType::Knight,
                                               core::PieceType::Bishop, core::PieceType::Rook,
                                               core::PieceType::Queen,  core::PieceType::King};

    const Board& b = pos.getBoard();

    for (int c = 0; c < 2; ++c) {
      const auto color = static_cast<core::Color>(c);
      for (int i = 0; i < 6; ++i) {
        const auto pt = PTs[i];
        const int pti = static_cast<int>(pt);
        bb::Bitboard bbp = b.getPieces(color, pt);
        while (bbp) {
          core::Square s = bb::pop_lsb(bbp);
          h ^= piece[c][pti][s];
        }
      }
    }

    const GameState& st = pos.getState();
    h ^= castling[st.castlingRights & 0xF];
    h ^= epHashIfRelevant(b, st);
    if (st.sideToMove == core::Color::Black) h ^= side;

    return h;
  }

  static bb::Bitboard computePawnKey(const Board& b) noexcept {
    bb::Bitboard h = 0;
    const int pawnIdx = static_cast<int>(core::PieceType::Pawn);

    for (int c = 0; c < 2; ++c) {
      const auto color = static_cast<core::Color>(c);
      bb::Bitboard pawns = b.getPieces(color, core::PieceType::Pawn);
      while (pawns) {
        core::Square s = bb::pop_lsb(pawns);
        h ^= piece[c][pawnIdx][s];
      }
    }
    return h;
  }
};

}  // namespace lilia::model
