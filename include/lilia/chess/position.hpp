#pragma once
#include <cstdint>
#include <vector>

#include "board.hpp"
#include "core/bitboard.hpp"
#include "game_state.hpp"
#include "zobrist.hpp"
#include "compiler.hpp"

namespace lilia::chess
{

  // Owns a full chess position and supports incremental make/unmake, hashing, and legality helpers.
  class Position
  {
  public:
    Position() = default;

    LILIA_ALWAYS_INLINE Board &getBoard() { return m_board; }
    LILIA_ALWAYS_INLINE const Board &getBoard() const { return m_board; }
    LILIA_ALWAYS_INLINE GameState &getState() { return m_state; }
    LILIA_ALWAYS_INLINE const GameState &getState() const { return m_state; }

    // Recompute the full hash and pawnKey from the current position
    [[nodiscard]] LILIA_ALWAYS_INLINE std::uint64_t hash() const noexcept
    {
      return static_cast<std::uint64_t>(m_hash);
    }
    [[nodiscard]] LILIA_ALWAYS_INLINE bool lastMoveGaveCheck() const noexcept
    {
      return !m_history.empty() && m_history.back().gaveCheck != 0;
    }

    void buildHash()
    {
      m_hash = Zobrist::compute(*this);

      // Rebuild pawnKey
      bb::Bitboard pk = 0;
      for (auto c : {Color::White, Color::Black})
      {
        bb::Bitboard pawns = m_board.getPieces(c, PieceType::Pawn);
        while (pawns)
        {
          Square s = bb::pop_lsb(pawns);
          pk ^= Zobrist::piece[bb::ci(c)][static_cast<int>(PieceType::Pawn)][s];
        }
      }
      m_state.pawnKey = pk;
    }

    bool doMove(const Move &m);
    void undoMove();
    bool doNullMove();
    void undoNullMove();

    bool checkInsufficientMaterial();
    bool checkMoveRule();
    bool checkRepetition();

    bool inCheck() const;
    bool isPseudoLegal(const Move &m) const;

  private:
    Board m_board;
    GameState m_state;
    std::vector<StateInfo> m_history;
    bb::Bitboard m_hash = 0;
    std::vector<NullState> m_null_history;

    // Internal helpers
    void applyMove(const Move &m, StateInfo &st);
    void unapplyMove(const StateInfo &st);

    // Incremental Zobrist / pawnKey updates
    LILIA_ALWAYS_INLINE void hashXorPiece(Color c, PieceType pt, Square s)
    {
      m_hash ^= Zobrist::piece[bb::ci(c)][static_cast<int>(pt)][s];
      if (pt == PieceType::Pawn)
      {
        m_state.pawnKey ^= Zobrist::piece[bb::ci(c)][static_cast<int>(PieceType::Pawn)][s];
      }
    }
    LILIA_ALWAYS_INLINE void hashXorSide() { m_hash ^= Zobrist::side; }
    LILIA_ALWAYS_INLINE void hashSetCastling(std::uint8_t prev, std::uint8_t next)
    {
      m_hash ^= Zobrist::castling[prev & 0xF];
      m_hash ^= Zobrist::castling[next & 0xF];
    }

    // XOR the EP hash only if en passant is relevant for the current state.
    // Important: call this BEFORE state changes to remove the "old" value from the hash,
    // and call it AGAIN AFTER all state changes to add the "new" value.
    LILIA_ALWAYS_INLINE void xorEPRelevant()
    {
      const auto ep = m_state.enPassantSquare;
      if (LILIA_UNLIKELY(ep == NO_SQUARE))
        return;

      const auto stm = m_state.sideToMove;
      const bb::Bitboard pawnsSTM = m_board.getPieces(stm, PieceType::Pawn);
      if (!pawnsSTM)
        return; // nothing to do

      const int epIdx = static_cast<int>(ep);
      const int file = epIdx & 7;
      const int ci = bb::ci(stm);

      if (pawnsSTM & Zobrist::epCaptureMask[ci][epIdx])
        m_hash ^= Zobrist::epFile[file];
    }
  };

}
