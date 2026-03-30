#pragma once

#include <array>
#include <cassert>
#include <utility>

#include "config.hpp"
#include "eval_acc.hpp"
#include "lilia/chess/position.hpp"

namespace lilia::engine
{
  class SearchPosition
  {
  public:
    SearchPosition() = default;

    explicit SearchPosition(const chess::Position &pos)
        : m_pos(pos)
    {
      m_eval.build_from_board(m_pos.getBoard());
    }

    explicit SearchPosition(chess::Position &&pos)
        : m_pos(std::move(pos))
    {
      m_eval.build_from_board(m_pos.getBoard());
    }

    // ---- pure chess accessors ----
    const chess::Position &position() const noexcept { return m_pos; }

    const chess::Board &getBoard() const noexcept { return m_pos.getBoard(); }
    const chess::GameState &getState() const noexcept { return m_pos.getState(); }

    std::uint64_t hash() const noexcept { return m_pos.hash(); }
    bool lastMoveGaveCheck() const noexcept { return m_pos.lastMoveGaveCheck(); }

    bool checkInsufficientMaterial() { return m_pos.checkInsufficientMaterial(); }
    bool checkMoveRule() { return m_pos.checkMoveRule(); }
    bool checkRepetition() { return m_pos.checkRepetition(); }

    bool inCheck() const { return m_pos.inCheck(); }
    bool see(const chess::Move &m) const { return m_pos.see(m); }
    bool isPseudoLegal(const chess::Move &m) const { return m_pos.isPseudoLegal(m); }

    // ---- engine eval access ----
    const EvalAcc &evalAcc() const noexcept { return m_eval; }
    void rebuildEvalAcc() { m_eval.build_from_board(m_pos.getBoard()); }

    // ---- incremental make/unmake ----
    bool doMove(const chess::Move &m);
    void undoMove();

    bool doNullMove();
    void undoNullMove();

  private:
    static void applyEvalDelta(const chess::Position &posBefore,
                               const chess::Move &m,
                               EvalAcc &eval);

  private:
    static constexpr int EVAL_STACK_CAP = MAX_PLY + 8;

    chess::Position m_pos;
    EvalAcc m_eval{};

    std::array<EvalAcc, EVAL_STACK_CAP> m_evalStack{};
    int m_evalTop = 0;
  };
} // namespace lilia::engine
