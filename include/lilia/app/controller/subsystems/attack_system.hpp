#pragma once

#include <vector>

#include "lilia/chess/move.hpp"
#include "lilia/chess/move_generator.hpp"

namespace lilia::chess
{
  class ChessGame;
}

namespace lilia::app::view::ui
{
  class GameView;
}

namespace lilia::app::controller
{

  class LegalMoveCache;

  /// An attack system only for the board input system, to find possible pseudo attack squares of a piece
  class AttackSystem
  {
  public:
    AttackSystem(view::ui::GameView &view, chess::ChessGame &game, LegalMoveCache &legal);

    /// @brief it looks up wether pieceSq is a piece and finds its relative pseudo attacks.
    /// because premoving is possible, the current visual state of the board is
    /// @param pieceSq
    /// @return the possible pseudo attack squares of the Piece
    const std::vector<chess::Square> &attacks(chess::Square pieceSq) const;

  private:
    view::ui::GameView &m_view;
    chess::ChessGame &m_game;
    LegalMoveCache &m_legal;

    mutable chess::MoveGenerator m_movegen;
    mutable std::vector<chess::Move> m_pseudo;
    mutable std::vector<chess::Square> m_out;
  };

}
