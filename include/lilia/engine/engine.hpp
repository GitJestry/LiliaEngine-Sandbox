#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <optional>

#include "lilia/chess/core/magic.hpp"
#include "lilia/chess/move.hpp"
#include "lilia/chess/position.hpp"
#include "config.hpp"

namespace lilia::engine
{
  struct SearchStats;

  class Engine
  {
  public:
    explicit Engine(const EngineConfig &cfg = {});
    ~Engine();

    static void init()
    {
      static std::once_flag magic_once;
      std::call_once(magic_once, []()
                     { chess::magic::init_magics(); });
    }
    std::optional<chess::Move> find_best_move(chess::Position &pos, int maxDepth = 8,
                                              std::shared_ptr<std::atomic<bool>> stop = nullptr);
    const SearchStats &getLastSearchStats() const;
    const EngineConfig &getConfig() const;

  private:
    struct Impl;
    Impl *pimpl;
  };

} // namespace lilia::engine
