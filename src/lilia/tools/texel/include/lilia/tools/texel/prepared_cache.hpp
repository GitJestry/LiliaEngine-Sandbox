#pragma once
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "lilia/engine/eval_shared.hpp"
#include "lilia/tools/texel/types.hpp"

namespace lilia::tools::texel {

// Hash of defaults + parameter names + deltaStep, used to validate cache compatibility.
uint64_t hash_defaults(const std::span<const engine::EvalParamEntry>& entries,
                       const std::vector<int>& defaults, int deltaStep, uint32_t engineId = 0);

// Returns true on success. hasFenOut indicates whether the loaded cache contains FEN strings
// (required for relinearization).
bool load_prepared_cache(const std::string& path, std::vector<PreparedSample>& out,
                         uint32_t expectedParams, double expectedScale,
                         uint64_t expectedDefaultsHash, int expectedDelta, bool& hasFenOut);

// Saves v3 cache (with checksum, per-sample weights, and FEN).
bool save_prepared_cache(const std::string& path, const std::vector<PreparedSample>& samples,
                         uint32_t paramCount, double logisticScale, uint64_t defaultsHash,
                         int deltaStep, uint32_t engineId = 0);

}  // namespace lilia::tools::texel
