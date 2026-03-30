#include "lilia/tools/texel/prepared_cache.hpp"

#include <cmath>
#include <filesystem>
#include <fstream>

namespace lilia::tools::texel {
namespace fs = std::filesystem;

struct PreparedCacheHeaderV1 {
  uint32_t magic = 0x54455845u;  // 'TEXE'
  uint32_t version = 1;
  uint32_t paramCount = 0;
  uint64_t sampleCount = 0;
  double logisticScale = 256.0;
};

struct PreparedCacheHeaderV2 {
  uint32_t magic = 0x54455845u;
  uint32_t version = 2;
  uint32_t paramCount = 0;
  uint64_t sampleCount = 0;
  double logisticScale = 256.0;
  uint64_t defaultsHash = 0;
  uint32_t deltaStep = 1;
  uint32_t engineId = 0;
};

struct PreparedCacheHeaderV3 {
  uint32_t magic = 0x54455845u;
  uint32_t version = 3;
  uint32_t paramCount = 0;
  uint64_t sampleCount = 0;
  double logisticScale = 256.0;
  uint64_t defaultsHash = 0;
  uint32_t deltaStep = 1;
  uint32_t engineId = 0;
  uint64_t checksum = 0;
};

static uint64_t fnv1a64_update(uint64_t h, uint64_t x) {
  h ^= x;
  h *= 1099511628211ull;
  return h;
}

uint64_t hash_defaults(const std::span<const engine::EvalParamEntry>& entries,
                       const std::vector<int>& defaults, int deltaStep, uint32_t engineId) {
  uint64_t h = 1469598103934665603ull;
  auto mix = [&](uint64_t x) { h = fnv1a64_update(h, x); };

  mix(static_cast<uint64_t>(entries.size()));
  for (size_t i = 0; i < entries.size(); ++i) {
    for (unsigned char c : entries[i].name) mix(c);
    mix(static_cast<uint64_t>(static_cast<int64_t>(defaults[i])));
  }
  mix(static_cast<uint64_t>(deltaStep));
  mix(static_cast<uint64_t>(engineId));
  return h;
}

static uint64_t checksum_samples(const std::vector<PreparedSample>& v) {
  uint64_t h = 1469598103934665603ull;
  for (const auto& s : v) {
    for (unsigned char c : s.fen) h = fnv1a64_update(h, c);
    h = fnv1a64_update(h, static_cast<uint64_t>(std::llround(s.result * 1e6)));
    h = fnv1a64_update(h, static_cast<uint64_t>(std::llround(s.baseEval * 1e2)));
    h = fnv1a64_update(h, static_cast<uint64_t>(std::llround(s.weight * 1e6)));
    for (float g : s.gradients) {
      h = fnv1a64_update(h, static_cast<uint64_t>(std::llround(static_cast<double>(g) * 1e3)));
    }
  }
  return h;
}

bool load_prepared_cache(const std::string& path, std::vector<PreparedSample>& out,
                         uint32_t expectedParams, double expectedScale,
                         uint64_t expectedDefaultsHash, int expectedDelta, bool& hasFenOut) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return false;

  uint32_t magic = 0, version = 0;
  f.read(reinterpret_cast<char*>(&magic), sizeof(magic));
  f.read(reinterpret_cast<char*>(&version), sizeof(version));
  if (!f || magic != 0x54455845u) return false;

  if (version == 1) {
    PreparedCacheHeaderV1 h{};
    h.magic = magic;
    h.version = version;
    f.read(reinterpret_cast<char*>(&h.paramCount), sizeof(h.paramCount));
    f.read(reinterpret_cast<char*>(&h.sampleCount), sizeof(h.sampleCount));
    f.read(reinterpret_cast<char*>(&h.logisticScale), sizeof(h.logisticScale));
    if (!f || h.paramCount != expectedParams) return false;
    if (std::abs(h.logisticScale - expectedScale) > 1e-9) return false;

    out.assign(h.sampleCount, PreparedSample{});
    for (uint64_t i = 0; i < h.sampleCount; ++i) {
      float res = 0, base = 0;
      f.read(reinterpret_cast<char*>(&res), sizeof(float));
      f.read(reinterpret_cast<char*>(&base), sizeof(float));
      out[i].result = res;
      out[i].baseEval = base;
      out[i].weight = 1.0f;
    }
    for (uint64_t i = 0; i < h.sampleCount; ++i) {
      out[i].gradients.resize(h.paramCount);
      f.read(reinterpret_cast<char*>(out[i].gradients.data()), sizeof(float) * h.paramCount);
    }
    hasFenOut = false;
    return static_cast<bool>(f);
  }

