#pragma once
#include <string>
#include <vector>

#include "lilia/tools/texel/options.hpp"
#include "lilia/tools/texel/types.hpp"

namespace lilia::tools::texel {

std::vector<RawSample> generate_samples_parallel(const Options& opts);

void write_dataset(const std::vector<RawSample>& samples, const std::string& path);

std::vector<RawSample> read_dataset(const std::string& path);

}  // namespace lilia::tools::texel
