#pragma once

#include <string>

#include "../chess_types.hpp"
#include "../model/move.hpp"

namespace lilia::uci
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

  inline std::string square_to_uci(int sq)
  {
    if (sq < 0 || sq > 63)
      return "--";
    int file = sq % 8;
    int rank = sq / 8;
    std::string s;
    s.push_back('a' + file);
    s.push_back('1' + rank);
    return s;
  }

  inline std::string move_to_uci(const model::Move &m)
  {
    if (m.from() < 0 || m.to() < 0)
      return "----";

    std::string uci = square_to_uci(m.from()) + square_to_uci(m.to());
    using core::PieceType;
    if (m.promotion() != PieceType::None)
    {
      char p = 'q';
      switch (m.promotion())
      {
      case PieceType::Knight:
        p = 'n';
        break;
      case PieceType::Bishop:
        p = 'b';
        break;
      case PieceType::Rook:
        p = 'r';
        break;
      case PieceType::Queen:
        p = 'q';
        break;
      default:
        p = 'q';
        break;
      }
      uci.push_back(p);
    }

    return uci;
  }

} // namespace lilia
