#pragma once

#include <string>

#include "../chess_types.hpp"
#include "../model/move.hpp"

namespace lilia {

inline std::string square_to_uci(int sq) {
  if (sq < 0 || sq > 63) return "--";
  int file = sq % 8;
  int rank = sq / 8;
  std::string s;
  s.push_back('a' + file);
  s.push_back('1' + rank);
  return s;
}

inline std::string move_to_uci(const model::Move& m) {
  if (m.from() < 0 || m.to() < 0) return "----";

  std::string uci = square_to_uci(m.from()) + square_to_uci(m.to());
  using core::PieceType;
  if (m.promotion() != PieceType::None) {
    char p = 'q';
    switch (m.promotion()) {
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

}  // namespace lilia
