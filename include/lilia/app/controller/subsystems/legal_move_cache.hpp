#pragma once

#include <vector>

#include "lilia/chess/move.hpp"

namespace lilia::chess
{
  class ChessGame;
}

namespace lilia::app::controller
{

  /// Adapts on the game move generation and simplifies logic.
  class LegalMoveCache
  {
  public:
    explicit LegalMoveCache(chess::ChessGame &game) : m_game(game) {}

    void invalidate() { m_cached = nullptr; }

    const std::vector<chess::Move> &legal() const;
    bool contains(chess::Square from, chess::Square to) const;
    bool isPromotion(chess::Square from, chess::Square to) const;

  private:
    chess::ChessGame &m_game;
    mutable const std::vector<chess::Move> *m_cached{nullptr};
  };

}
