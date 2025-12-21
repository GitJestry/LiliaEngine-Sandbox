#pragma once
#include <cstddef>

#include "lilia/model/move.hpp"

namespace lilia::engine {

constexpr int MAX_MOVES = 256;

struct MoveBuffer {
  model::Move* out;
  int cap;
  int n;

  inline MoveBuffer(model::Move* ptr, int capacity) : out(ptr), cap(capacity), n(0) {}

  inline bool can_push() const { return n < cap; }

  inline void push_unchecked(const model::Move& m) {
    out[n++] = m;  // kein Bounds-Check im Hotpath
  }

  inline void push(const model::Move& m) {
#ifndef NDEBUG
    if (!can_push()) {
      __debugbreak();
    }
#endif
    out[n++] = m;
  }
};

}  // namespace lilia::engine
