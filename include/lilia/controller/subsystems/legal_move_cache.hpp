#pragma once

#include <vector>

#include "../../chess_types.hpp"
#include "../../model/move.hpp"

namespace lilia::model {
class ChessGame;
}

namespace lilia::controller {

class LegalMoveCache {
 public:
  explicit LegalMoveCache(model::ChessGame& game) : m_game(game) {}

  void invalidate() { m_cached = nullptr; }

  const std::vector<model::Move>& legal() const;
  bool contains(core::Square from, core::Square to) const;
  bool isPromotion(core::Square from, core::Square to) const;

 private:
  model::ChessGame& m_game;
  mutable const std::vector<model::Move>* m_cached{nullptr};
};

}  // namespace lilia::controller
