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

#if defined(__GNUC__) || defined(__clang__)
#define LILIA_LIKELY(x) __builtin_expect(!!(x), 1)
#define LILIA_UNLIKELY(x) __builtin_expect(!!(x), 0)
#define LILIA_RESTRICT __restrict__
#elif defined(_MSC_VER)
#define LILIA_LIKELY(x) (x)
#define LILIA_UNLIKELY(x) (x)
#define LILIA_RESTRICT __restrict
#else
#define LILIA_LIKELY(x) (x)
#define LILIA_UNLIKELY(x) (x)
#define LILIA_RESTRICT
#endif

namespace lilia::engine {

constexpr int MAX_MOVES = 256;

struct MoveBuffer {
  model::Move* LILIA_RESTRICT out;
  int cap;
  int n;

  inline MoveBuffer(model::Move* ptr, int capacity) noexcept : out(ptr), cap(capacity), n(0) {}

  [[nodiscard]] inline bool can_push() const noexcept { return n < cap; }

  // Kept for hot paths where caller guarantees capacity.
  inline void push_unchecked(const model::Move& m) noexcept {
    out[n] = m;
    ++n;
  }

  inline void push(const model::Move& m) noexcept {
#ifndef NDEBUG
    if (LILIA_UNLIKELY(n >= cap)) {
      LILIA_DEBUGBREAK();
      return;  // avoid UB if user continues after breaking in a debugger
    }
#endif
    out[n] = m;
    ++n;
  }

  // Additive helpers (do not affect existing call sites)
  [[nodiscard]] inline int size() const noexcept { return n; }
  inline void reset() noexcept { n = 0; }
};

}  // namespace lilia::engine
