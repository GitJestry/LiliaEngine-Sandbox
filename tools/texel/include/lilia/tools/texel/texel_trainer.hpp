#pragma once
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "lilia/engine/eval_shared.hpp"
#include "lilia/engine/eval.hpp"
#include "lilia/tools/texel/options.hpp"
#include "lilia/tools/texel/types.hpp"

namespace lilia::tools::texel
{

  // Finite-difference linearization around linpoint. Serial by design because evaluation parameters
  // are global engine state.
  std::vector<PreparedSample> prepare_samples(const std::vector<RawSample> &rawSamples,
                                              engine::Evaluator &evaluator,
                                              const std::vector<int> &linpoint,
                                              const std::span<const engine::EvalParamEntry> &entries,
                                              const Options &opts);

  // Texel tuning using logistic loss over prepared samples (parallel gradient reduction).
  TrainingResult train_texel(std::vector<PreparedSample> &trainSamples,
                             std::vector<PreparedSample> &valSamples,
                             const std::vector<int> &defaults,
                             const std::span<const engine::EvalParamEntry> &entries,
                             const Options &opts);

  // Writes tuned weights to stdout or opts.weightsOutput.
  void emit_weights(const TrainingResult &result, const std::vector<int> &defaults,
                    const std::span<const engine::EvalParamEntry> &entries, const Options &opts);

} // namespace lilia::tools::texel
