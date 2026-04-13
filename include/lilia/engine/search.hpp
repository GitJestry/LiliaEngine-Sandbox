#pragma once

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
#include "transposition_table.hpp"
#include "config.hpp"
#include "eval.hpp"
#include "lilia/chess/compiler.hpp"

namespace lilia::engine
{
  static constexpr int CONTHIST_LAYERS = 6; // 1..6 ply

  struct SearchStoppedException : public std::exception
  {
    const char *what() const noexcept override { return "Search stopped"; }
  };

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

  class Search
  {
  public:
    Search(TT &tt, const EngineConfig &cfg);
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
    LILIA_ALWAYS_INLINE void set_node_limit(std::shared_ptr<std::atomic<std::uint64_t>> shared, std::uint64_t limit)
    {
      sharedNodes = std::move(shared);
      nodeLimit = limit;
    }

    [[nodiscard]] LILIA_ALWAYS_INLINE const SearchStats &getStats() const noexcept { return stats; }
    void clearSearchState(); // Killers/History reset

    LILIA_ALWAYS_INLINE TT &ttRef() noexcept { return tt; }

    // Killers: 2 every Ply
    alignas(64) std::array<std::array<chess::Move, 2>, MAX_PLY> killers{};

    // Basehistory (from->to)
    alignas(64) std::array<std::array<int16_t, chess::SQ_NB>, chess::SQ_NB> history{};

    // Quiet-History: (moverPiece, to)
    alignas(64) int16_t quietHist[chess::PIECE_TYPE_NB][chess::SQ_NB] = {};

    // Capture-History: (moverPiece, to, capturedPiece)
    alignas(64) int16_t captureHist[chess::PIECE_TYPE_NB][chess::SQ_NB][chess::PIECE_TYPE_NB] = {};

    alignas(64) chess::Move counterMove[chess::SQ_NB][chess::SQ_NB] = {};
    alignas(64) int16_t counterHist[chess::SQ_NB][chess::SQ_NB] = {};
    alignas(64) int16_t contHist[CONTHIST_LAYERS][chess::PIECE_TYPE_NB][chess::SQ_NB][chess::PIECE_TYPE_NB][chess::SQ_NB];

    LILIA_ALWAYS_INLINE void set_thread_id(int id) { thread_id_ = id; }
    [[nodiscard]] LILIA_ALWAYS_INLINE int thread_id() const { return thread_id_; }

  private:
    int thread_id_ = 0; // 0 = main, >0 helpers
    int negamax(SearchPosition &pos, int depth, int alpha, int beta, int ply, chess::Move &refBest,
                int parentStaticEval = 0, const chess::Move *excludedMove = nullptr);
    int quiescence(SearchPosition &pos, int alpha, int beta, int ply);
    std::vector<chess::Move> build_pv_from_tt(SearchPosition pos, int max_len = 16);
    int signed_eval(SearchPosition &pos);
    // Copy global heuristics into this worker (killers are reset, on purpose)
    void copy_heuristics_from(const Search &src);
    // Merge this worker's heuristics into the global (killers are NOT merged)
    void merge_from(const Search &other);

    uint32_t tick_ = 0;
    static constexpr uint32_t TICK_STEP = 1024;

    // to reduce exact node count
    LILIA_ALWAYS_INLINE void fast_tick()
    {
      ++tick_;

      if (LILIA_LIKELY((tick_ & (TICK_STEP - 1)) != 0))
        return;

      if (sharedNodes)
      {
        auto cur = sharedNodes->fetch_add(TICK_STEP, std::memory_order_relaxed) + TICK_STEP;
        if (LILIA_UNLIKELY(nodeLimit && cur >= nodeLimit))
        {
          if (stopFlag)
            stopFlag->store(true, std::memory_order_relaxed);
          throw SearchStoppedException();
        }
      }

      if (LILIA_UNLIKELY(stopFlag && stopFlag->load(std::memory_order_relaxed)))
        throw SearchStoppedException();
    }

    LILIA_ALWAYS_INLINE void flush_tick()
    {
      if (!sharedNodes)
        return;
      uint32_t rem = (tick_ & (TICK_STEP - 1));
      if (rem)
        sharedNodes->fetch_add(rem, std::memory_order_relaxed);
    }

    TT &tt;
    chess::MoveGenerator mg;
    const EngineConfig &cfg;
    Evaluator eval_;

    std::array<chess::Move, MAX_PLY> prevMove{};
    std::array<chess::PieceType, MAX_PLY> prevMovedPiece{};

    chess::Move genArr_[MAX_PLY][MAX_MOVES];
    int genN_[MAX_PLY]{};

    chess::Move capArr_[MAX_PLY][MAX_MOVES];
    int capN_[MAX_PLY]{};

    alignas(64) chess::Move ordArr_[MAX_PLY][MAX_MOVES];
    alignas(64) int ordScore_[MAX_PLY][MAX_MOVES];

    std::shared_ptr<std::atomic<bool>> stopFlag;
    SearchStats stats;
    std::shared_ptr<std::atomic<std::uint64_t>> sharedNodes;
    std::uint64_t nodeLimit = 0;
  };

}
