#pragma once
#include <cstddef>
#include <cstdint>

namespace lilia::engine {
struct EngineConfig {
  int maxDepth = 12;  // slightly deeper; iterative deepening helps stability
  std::uint64_t maxNodes = 100000;
  std::size_t ttSizeMb = 1024;  // larger TT eases aspiration/transpositions
  bool useNullMove = true;      // good for middlegame; quiescence fixes reduce risks
  bool useLMR = true;           // mild reductions
  bool useAspiration = true;    // stable with score normalization
  int aspirationWindow = 20;    // not too tight, otherwise re-search flapping
  int threads = 0;              // 0 => auto (HW); engine limits anyway
  bool useLMP = true;           // Late Move Pruning (quiet moves, shallow)
  bool useIID = true;           // Internal Iterative Deepening for uncertain nodes
  bool useSingularExt = true;   // extended search on best moves
  int lmpDepthMax = 3;  // only for depth <= 3
  int lmpBase = 2;      // threshold ~ lmpBase + depth*depth

  bool useFutility = true;  // futility at depth==1, quiet moves
  int futilityMargin = 125; // staticeval + futilityMargin <= alpha

  bool useReverseFutility = true;  // shallow: if staticEval >> beta then cut, not in check
  bool useSEEPruning = true;       // prune bad captures early (qsearch/low depth)
  bool useProbCut = true;          // reduced forward pruning on checks and captures
  bool qsearchQuietChecks = true;  // look at quiet checks

  bool useThreatSignals = true;      // heuristik to see threats ahead of time
  int threatSignalsDepthMax = 5;     // disable deeper than this (recommended)
  int threatSignalsQuietCap = 8;     // compute only for first K quiet moves (recommended)
  int threatSignalsHistMin = -8000;  // skip if history is really bad

  // LMR fine-tuning
  int lmrBase = 1;            // base reduction
  int lmrMax = 3;             // cap
  bool lmrUseHistory = true;  // good history => less reduction
  int fullRescoreTopK = 4;    // 0 = none, 1 = only winner, N>1 = also N-1 others
};
static const int base_value[6] = {100, 320, 330, 500, 950, 20000};
constexpr int INF = 32000;                  // INF has to be higher than MATE
constexpr int MATE = 30000;
constexpr int MAX_PLY = 128;                // max half moves
constexpr int MATE_THR = MATE - 512;        // mate threshold for detection/encoding
static constexpr int VALUE_INF = MATE - 1;  // never greater than mate!
}  // namespace lilia::engine
