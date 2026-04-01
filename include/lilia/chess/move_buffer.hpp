#pragma once
#include <cstddef>

#include "move.hpp"
#include "lilia/chess/compiler.hpp"

namespace lilia::chess
{

  constexpr int MAX_MOVES = 256;

  struct MoveBuffer
  {
    Move *LILIA_RESTRICT out;
    int cap;
    int n;

    LILIA_ALWAYS_INLINE MoveBuffer(Move *ptr, int capacity) noexcept : out(ptr), cap(capacity), n(0) {}

    [[nodiscard]] LILIA_ALWAYS_INLINE bool can_push() const noexcept { return n < cap; }

    // Kept for hot paths where caller guarantees capacity.
    LILIA_ALWAYS_INLINE void push_unchecked(const Move &m) noexcept
    {
      LILIA_ASSUME(n < cap);
      out[n] = m;
      ++n;
    }

    LILIA_ALWAYS_INLINE void push(const Move &m) noexcept
    {
#ifndef NDEBUG
      if (LILIA_UNLIKELY(n >= cap))
      {
        LILIA_DEBUGBREAK();
        return; // avoid UB if user continues after breaking in a debugger
      }
#endif
      out[n] = m;
      ++n;
    }

    // Additive helpers (do not affect existing call sites)
    [[nodiscard]] LILIA_ALWAYS_INLINE int size() const noexcept { return n; }
    LILIA_ALWAYS_INLINE void reset() noexcept { n = 0; }
  };

}
