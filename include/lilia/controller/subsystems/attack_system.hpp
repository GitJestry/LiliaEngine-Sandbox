#pragma once

#include <vector>

#include "lilia/model/move.hpp"
#include "lilia/model/move_generator.hpp"

namespace lilia::model
{
  class ChessGame;
} // namespace lilia::model

namespace lilia::view
{
  class GameView;
}

namespace lilia::controller
{

  class LegalMoveCache;

  /// An attack system only for the board input system, to find possible pseudo attack squares of a piece
  class AttackSystem
  {
  public:
    AttackSystem(view::GameView &view, model::ChessGame &game, LegalMoveCache &legal);

    /// @brief it looks up wether pieceSq is a piece and finds its relative pseudo attacks.
    /// because premoving is possible, the current visual state of the board is
    /// @param pieceSq
    /// @return the possible pseudo attack squares of the Piece
    const std::vector<core::Square> &attacks(core::Square pieceSq) const;

  private:
    view::GameView &m_view;
    model::ChessGame &m_game;
    LegalMoveCache &m_legal;

    mutable model::MoveGenerator m_movegen;
    mutable std::vector<model::Move> m_pseudo;
    mutable std::vector<core::Square> m_out;
  };

} // namespace lilia::controller
