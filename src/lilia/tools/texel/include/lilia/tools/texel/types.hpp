#pragma once
#include <string>
#include <vector>

namespace lilia::tools::texel {

struct RawSample {
  std::string fen;
  double result = 0.5;  // from side-to-move POV
};

struct PreparedSample {
  std::string fen;                 // required for relinearization and cache v2/v3
  float result = 0.5f;             // [0,1]
  float baseEval = 0.0f;           // linearization evaluation (from side-to-move POV)
  float weight = 1.0f;             // per-sample weight
  std::vector<float> gradients;    // dEval/dw_j at linearization point
};

struct TrainingResult {
  std::vector<double> weights;   // engine parameters only
  double finalLoss = 0.0;
  double learnedBias = 0.0;
  double learnedScale = 256.0;
};

}  // namespace lilia::tools::texel
