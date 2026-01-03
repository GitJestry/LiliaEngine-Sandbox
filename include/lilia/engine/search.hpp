#pragma once

// -----------------------------------------------------------------------------
// Branch-Prediction Hints
// -----------------------------------------------------------------------------
#if defined(__GNUC__) || defined(__clang__)
#define LILIA_LIKELY(x) __builtin_expect(!!(x), 1)
#define LILIA_UNLIKELY(x) __builtin_expect(!!(x), 0)
#else
#define LILIA_LIKELY(x) (x)
#define LILIA_UNLIKELY(x) (x)
#endif

#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "../model/move_generator.hpp"
#include "../model/position.hpp"
#include "../model/tt5.hpp"
#include "config.hpp"
#include "eval.hpp"

namespace lilia::engine
{

  // -----------------------------------------------------------------------------
  // Limits / constants
  // -----------------------------------------------------------------------------
  static constexpr int PIECE_NB = 6;
  static constexpr int SQ_NB = 64;
  static constexpr int CH_LAYERS = 6; // 1..6 ply

  struct SearchStoppedException : public std::exception
  {
    const char *what() const noexcept override { return "Search stopped"; }
  };

  // -----------------------------------------------------------------------------
  // SearchStats – robust count (64-bit)
  // -----------------------------------------------------------------------------
  struct SearchStats
  {
    std::uint64_t nodes = 0;
    double nps = 0.0;
    std::uint64_t elapsedMs = 0;
    int bestScore = 0;
    std::optional<model::Move> bestMove;
    std::vector<std::pair<model::Move, int>> topMoves;
    std::vector<model::Move> bestPV;
  };

  // Forwarddecleration
  class Evaluator;

  // -----------------------------------------------------------------------------
  // Search – one Instance-per-Thread (no shared mutable Data)
  // -----------------------------------------------------------------------------
  class Search
  {
  public:
    Search(model::TT5 &tt, std::shared_ptr<const Evaluator> eval, const EngineConfig &cfg);
    ~Search() = default;

    // Non-copyable / non-movable
    Search(const Search &) = delete;
    Search &operator=(const Search &) = delete;
    Search(Search &&) = delete;
    Search &operator=(Search &&) = delete;

    // Root (iterative deepening, parallel for Root-Children)
    // maxThreads <= 0 -> use cfg.threads for deterministic thread count
    int search_root_single(model::Position &pos, int maxDepth,
                           std::shared_ptr<std::atomic<bool>> stop, std::uint64_t maxNodes = 0);

    int search_root_lazy_smp(model::Position &pos, int maxDepth,
                             std::shared_ptr<std::atomic<bool>> stop, int maxThreads,
                             std::uint64_t maxNodes = 0);
    void set_node_limit(std::shared_ptr<std::atomic<std::uint64_t>> shared, std::uint64_t limit)
    {
      sharedNodes = std::move(shared);
      nodeLimit = limit;
    }

    [[nodiscard]] const SearchStats &getStats() const noexcept { return stats; }
    void clearSearchState(); // Killers/History reset

    model::TT5 &ttRef() noexcept { return tt; }

    // Killers: 2 every Ply
    alignas(64) std::array<std::array<model::Move, 2>, MAX_PLY> killers{};

    // Basehistory (from->to)
    alignas(64) std::array<std::array<int16_t, SQ_NB>, SQ_NB> history{};

    // extended heuristics (for better Move-Order/Cutoffs)
    // Quiet-History: (moverPiece, to)
    alignas(64) int16_t quietHist[PIECE_NB][SQ_NB] = {};

    // Capture-History: (moverPiece, to, capturedPiece)
    alignas(64) int16_t captureHist[PIECE_NB][SQ_NB][PIECE_NB] = {};

    // Counter-Move: (from,to) typical answer
    // plus Counter-History-Bonus for this exact move
    alignas(64) model::Move counterMove[SQ_NB][SQ_NB] = {};
    alignas(64) int16_t counterHist[SQ_NB][SQ_NB] = {};
    alignas(64) int16_t contHist[CH_LAYERS][PIECE_NB][SQ_NB][PIECE_NB][SQ_NB];

    void set_thread_id(int id) { thread_id_ = id; }
    int thread_id() const { return thread_id_; }

  private:
    int thread_id_ = 0; // 0 = main, >0 helpers
    int negamax(model::Position &pos, int depth, int alpha, int beta, int ply, model::Move &refBest,
                int parentStaticEval = 0, const model::Move *excludedMove = nullptr);
    int quiescence(model::Position &pos, int alpha, int beta, int ply);
    std::vector<model::Move> build_pv_from_tt(model::Position pos, int max_len = 16);
    int signed_eval(model::Position &pos);
    // Copy global heuristics into this worker (killers are reset, on purpose)
    void copy_heuristics_from(const Search &src);
    // Merge this worker's heuristics into the global (killers are NOT merged)
    void merge_from(const Search &other);

    uint32_t tick_ = 0;
    static constexpr uint32_t TICK_STEP = 1024;

    // to reduce exact node count
    inline void fast_tick()
    {
      ++tick_;
      if ((tick_ & (TICK_STEP - 1)) != 0)
        return;

      if (sharedNodes)
      {
        auto cur = sharedNodes->fetch_add(TICK_STEP, std::memory_order_relaxed) + TICK_STEP;
        if (nodeLimit && cur >= nodeLimit)
        {
          if (stopFlag)
            stopFlag->store(true, std::memory_order_relaxed);
          throw SearchStoppedException();
        }
      }
      if (stopFlag && stopFlag->load(std::memory_order_relaxed))
        throw SearchStoppedException();
    }

    // so that the last nodes still count
    inline void flush_tick()
    {
      if (!sharedNodes)
        return;
      uint32_t rem = (tick_ & (TICK_STEP - 1));
      if (rem)
        sharedNodes->fetch_add(rem, std::memory_order_relaxed);
    }

    // ---------------------------------------------------------------------------
    // Data
    // ---------------------------------------------------------------------------
    model::TT5 &tt;
    model::MoveGenerator mg;
    const EngineConfig &cfg;
    std::shared_ptr<const Evaluator> eval_;

    std::array<model::Move, MAX_PLY> prevMove{};

    model::Move genArr_[MAX_PLY][lilia::engine::MAX_MOVES];
    int genN_[MAX_PLY]{};

    model::Move capArr_[MAX_PLY][lilia::engine::MAX_MOVES];
    int capN_[MAX_PLY]{};

    alignas(64) model::Move ordArr_[MAX_PLY][lilia::engine::MAX_MOVES];
    alignas(64) int ordScore_[MAX_PLY][lilia::engine::MAX_MOVES];

    // Stop/Stats
    std::shared_ptr<std::atomic<bool>> stopFlag;
    SearchStats stats;
    std::shared_ptr<std::atomic<std::uint64_t>> sharedNodes;
    std::uint64_t nodeLimit = 0;
  };

} // namespace lilia::engine
