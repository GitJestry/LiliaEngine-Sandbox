#pragma once

#include <atomic>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "../model/move.hpp"
#include "engine.hpp"
#include "search.hpp"

namespace lilia::model {
class ChessGame;
}  // namespace lilia::model

namespace lilia::engine {

struct SearchResult {
  std::optional<model::Move> bestMove;
  engine::SearchStats stats;

  std::vector<std::pair<model::Move, int>> topMoves;
};

class BotEngine {
 public:
  explicit BotEngine(const EngineConfig& cfg = {});
  ~BotEngine();

  SearchResult findBestMove(model::ChessGame& gameState, int maxDepth, int thinkMillis,
                            std::atomic<bool>* externalCancel = nullptr);
  const engine::SearchStats& getLastSearchStats() const;

 private:
  Engine m_engine;
};

}  // namespace lilia::engine