  if (version == 2) {
    PreparedCacheHeaderV2 h{};
    h.magic = magic;
    h.version = version;
    f.read(reinterpret_cast<char*>(&h.paramCount), sizeof(h.paramCount));
    f.read(reinterpret_cast<char*>(&h.sampleCount), sizeof(h.sampleCount));
    f.read(reinterpret_cast<char*>(&h.logisticScale), sizeof(h.logisticScale));
    f.read(reinterpret_cast<char*>(&h.defaultsHash), sizeof(h.defaultsHash));
    f.read(reinterpret_cast<char*>(&h.deltaStep), sizeof(h.deltaStep));
    f.read(reinterpret_cast<char*>(&h.engineId), sizeof(h.engineId));

    if (!f || h.paramCount != expectedParams) return false;
    if (std::abs(h.logisticScale - expectedScale) > 1e-9) return false;
    if (h.defaultsHash != expectedDefaultsHash) return false;
    if (static_cast<int>(h.deltaStep) != expectedDelta) return false;

    out.assign(h.sampleCount, PreparedSample{});
    for (uint64_t i = 0; i < h.sampleCount; ++i) {
      uint32_t flen = 0;
      f.read(reinterpret_cast<char*>(&flen), sizeof(flen));
      std::string fen(flen, '\0');
      if (flen) f.read(&fen[0], flen);

      float res = 0, base = 0;
      f.read(reinterpret_cast<char*>(&res), sizeof(float));
      f.read(reinterpret_cast<char*>(&base), sizeof(float));
      out[i].fen = std::move(fen);
      out[i].result = res;
      out[i].baseEval = base;
      out[i].weight = 1.0f;
    }
    for (uint64_t i = 0; i < h.sampleCount; ++i) {
      out[i].gradients.resize(h.paramCount);
      f.read(reinterpret_cast<char*>(out[i].gradients.data()), sizeof(float) * h.paramCount);
    }
    hasFenOut = true;
    return static_cast<bool>(f);
  }

  if (version == 3) {
    PreparedCacheHeaderV3 h{};
    h.magic = magic;
    h.version = version;
    f.read(reinterpret_cast<char*>(&h.paramCount), sizeof(h.paramCount));
    f.read(reinterpret_cast<char*>(&h.sampleCount), sizeof(h.sampleCount));
    f.read(reinterpret_cast<char*>(&h.logisticScale), sizeof(h.logisticScale));
    f.read(reinterpret_cast<char*>(&h.defaultsHash), sizeof(h.defaultsHash));
    f.read(reinterpret_cast<char*>(&h.deltaStep), sizeof(h.deltaStep));
    f.read(reinterpret_cast<char*>(&h.engineId), sizeof(h.engineId));
    f.read(reinterpret_cast<char*>(&h.checksum), sizeof(h.checksum));

    if (!f || h.paramCount != expectedParams) return false;
    if (std::abs(h.logisticScale - expectedScale) > 1e-9) return false;
    if (h.defaultsHash != expectedDefaultsHash) return false;
    if (static_cast<int>(h.deltaStep) != expectedDelta) return false;

    out.assign(h.sampleCount, PreparedSample{});
    for (uint64_t i = 0; i < h.sampleCount; ++i) {
      uint32_t flen = 0;
      f.read(reinterpret_cast<char*>(&flen), sizeof(flen));
      std::string fen(flen, '\0');
      if (flen) f.read(&fen[0], flen);

      float res = 0, base = 0, w = 1.0f;
      f.read(reinterpret_cast<char*>(&res), sizeof(float));
      f.read(reinterpret_cast<char*>(&base), sizeof(float));
      f.read(reinterpret_cast<char*>(&w), sizeof(float));

      out[i].fen = std::move(fen);
      out[i].result = res;
      out[i].baseEval = base;
      out[i].weight = w;
    }
    for (uint64_t i = 0; i < h.sampleCount; ++i) {
      out[i].gradients.resize(h.paramCount);
      f.read(reinterpret_cast<char*>(out[i].gradients.data()), sizeof(float) * h.paramCount);
    }
    hasFenOut = true;

    if (checksum_samples(out) != h.checksum) return false;
    return static_cast<bool>(f);
  }

  return false;
}

bool save_prepared_cache(const std::string& path, const std::vector<PreparedSample>& samples,
                         uint32_t paramCount, double logisticScale, uint64_t defaultsHash,
                         int deltaStep, uint32_t engineId) {
  fs::path p{path};
  if (p.has_parent_path()) {
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
  }

  std::ofstream f(path, std::ios::binary | std::ios::trunc);
  if (!f) return false;

  PreparedCacheHeaderV3 h{};
  h.paramCount = paramCount;
  h.sampleCount = samples.size();
  h.logisticScale = logisticScale;
  h.defaultsHash = defaultsHash;
  h.deltaStep = static_cast<uint32_t>(deltaStep);
  h.engineId = engineId;
  h.checksum = checksum_samples(samples);

  f.write(reinterpret_cast<const char*>(&h), sizeof(h));
  for (const auto& s : samples) {
    const uint32_t flen = static_cast<uint32_t>(s.fen.size());
    f.write(reinterpret_cast<const char*>(&flen), sizeof(flen));
    if (flen) f.write(s.fen.data(), flen);
    f.write(reinterpret_cast<const char*>(&s.result), sizeof(float));
    f.write(reinterpret_cast<const char*>(&s.baseEval), sizeof(float));
    f.write(reinterpret_cast<const char*>(&s.weight), sizeof(float));
  }
  for (const auto& s : samples) {
    f.write(reinterpret_cast<const char*>(s.gradients.data()),
            sizeof(float) * static_cast<std::streamsize>(s.gradients.size()));
  }
  return static_cast<bool>(f);
}

}  // namespace lilia::tools::texel
