#pragma once
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

#include "lilia/tools/texel/common.hpp"

namespace lilia::tools::texel {

struct Options {
  bool generateData = false;
  bool tune = false;

  std::string stockfishPath;
  int games = 8;
  int depth = 12;
  int maxPlies = 160;
  int sampleSkip = 6;
  int sampleStride = 4;

  std::string dataFile;
  int iterations = 200;
  double learningRate = 0.0005;
  double logisticScale = 256.0;
  double l2 = 0.0;

  std::optional<std::string> weightsOutput;
  std::optional<int> sampleLimit;
  bool shuffleBeforeTraining = true;
  int progressIntervalMs = 750;

  // Engine / self-play
  int threads = 1;
  int multipv = 4;
  double tempCp = 80.0;
  int movetimeMs = 0;
  int movetimeJitterMs = 0;
  std::optional<int> skillLevel;
  std::optional<int> elo;
  std::optional<int> contempt;

  // Performance / training
  int genWorkers = 1;
  int trainWorkers = 1;
  bool useAdam = true;
  double adamBeta1 = 0.9;
  double adamBeta2 = 0.999;
  double adamEps = 1e-8;
  double weightDecay = 0.0;

  int logEvery = 0;
  uint64_t seed = 0;
  int batchSize = 0;
  double valSplit = 0.0;
  int evalEvery = 0;
  int earlyStopPatience = 0;
  double earlyStopDelta = 0.0;
  double gradClip = 0.0;

  // LR schedule
  int lrWarmup = 0;
  int lrCosine = 0;

  // Prepared cache
  std::optional<std::string> preparedCache;
  bool loadPreparedIfExists = true;
  bool savePrepared = true;

  // Warm start
  std::optional<std::string> initWeightsPath;

  // Relinearization
  int relinEvery = 0;
  double relinFrac = 0.0;
  int relinDelta = 1;

  // Auto-scale
  bool autoScale = false;

  // Learnable extras
  bool learnBias = true;
  bool learnScale = false;

  // Logging
  std::optional<std::string> logCsv;
};

Options parse_args(int argc, char** argv, const DefaultPaths& defaults);

}  // namespace lilia::tools::texel
