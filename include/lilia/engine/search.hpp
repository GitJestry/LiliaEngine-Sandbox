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

#include "lilia/chess/move_generator.hpp"
#include "lilia/engine/search_position.hpp"
#include "lilia/chess/chess_types.hpp"
#include "tt5.hpp"
#include "config.hpp"
#include "eval.hpp"

namespace lilia::engine
{

  // -----------------------------------------------------------------------------
  // Limits / constants
  // -----------------------------------------------------------------------------
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
    std::optional<chess::Move> bestMove;
    std::vector<std::pair<chess::Move, int>> topMoves;
    std::vector<chess::Move> bestPV;
  };

  // Forwarddecleration
  class Evaluator;

  // -----------------------------------------------------------------------------
  // Search – one Instance-per-Thread (no shared mutable Data)
  // -----------------------------------------------------------------------------
  class Search
  {
  public:
    Search(TT5 &tt, std::shared_ptr<const Evaluator> eval, const EngineConfig &cfg);
    ~Search() = default;

    // Non-copyable / non-movable
    Search(const Search &) = delete;
    Search &operator=(const Search &) = delete;
    Search(Search &&) = delete;
    Search &operator=(Search &&) = delete;

    // Root (iterative deepening, parallel for Root-Children)
    // maxThreads <= 0 -> use cfg.threads for deterministic thread count
    int search_root_single(SearchPosition &pos, int maxDepth,
                           std::shared_ptr<std::atomic<bool>> stop, std::uint64_t maxNodes = 0);

    int search_root_lazy_smp(SearchPosition &pos, int maxDepth,
                             std::shared_ptr<std::atomic<bool>> stop, int maxThreads,
                             std::uint64_t maxNodes = 0);
    void set_node_limit(std::shared_ptr<std::atomic<std::uint64_t>> shared, std::uint64_t limit)
    {
      sharedNodes = std::move(shared);
      nodeLimit = limit;
    }

    [[nodiscard]] const SearchStats &getStats() const noexcept { return stats; }
    void clearSearchState(); // Killers/History reset

    TT5 &ttRef() noexcept { return tt; }

    // Killers: 2 every Ply
    alignas(64) std::array<std::array<chess::Move, 2>, MAX_PLY> killers{};

    // Basehistory (from->to)
    alignas(64) std::array<std::array<int16_t, chess::SQ_NB>, chess::SQ_NB> history{};

    // extended heuristics (for better Move-Order/Cutoffs)
    // Quiet-History: (moverPiece, to)
    alignas(64) int16_t quietHist[chess::PIECE_TYPE_NB][chess::SQ_NB] = {};

    // Capture-History: (moverPiece, to, capturedPiece)
    alignas(64) int16_t captureHist[chess::PIECE_TYPE_NB][chess::SQ_NB][chess::PIECE_TYPE_NB] = {};

    // Counter-Move: (from,to) typical answer
    // plus Counter-History-Bonus for this exact move
    alignas(64) chess::Move counterMove[chess::SQ_NB][chess::SQ_NB] = {};
    alignas(64) int16_t counterHist[chess::SQ_NB][chess::SQ_NB] = {};
    alignas(64) int16_t contHist[CH_LAYERS][chess::PIECE_TYPE_NB][chess::SQ_NB][chess::PIECE_TYPE_NB][chess::SQ_NB];

    void set_thread_id(int id) { thread_id_ = id; }
    int thread_id() const { return thread_id_; }

  private:
    int thread_id_ = 0; // 0 = main, >0 helpers
    int negamax(SearchPosition &pos, int depth, int alpha, int beta, int ply, chess::Move &refBest,
                int parentStaticEval = 0, const chess::Move *excludedMove = nullptr);
    int quiescence(SearchPosition &pos, int alpha, int beta, int ply);
    std::vector<chess::Move> build_pv_from_tt(SearchPosition pos, int max_len = 16);
    int signed_eval(SearchPosition &pos);
    std::vector<chess::Move> build_pv_from_tt(chess::Position pos, int max_len = 16);
    int signed_eval(chess::Position &pos);
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
    TT5 &tt;
    chess::MoveGenerator mg;
    const EngineConfig &cfg;
    std::shared_ptr<const Evaluator> eval_;

    std::array<chess::Move, MAX_PLY> prevMove{};

    chess::Move genArr_[MAX_PLY][MAX_MOVES];
    int genN_[MAX_PLY]{};

    chess::Move capArr_[MAX_PLY][MAX_MOVES];
    int capN_[MAX_PLY]{};

    alignas(64) chess::Move ordArr_[MAX_PLY][MAX_MOVES];
    alignas(64) int ordScore_[MAX_PLY][MAX_MOVES];

    // Stop/Stats
    std::shared_ptr<std::atomic<bool>> stopFlag;
    SearchStats stats;
    std::shared_ptr<std::atomic<std::uint64_t>> sharedNodes;
    std::uint64_t nodeLimit = 0;
  };

} // namespace lilia::engine
