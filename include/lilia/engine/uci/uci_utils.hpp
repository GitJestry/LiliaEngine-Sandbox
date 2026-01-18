#pragma once

#include "lilia/chess_types.hpp"
#include <sstream>

namespace lilia::engine::uci
{

  // Fast ASCII helpers
  inline char tolower_ascii(char c) noexcept
  {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c | 32) : c;
  }

  inline int squareFromUCI(const char *sq) noexcept
  {
    // expects "e2", returns 0..63 or -1
    if (!sq)
      return -1;
    const int file = sq[0] - 'a';
    const int rank = sq[1] - '1';
    return (static_cast<unsigned>(file) < 8u && static_cast<unsigned>(rank) < 8u) ? (rank * 8 + file)
                                                                                  : -1;
  }

  inline core::Square stringToSquare(std::string_view sv) noexcept
  {
    if (sv.size() < 2)
      return core::NO_SQUARE;
    const char f = sv[0];
    const char r = sv[1];
    if (f < 'a' || f > 'h' || r < '1' || r > '8')
      return core::NO_SQUARE;
    const uint8_t file = static_cast<uint8_t>(f - 'a');
    const uint8_t rank = static_cast<uint8_t>(r - '1');
    return static_cast<core::Square>(file + rank * 8);
  }

  inline int parseInt(std::string_view sv) noexcept
  {
    int val = 0;
    for (char c : sv)
    {
      if (c < '0' || c > '9')
        break;
      val = val * 10 + (c - '0');
    }
    return val;
  }

}
