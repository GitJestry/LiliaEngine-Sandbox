#pragma once
#include <array>
#include <cstdint>
#include <vector>

#include "bitboard.hpp"

namespace lilia::chess::magic
{

  enum class Slider
  {
    Rook,
    Bishop
  };

  struct Magic
  {
    core::Bitboard magic = 0ULL;
    std::uint8_t shift = 0;
  };

  void init_magics();

  core::Bitboard sliding_attacks(Slider s, Square sq, core::Bitboard occ) noexcept;

  const std::array<core::Bitboard, 64> &rook_masks();
  const std::array<core::Bitboard, 64> &bishop_masks();
  const std::array<Magic, 64> &rook_magics();
  const std::array<Magic, 64> &bishop_magics();
  const std::array<std::vector<core::Bitboard>, 64> &rook_tables();
  const std::array<std::vector<core::Bitboard>, 64> &bishop_tables();

} // namespace lilia::model::magic
