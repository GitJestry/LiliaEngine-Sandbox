#pragma once
#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "lilia/model/core/bitboard.hpp"

/**
 * @brief Its like the look up table as shared parameter values for the eval class
 */

namespace lilia::engine {
using namespace lilia::core;
using namespace lilia::model;

inline constexpr int mirror_sq_black(int sq) noexcept {
  return sq ^ 56;
}

struct EvalParams {
#define EVAL_PARAM_SCALAR(name, default_value) int name = default_value;
#define EVAL_PARAM_ARRAY(name, size, ...) std::array<int, size> name = __VA_ARGS__;
#include "lilia/engine/eval_params.inc"
#undef EVAL_PARAM_SCALAR
#undef EVAL_PARAM_ARRAY
};

EvalParams& eval_params();
const EvalParams& default_eval_params();
void reset_eval_params();

struct EvalParamEntry {
  std::string name;
  int* value = nullptr;
  int default_value = 0;
};

std::span<const EvalParamEntry> eval_param_entries();
std::vector<int> get_eval_param_values();
std::vector<int> get_default_eval_param_values();
void set_eval_param_values(std::span<const int> values);

constexpr int MAX_PHASE = 16;
inline int taper(int mg, int eg, int phase) {
  // mg when phase=MAX_PHASE, eg when phase=0
  return ((mg * phase) + (eg * (MAX_PHASE - phase))) / MAX_PHASE;
}

// after: tie them to MAX_PHASE so “opening” really means early-phase
constexpr int CENTER_BLOCK_PHASE_MAX = MAX_PHASE;
constexpr int CENTER_BLOCK_PHASE_DEN = MAX_PHASE;

// Geometrie / Power-Counting
constexpr int KING_RING_RADIUS = 2;
constexpr int KING_SHIELD_DEPTH = 2;

// Passed pawn race flags (non-tunable)
constexpr bool PASS_RACE_NEED_QUEENLESS = true;  // nur ohne Damen

// Utility wrappers for derived constants
inline bool rook_on_start_square(bb::Bitboard rooks, bool white) {
  return white ? (rooks & (bb::sq_bb(Square(0)) | bb::sq_bb(Square(7))))     // a1,h1
               : (rooks & (bb::sq_bb(Square(56)) | bb::sq_bb(Square(63))));  // a8,h8
}

// =============================================================================
// Mobility Profile & Clamp (remain constexpr tables)
// =============================================================================
static constexpr int KN_MOB_MG[9] = {-14, -8, -4, 0, 4, 8, 12, 16, 18};
static constexpr int KN_MOB_EG[9] = {-10, -6, -2, 2, 6, 10, 12, 14, 16};

static constexpr int BI_MOB_MG[14] = {-18, -12, -6, -2, 2, 6, 10, 14, 18, 22, 24, 26, 28, 30};
static constexpr int BI_MOB_EG[14] = {-14, -10, -4, 0, 4, 8, 12, 16, 20, 24, 26, 28, 30, 32};

static constexpr int RO_MOB_MG[15] = {-18, -12, -6, -2, 2, 6, 10, 14, 18, 22, 26, 30, 32, 34, 36};
static constexpr int RO_MOB_EG[15] = {-10, -6, -2, 2, 6, 10, 14, 18, 22, 26, 30, 34, 36, 38, 40};

static constexpr int QU_MOB_MG[28] = {-8, -6, -4, -2, 0,  2,  4,  6,  8,  10, 12, 14, 16, 18,
                                      20, 22, 24, 26, 28, 30, 32, 34, 36, 38, 40, 42, 44, 46};
static constexpr int QU_MOB_EG[28] = {-6, -4, -2, 0,  2,  4,  6,  8,  10, 12, 14, 16, 18, 20,
                                      22, 24, 26, 28, 30, 32, 34, 36, 38, 40, 42, 44, 46, 48};

// =============================================================================
// PSTs (mg/eg) — belassen (engine-spezifisch)
// =============================================================================
inline constexpr std::array<int, 64> PST_P_MG = {
    0,  0,  0,  0,  0,  0,  0,  0,  6,  6,  2,  -6, -6, 2,  6,  6,  4,  -2, -3, 2,  2,  -3,
    -2, 4,  6,  8,  12, 16, 16, 12, 8,  6,  8,  12, 18, 24, 24, 18, 12, 8,  12, 18, 24, 28,
    28, 24, 18, 12, 12, 12, 12, 12, 12, 12, 12, 12, 0,  0,  0,  0,  0,  0,  0,  0};
inline constexpr std::array<int, 64> PST_P_EG = {
    0,  0,  0,  0,  0,  0,  0,  0,  6,  8,  4,  -2, -2, 4,  8,  6,  6,  2,  2,  6,  6,  2,
    2,  6,  8,  12, 16, 20, 20, 16, 12, 8,  12, 18, 24, 30, 30, 24, 18, 12, 16, 24, 32, 40,
    40, 32, 24, 16, 10, 14, 18, 22, 22, 18, 14, 10, 0,  0,  0,  0,  0,  0,  0,  0};
inline constexpr std::array<int, 64> PST_N_MG = {
    -50, -38, -28, -22, -22, -28, -38, -50, -32, -16, -4,  2,   2,   -4,  -16, -32,
    -24, -2,  12,  18,  18,  12,  -2,  -24, -20, 4,   18,  26,  26,  18,  4,   -20,
    -20, 4,   18,  26,  26,  18,  4,   -20, -24, -2,  12,  18,  18,  12,  -2,  -24,
    -34, -16, -4,  0,   0,   -4,  -16, -34, -46, -36, -28, -24, -24, -28, -36, -46};
inline constexpr std::array<int, 64> PST_N_EG = {
    -36, -26, -18, -14, -14, -18, -26, -36, -26, -12, -2,  6,   6,   -2,  -12, -26,
    -18, -2,  10,  16,  16,  10,  -2,  -18, -14, 6,   16,  22,  22,  16,  6,   -14,
    -14, 6,   16,  22,  22,  16,  6,   -14, -18, -2,  10,  16,  16,  10,  -2,  -18,
    -26, -12, -2,  6,   6,   -2,  -12, -26, -36, -26, -18, -14, -14, -18, -26, -36};
inline constexpr std::array<int, 64> PST_B_MG = {
    -26, -14, -10, -8, -8, -10, -14, -26, -12, -4,  2,  6,  6,  2,  -4,  -12,
    -8,  4,   10,  14, 14, 10,  4,   -8,  -6,  8,   14, 20, 20, 14, 8,   -6,
    -6,  8,   14,  20, 20, 14,  8,   -6,  -8,  4,   10, 14, 14, 10, 4,   -8,
    -12, -4,  2,   6,  6,  2,   -4,  -12, -24, -12, -8, -6, -6, -8, -12, -24};
inline constexpr std::array<int, 64> PST_B_EG = {
    -18, -8, -4, -2, -2, -4, -8, -18, -8, 0,  8,  12, 12,  8,  0,  -8, -4, 8,  14, 20, 20, 14,
    8,   -4, -2, 12, 20, 26, 26, 20,  12, -2, -2, 12, 20,  26, 26, 20, 12, -2, -4, 8,  14, 20,
    20,  14, 8,  -4, -8, 0,  8,  12,  12, 8,  0,  -8, -16, -8, -4, -2, -2, -4, -8, -16};
inline constexpr std::array<int, 64> PST_R_MG = {
    0,  2,  3,  4,  4, 3, 2, 0, -2, 0,  2,  4,  4, 2, 0, -2, -3, -1, 0,  2,  2, 0,
    -1, -3, -4, -1, 1, 2, 2, 1, -1, -4, -4, -1, 1, 2, 2, 1,  -1, -4, -3, -1, 0, 2,
    2,  0,  -1, -3, 4, 6, 6, 8, 8,  6,  6,  4,  2, 4, 4, 6,  6,  4,  4,  2};
inline constexpr std::array<int, 64> PST_R_EG = {
    2, 4,  6,  8,  8, 6, 4, 2, 0, 2,  4,  6, 6, 4, 2, 0,  -1, 1,  2,  4, 4, 2,
    1, -1, -1, 1,  2, 4, 4, 2, 1, -1, -1, 1, 2, 4, 4, 2,  1,  -1, -1, 1, 2, 4,
    4, 2,  1,  -1, 3, 5, 7, 9, 9, 7,  5,  3, 4, 6, 8, 10, 10, 8,  6,  4};
inline constexpr std::array<int, 64> PST_Q_MG = {
    -24, -16, -12, -8, -8, -12, -16, -24, -16, -8,  -4,  -2, -2, -4,  -8,  -16,
    -12, -4,  2,   4,  4,  2,   -4,  -12, -8,  -2,  4,   6,  6,  4,   -2,  -8,
    -8,  -2,  4,   6,  6,  4,   -2,  -8,  -12, -4,  2,   4,  4,  2,   -4,  -12,
    -16, -8,  -4,  -2, -2, -4,  -8,  -16, -24, -16, -12, -8, -8, -12, -16, -24};
inline constexpr std::array<int, 64> PST_Q_EG = {
    -10, -6, -2, 0,  0,  -2, -6, -10, -6, -2, 2,  4,  4,   2,  -2, -6, -2, 2,  6,  8,  8, 6,
    2,   -2, 0,  4,  8,  12, 12, 8,   4,  0,  0,  4,  8,   12, 12, 8,  4,  0,  -2, 2,  6, 8,
    8,   6,  2,  -2, -6, -2, 2,  4,   4,  2,  -2, -6, -10, -6, -2, 0,  0,  -2, -6, -10};
inline constexpr std::array<int, 64> PST_K_MG = {
    -40, -48, -52, -56, -56, -52, -48, -40, -32, -40, -44, -50, -50, -44, -40, -32,
    -24, -32, -36, -44, -44, -36, -32, -24, -12, -20, -28, -36, -36, -28, -20, -12,
    0,   -8,  -18, -28, -28, -18, -8,  0,   10,  18,  4,   -10, -10, 4,   18,  10,
    20,  28,  18,  6,   6,   18,  28,  20,  28,  38,  28,  12,  12,  28,  38,  28};
inline constexpr std::array<int, 64> PST_K_EG = {
    -8, -4, -4, -2, -2, -4, -4, -8, -4, 2,  4,  6,  6,  4,  2,  -4, -4, 4,  10, 12, 12, 10,
    4,  -4, -2, 6,  12, 18, 18, 12, 6,  -2, -2, 6,  12, 18, 18, 12, 6,  -2, -4, 4,  10, 12,
    12, 10, 4,  -4, -4, 2,  4,  6,  6,  4,  2,  -4, -8, -4, -4, -2, -2, -4, -4, -8};

// -------- PST accessors (inline) --------
inline int pst_mg(PieceType pt, int sq) {
  switch (pt) {
    case PieceType::Pawn:
      return PST_P_MG[sq];
    case PieceType::Knight:
      return PST_N_MG[sq];
    case PieceType::Bishop:
      return PST_B_MG[sq];
    case PieceType::Rook:
      return PST_R_MG[sq];
    case PieceType::Queen:
      return PST_Q_MG[sq];
    case PieceType::King:
      return PST_K_MG[sq];
    default:
      return 0;
  }
}
inline int pst_eg(PieceType pt, int sq) {
  switch (pt) {
    case PieceType::Pawn:
      return PST_P_EG[sq];
    case PieceType::Knight:
      return PST_N_EG[sq];
    case PieceType::Bishop:
      return PST_B_EG[sq];
    case PieceType::Rook:
      return PST_R_EG[sq];
    case PieceType::Queen:
      return PST_Q_EG[sq];
    case PieceType::King:
      return PST_K_EG[sq];
    default:
      return 0;
  }
}

}  // namespace lilia::engine
