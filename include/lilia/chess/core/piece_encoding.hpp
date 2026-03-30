#pragma once
#include <cstdint>

#include "lilia/chess/chess_types.hpp"

namespace lilia::chess
{
  [[nodiscard]] constexpr int decode_ti(std::uint8_t packed) noexcept
  {
    return (packed & 0x7) - 1;
  }

  [[nodiscard]] constexpr int decode_ci(std::uint8_t packed) noexcept
  {
    return (packed >> 3) & 0x1;
  }

  [[nodiscard]] constexpr std::uint8_t encode_piece(PieceType type, Color color) noexcept
  {
    const int ti = (type == PieceType::None) ? -1 : static_cast<int>(type);
    const int ci = (color == Color::White) ? 0 : 1;
    return static_cast<std::uint8_t>((ti + 1) | (ci << 3));
  }
}
