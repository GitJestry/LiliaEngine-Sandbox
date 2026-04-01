#pragma once

#include <atomic>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "lilia/chess/move.hpp"
#include "engine.hpp"
#include "search.hpp"

namespace lilia::chess
{
  class ChessGame;
}

namespace lilia::engine
{

  struct SearchResult
  {
    std::optional<chess::Move> bestMove;
    engine::SearchStats stats;

    std::vector<std::pair<chess::Move, int>> topMoves;
  };

  class BotEngine
  {
  public:
    explicit BotEngine(const EngineConfig &cfg = {});
    ~BotEngine();

    SearchResult findBestMove(chess::ChessGame &gameState, int maxDepth, int thinkMillis,
                              std::atomic<bool> *externalCancel = nullptr);
    const engine::SearchStats &getLastSearchStats() const;

  private:
    Engine m_engine;
  };

}
