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
    Move *LILIA_RESTRICT cur;
    Move *LILIA_RESTRICT end;
    int cap;
    int n;

    LILIA_ALWAYS_INLINE MoveBuffer(Move *ptr, int capacity) noexcept
        : out(ptr), cur(ptr), end(ptr + capacity), cap(capacity), n(0) {}

    [[nodiscard]] LILIA_ALWAYS_INLINE bool can_push() const noexcept { return cur < end; }
    [[nodiscard]] LILIA_ALWAYS_INLINE int size() const noexcept { return n; }
    [[nodiscard]] LILIA_ALWAYS_INLINE int remaining() const noexcept
    {
      return static_cast<int>(end - cur);
    }

    [[nodiscard]] LILIA_ALWAYS_INLINE Move *data() noexcept { return out; }
    [[nodiscard]] LILIA_ALWAYS_INLINE const Move *data() const noexcept { return out; }

    [[nodiscard]] LILIA_ALWAYS_INLINE Move *current() noexcept { return cur; }
    [[nodiscard]] LILIA_ALWAYS_INLINE const Move *current() const noexcept { return cur; }

    LILIA_ALWAYS_INLINE void advance_to(Move *p) noexcept
    {
      LILIA_ASSUME(p >= out && p <= end);
      cur = p;
      n = static_cast<int>(cur - out);
    }

    LILIA_ALWAYS_INLINE void push_unchecked(const Move &m) noexcept
    {
      LILIA_ASSUME(cur < end);
      *cur++ = m;
      ++n;
    }

    LILIA_ALWAYS_INLINE void push(const Move &m) noexcept
    {
#ifndef NDEBUG
      if (LILIA_UNLIKELY(cur >= end))
      {
        LILIA_DEBUGBREAK();
        return;
      }
#endif
      *cur++ = m;
      ++n;
    }

    LILIA_ALWAYS_INLINE void reset() noexcept
    {
      cur = out;
      n = 0;
    }
  };

}
