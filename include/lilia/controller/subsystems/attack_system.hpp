#pragma once

#include <vector>

#include "../../chess_types.hpp"
#include "../../model/move.hpp"
#include "../../model/move_generator.hpp"

namespace lilia::model {
class ChessGame;
class Position;
}  // namespace lilia::model

namespace lilia::view {
class GameView;
}

namespace lilia::controller {

class LegalMoveCache;

class AttackSystem {
 public:
  AttackSystem(view::GameView& view, model::ChessGame& game, LegalMoveCache& legal);

  const std::vector<core::Square>& attacks(core::Square pieceSq) const;

 private:
  view::GameView& m_view;
  model::ChessGame& m_game;
  LegalMoveCache& m_legal;

  mutable model::MoveGenerator m_movegen;
  mutable std::vector<model::Move> m_pseudo;
  mutable std::vector<core::Square> m_out;
};

}  // namespace lilia::controller
