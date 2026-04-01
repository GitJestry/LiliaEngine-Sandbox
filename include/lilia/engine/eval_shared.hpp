#pragma once
#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "lilia/chess/core/bitboard.hpp"
#include "lilia/chess/compiler.hpp"

namespace lilia::engine
{

  LILIA_ALWAYS_INLINE constexpr int mirror_sq_black(int sq) noexcept
  {
    return sq ^ 56;
  }

  struct EvalParams
  {
#define EVAL_PARAM_SCALAR(name, default_value) int name = default_value;
#define EVAL_PARAM_ARRAY(name, size, ...) std::array<int, size> name = __VA_ARGS__;
#include "lilia/engine/eval_params.inc"
#undef EVAL_PARAM_SCALAR
#undef EVAL_PARAM_ARRAY
  };

  EvalParams &eval_params();
  const EvalParams &default_eval_params();
  void reset_eval_params();

  struct EvalParamEntry
  {
    std::string name;
    int *value = nullptr;
    int default_value = 0;
  };

  std::span<const EvalParamEntry> eval_param_entries();
  std::vector<int> get_eval_param_values();
  std::vector<int> get_default_eval_param_values();
  void set_eval_param_values(std::span<const int> values);

  constexpr int MAX_PHASE = 16;
  LILIA_ALWAYS_INLINE int taper(int mg, int eg, int phase)
  {
    // mg when phase=MAX_PHASE, eg when phase=0
    return ((mg * phase) + (eg * (MAX_PHASE - phase))) / MAX_PHASE;
  }

  constexpr int CENTER_BLOCK_PHASE_MAX = MAX_PHASE;
  constexpr int CENTER_BLOCK_PHASE_DEN = MAX_PHASE;

  constexpr int KING_RING_RADIUS = 2;
  constexpr int KING_SHIELD_DEPTH = 2;

  LILIA_ALWAYS_INLINE bool rook_on_start_square(chess::bb::Bitboard rooks, bool white)
  {
    return white ? (rooks & (chess::bb::sq_bb(chess::bb::A1) | chess::bb::sq_bb(chess::bb::H1)))
                 : (rooks & (chess::bb::sq_bb(chess::bb::A8) | chess::bb::sq_bb(chess::bb::H8)));
  }
}
