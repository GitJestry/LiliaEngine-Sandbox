#pragma once
#include <cstdint>
#include <optional>
#include <random>
#include <string>
#include <vector>

#include "lilia/tools/texel/options.hpp"

namespace lilia::tools::texel {

// Persistent UCI engine process (Stockfish) with basic option setting and MultiPV sampling.
class UciEngine {
 public:
  UciEngine(const std::string& exePath, const Options& opts, uint64_t seed = 0);
  ~UciEngine();

  UciEngine(const UciEngine&) = delete;
  UciEngine& operator=(const UciEngine&) = delete;

  void new_game();

  // Choose a move for: "position startpos [moves ...]"
  // Uses MultiPV+softmax temperature sampling when opts.multipv>1; falls back to bestmove otherwise.
  std::string pick_move_from_startpos(const std::vector<std::string>& moves);

 private:
  struct Impl;
  Impl* impl_{nullptr};
};

}  // namespace lilia::tools::texel
