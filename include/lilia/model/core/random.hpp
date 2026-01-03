#pragma once
#include "bitboard.hpp"

namespace lilia::model::random {

struct SplitMix64 {
  bb::Bitboard x;
  explicit SplitMix64(bb::Bitboard seed) : x(seed) {}
  bb::Bitboard next() {
    uint64_t z = (x += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
  }
};

}  // namespace lilia::model::random
