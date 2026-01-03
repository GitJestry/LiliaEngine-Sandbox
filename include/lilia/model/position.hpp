#pragma once
#include <cstdint>
#include <vector>

#include "../engine/eval_acc.hpp"
#include "board.hpp"
#include "core/bitboard.hpp"
#include "game_state.hpp"
#include "zobrist.hpp"

namespace lilia::model {

class Position {
 public:
  Position() = default;

  Board& getBoard() { return m_board; }
  const Board& getBoard() const { return m_board; }
  GameState& getState() { return m_state; }
  const GameState& getState() const { return m_state; }

  // Recompute the full hash and pawnKey from the current position
  [[nodiscard]] inline std::uint64_t hash() const noexcept {
    return static_cast<std::uint64_t>(m_hash);
  }
  [[nodiscard]] inline bool lastMoveGaveCheck() const noexcept {
    return !m_history.empty() && m_history.back().gaveCheck != 0;
  }

  // buildHash(): fast & without iterating over all squares
  void buildHash() {
    // Full hash (including EP relevance!) — NOTE: Zobrist::compute(*this) must use the same EP logic
    m_hash = Zobrist::compute(*this);

    // Rebuild pawnKey
    bb::Bitboard pk = 0;
    for (auto c : {core::Color::White, core::Color::Black}) {
      bb::Bitboard pawns = m_board.getPieces(c, core::PieceType::Pawn);
      while (pawns) {
        core::Square s = bb::pop_lsb(pawns);
        pk ^= Zobrist::piece[bb::ci(c)][static_cast<int>(core::PieceType::Pawn)][s];
      }
    }
    m_state.pawnKey = pk;
  }

  // Make/Unmake
  bool doMove(const Move& m);
  void undoMove();
  bool doNullMove();
  void undoNullMove();

  // Status queries
  bool checkInsufficientMaterial();
  bool checkMoveRule();
  bool checkRepetition();

  bool inCheck() const;
  /// Static exchange evaluation. Simulates the capture sequence on the
  /// destination square (also for quiet moves) and returns true if the net
  /// material gain is non-negative.
  bool see(const model::Move& m) const;
  bool isPseudoLegal(const Move& m) const;

  const engine::EvalAcc& getEvalAcc() const noexcept { return evalAcc_; }
  void rebuildEvalAcc() { evalAcc_.build_from_board(m_board); }

 private:
  Board m_board;
  GameState m_state;
  std::vector<StateInfo> m_history;
  bb::Bitboard m_hash = 0;
  engine::EvalAcc evalAcc_;
  std::vector<NullState> m_null_history;

  // Internal helpers
  void applyMove(const Move& m, StateInfo& st);
  void unapplyMove(const StateInfo& st);

  // Incremental Zobrist / pawnKey updates
  inline void hashXorPiece(core::Color c, core::PieceType pt, core::Square s) {
    m_hash ^= Zobrist::piece[bb::ci(c)][static_cast<int>(pt)][s];
    if (pt == core::PieceType::Pawn) {
      m_state.pawnKey ^= Zobrist::piece[bb::ci(c)][static_cast<int>(core::PieceType::Pawn)][s];
    }
  }
  inline void hashXorSide() { m_hash ^= Zobrist::side; }
  inline void hashSetCastling(std::uint8_t prev, std::uint8_t next) {
    m_hash ^= Zobrist::castling[prev & 0xF];
    m_hash ^= Zobrist::castling[next & 0xF];
  }

  // XOR the EP hash only if en passant is relevant for the current state.
  // Important: call this BEFORE state changes to remove the "old" value from the hash,
  // and call it AGAIN AFTER all state changes to add the "new" value.
  void xorEPRelevant() {
    const auto ep = m_state.enPassantSquare;
    if (ep == core::NO_SQUARE) return;

    const auto stm = m_state.sideToMove;
    const bb::Bitboard pawnsSTM = m_board.getPieces(stm, core::PieceType::Pawn);
    if (!pawnsSTM) return;  // nothing to do

    const int epIdx = static_cast<int>(ep);
    const int file = epIdx & 7;
    const int ci = bb::ci(stm);

    if (pawnsSTM & Zobrist::epCaptureMask[ci][epIdx]) m_hash ^= Zobrist::epFile[file];
  }
};

}  // namespace lilia::model
