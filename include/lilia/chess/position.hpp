#pragma once
#include <cstdint>
#include <utility>

#include "board.hpp"
#include "core/bitboard.hpp"
#include "game_state.hpp"
#include "zobrist.hpp"
#include "compiler.hpp"

namespace lilia::chess
{

  class Position
  {
  public:
    Position() = default;

    Position(const Position &other)
        : m_board(other.m_board),
          m_rootState(other.stateInfo()),
          m_st(&m_rootState)
    {
      m_rootState.previous = nullptr;
    }

    Position(Position &&other) noexcept
        : m_board(std::move(other.m_board)),
          m_rootState(other.stateInfo()),
          m_st(&m_rootState)
    {
      m_rootState.previous = nullptr;
    }

    Position &operator=(const Position &other)
    {
      if (this == &other)
        return *this;

      m_board = other.m_board;
      m_rootState = other.stateInfo();
      m_rootState.previous = nullptr;
      m_st = &m_rootState;
      return *this;
    }

    Position &operator=(Position &&other) noexcept
    {
      if (this == &other)
        return *this;

      m_board = std::move(other.m_board);
      m_rootState = other.stateInfo();
      m_rootState.previous = nullptr;
      m_st = &m_rootState;
      return *this;
    }

    LILIA_ALWAYS_INLINE Board &getBoard() { return m_board; }
    LILIA_ALWAYS_INLINE const Board &getBoard() const { return m_board; }

    LILIA_ALWAYS_INLINE GameState &getState() { return *m_st; }
    LILIA_ALWAYS_INLINE const GameState &getState() const { return *m_st; }

    LILIA_ALWAYS_INLINE StateInfo &stateInfo() { return *m_st; }
    LILIA_ALWAYS_INLINE const StateInfo &stateInfo() const { return *m_st; }

    [[nodiscard]] LILIA_ALWAYS_INLINE std::uint64_t hash() const noexcept
    {
      return static_cast<std::uint64_t>(m_st->zobristKey);
    }

    [[nodiscard]] LILIA_ALWAYS_INLINE bool lastMoveGaveCheck() const noexcept
    {
      return m_st->gaveCheck != 0;
    }

    void buildHash()
    {
      m_st->zobristKey = Zobrist::compute(*this);
      m_st->pawnKey = Zobrist::computePawnKey(m_board);
    }

    // Stockfish-style API: caller provides the next state node.
    bool doMove(const Move &m, StateInfo &newState);
    void undoMove();

    bool doNullMove(StateInfo &newState);
    void undoNullMove();

    bool checkInsufficientMaterial();
    bool checkMoveRule();
    bool checkRepetition();

    bool inCheck() const;
    bool isPseudoLegal(const Move &m) const;

  private:
    Board m_board;
    StateInfo m_rootState{};
    StateInfo *m_st = &m_rootState;

    void applyMove(const Move &m, StateInfo &st);
    void unapplyMove(const StateInfo &st);

    LILIA_ALWAYS_INLINE void hashXorPiece(StateInfo &st, Color c, PieceType pt, Square s) noexcept
    {
      st.zobristKey ^= Zobrist::piece[bb::ci(c)][static_cast<int>(pt)][s];
      if (pt == PieceType::Pawn)
        st.pawnKey ^= Zobrist::piece[bb::ci(c)][static_cast<int>(PieceType::Pawn)][s];
    }

    LILIA_ALWAYS_INLINE void hashXorSide(StateInfo &st) noexcept
    {
      st.zobristKey ^= Zobrist::side;
    }

    LILIA_ALWAYS_INLINE void hashSetCastling(StateInfo &st, std::uint8_t prev, std::uint8_t next) noexcept
    {
      st.zobristKey ^= Zobrist::castling[prev & 0xF];
      st.zobristKey ^= Zobrist::castling[next & 0xF];
    }

    // XOR the EP file only when EP is actually capturable by sideToMove.
    // This must match Zobrist::compute().
    LILIA_ALWAYS_INLINE void xorEPRelevant(StateInfo &st) const noexcept
    {
      const auto ep = st.enPassantSquare;
      if (LILIA_UNLIKELY(ep == NO_SQUARE))
        return;

      const Color stm = st.sideToMove;
      const bb::Bitboard pawnsSTM = m_board.getPieces(stm, PieceType::Pawn);
      if (!pawnsSTM)
        return;

      const int epIdx = static_cast<int>(ep);
      if (pawnsSTM & Zobrist::epCaptureMask[bb::ci(stm)][epIdx])
        st.zobristKey ^= Zobrist::epFile[epIdx & 7];
    }
  };

}
