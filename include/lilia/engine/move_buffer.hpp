#pragma once
#include <cstddef>

#include "lilia/model/move.hpp"

#if defined(_MSC_VER)
#define LILIA_DEBUGBREAK() __debugbreak()
#elif defined(__GNUC__) || defined(__clang__)
#define LILIA_DEBUGBREAK() __builtin_trap()
#else
#define LILIA_DEBUGBREAK() ((void)0)
#endif

namespace lilia::engine {

constexpr int MAX_MOVES = 256;

struct MoveBuffer {
  model::Move* out;
  int cap;
  int n;

  inline MoveBuffer(model::Move* ptr, int capacity) noexcept : out(ptr), cap(capacity), n(0) {}

  [[nodiscard]] inline bool can_push() const noexcept { return n < cap; }

  inline void push_unchecked(const model::Move& m) noexcept {
    out[n++] = m;  // no bounds check in hot path
  }

  inline void push(const model::Move& m) noexcept {
#ifndef NDEBUG
    if (!can_push()) {
      LILIA_DEBUGBREAK();
    }
#endif
    out[n++] = m;
  }
};

}  // namespace lilia::engine
