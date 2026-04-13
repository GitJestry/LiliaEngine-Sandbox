#include "lilia/engine/engine.hpp"

#include <algorithm>
#include <limits>
#include <memory>
#include <thread>
#include <vector>

#include "lilia/chess/core/magic.hpp"
#include "lilia/chess/game_state.hpp"
#include "lilia/engine/eval.hpp"
#include "lilia/engine/move_order.hpp"
#include "lilia/engine/search.hpp"
#include "lilia/engine/search_position.hpp"
#include "lilia/engine/thread_pool.hpp"

namespace lilia::engine
{

  struct Engine::Impl
  {
    EngineConfig cfg;
    TT tt;

    std::unique_ptr<Search> search;

    explicit Impl(const EngineConfig &c)
        : cfg(c), tt(c.ttSizeMb)
    {
      const unsigned hw = std::thread::hardware_concurrency();
      const int logical = (hw > 0 ? static_cast<int>(hw) : 1);

      if (cfg.threads <= 0)
      {
        cfg.threads = std::max(1, logical - 1); // auto
      }
      else
      {
        cfg.threads = std::clamp(cfg.threads, 1, logical);
      }

      ThreadPool::instance(cfg.threads);
      search = std::make_unique<Search>(tt, cfg);
    }
  };

  Engine::Engine(const EngineConfig &cfg)
      : pimpl(new Impl(cfg))
  {
    Engine::init();
  }

  Engine::~Engine()
  {
    try
    {
      pimpl->tt.clear();
    }
    catch (...)
    {
    }

    try
    {
      if (pimpl->search)
        pimpl->search->clearSearchState();
    }
    catch (...)
    {
    }

    delete pimpl;
  }

  std::optional<chess::Move> Engine::find_best_move(chess::Position &pos,
                                                    int maxDepth,
                                                    std::shared_ptr<std::atomic<bool>> stop)
  {
    if (maxDepth <= 0)
      maxDepth = pimpl->cfg.maxDepth;

    try
    {
      pimpl->search->clearSearchState();
    }
    catch (...)
    {
    }

    SearchPosition spos(pos);

    try
    {
      (void)pimpl->search->search_root_lazy_smp(spos, maxDepth, stop, pimpl->cfg.threads);
    }
    catch (...)
    {
    }

    const auto &stats = pimpl->search->getStats();
    if (stats.bestMove.has_value())
      return stats.bestMove;

    // TT fallback: verify the stored move is still legal under the new
    // caller-owned StateInfo make/unmake architecture.
    try
    {
      auto &tt = pimpl->search->ttRef();
      if (auto e = tt.probe(pos.hash()))
      {
        const chess::Move ttMove = e->best;

        chess::Position tmp = pos;
        chess::StateInfo st{};
        if (tmp.doMove(ttMove, st))
          return ttMove;
      }
    }
    catch (...)
    {
    }

    // Last fallback: pick best legal tactical move, otherwise first legal move.
    try
    {
      chess::MoveGenerator mg;
      std::vector<chess::Move> pseudo;
      pseudo.reserve(128);
      mg.generatePseudoLegalMoves(pos.getBoard(), pos.getState(), pseudo);

      std::optional<chess::Move> bestCapPromo;
      int bestCapScore = std::numeric_limits<int>::min();
      std::optional<chess::Move> firstLegal;

      for (const auto &m : pseudo)
      {
        chess::Position tmp = pos;
        chess::StateInfo st{};
        if (!tmp.doMove(m, st))
          continue;

        if (m.isCapture() || m.promotion() != chess::PieceType::None)
        {
          const int sc = mvv_lva_fast(pos, m);
          if (!bestCapPromo || sc > bestCapScore)
          {
            bestCapPromo = m;
            bestCapScore = sc;
          }
        }
        else if (!firstLegal)
        {
          firstLegal = m;
        }
      }

      if (bestCapPromo)
        return bestCapPromo;
      if (firstLegal)
        return firstLegal;
    }
    catch (...)
    {
    }

    return std::nullopt;
  }

  const SearchStats &Engine::getLastSearchStats() const
  {
    return pimpl->search->getStats();
  }

  const EngineConfig &Engine::getConfig() const
  {
    return pimpl->cfg;
  }

}
