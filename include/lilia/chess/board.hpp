#pragma once

#include <array>
#include <cstdint>
#include <optional>

#include "core/bitboard.hpp"
#include "lilia/chess/compiler.hpp"

namespace lilia::chess
{

  /**
   * Stores piece placement as both bitboards and packed per-square data.
   * This is the central mutable board container used by move making and queries.
   */
  class Board
  {
  public:
    Board();

    void clear() noexcept;

    void setPiece(Square sq, Piece p) noexcept;
    void removePiece(Square sq) noexcept;
    std::optional<Piece> getPiece(Square sq) const noexcept;

    LILIA_ALWAYS_INLINE bb::Bitboard getPieces(Color c) const noexcept
    {
      return m_color_occ[bb::ci(c)];
    }

    LILIA_ALWAYS_INLINE bb::Bitboard getAllPieces() const noexcept
    {
      return m_all_occ;
    }

    LILIA_ALWAYS_INLINE bb::Bitboard getPieces(Color c, PieceType t) const noexcept
    {
      const int ti = bb::type_index(t);
      return LILIA_UNLIKELY(ti < 0) ? 0ULL : m_bb[bb::ci(c)][ti];
    }

    void movePiece_noCapture(Square from, Square to) noexcept;

    // Supports normal captures and en passant by separating target and captured square.
    void movePiece_withCapture(Square from, Square cap_sq, Square to, Piece captured) noexcept;

    LILIA_ALWAYS_INLINE std::uint8_t getPiecePacked(Square sq) const noexcept
    {
      return m_piece_on[static_cast<int>(sq)];
    }

    LILIA_ALWAYS_INLINE bool isEmpty(Square sq) const noexcept
    {
      return m_piece_on[static_cast<int>(sq)] == 0;
    }

  private:
    std::array<std::array<bb::Bitboard, PIECE_TYPE_NB>, 2> m_bb{};
    std::array<bb::Bitboard, 2> m_color_occ{};
    bb::Bitboard m_all_occ = 0;
    std::array<std::uint8_t, SQ_NB> m_piece_on{};

    static constexpr std::uint8_t pack_piece(Piece p) noexcept;
    static constexpr Piece unpack_piece(std::uint8_t pp) noexcept;
  };

}
