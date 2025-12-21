#pragma once

#include "lilia/engine/eval_shared.hpp"
#include "lilia/engine/eval_alias.hpp"
#include "lilia/model/board.hpp"
#include "lilia/model/core/bitboard.hpp"
#include "lilia/model/move.hpp"

namespace lilia::engine {

struct EvalAcc {
  // White-POV buckets
  int mg = 0, eg = 0, phase = 0;
  // pieces 2 colors
  int P[2]{}, N[2]{}, B[2]{}, R[2]{}, Q[2]{};
  int kingSq[2]{-1, -1};  // [0]=W, [1]=B

  void clear() {
    mg = eg = phase = 0;
    for (int i = 0; i < 2; ++i) P[i] = N[i] = B[i] = R[i] = Q[i] = 0, kingSq[i] = -1;
  }

  void build_from_board(const model::Board& b);
  inline void add_piece(core::Color c, core::PieceType pt, int sq);
  inline void remove_piece(core::Color c, core::PieceType pt, int sq);
  inline void move_piece(core::Color c, core::PieceType pt, int from, int to);
};

struct EvalDelta {
  core::Color us{};
  core::PieceType moverType{core::PieceType::Pawn};
  core::PieceType capturedType{core::PieceType::None};
  core::PieceType promoType{core::PieceType::None};
  bool isCapture = false, isEnPassant = false, isCastle = false;
  int fromSq = -1, toSq = -1;
  int epCaptureSq = -1;
  int rookFrom = -1, rookTo = -1;
};

inline void EvalAcc::build_from_board(const model::Board& b) {
  using namespace lilia::core;
  using model::bb::Bitboard;
  using model::bb::ctz64;

  clear();
  for (int pt = 0; pt < 6; ++pt) {
    auto PType = static_cast<PieceType>(pt);
    // White
    Bitboard w = b.getPieces(Color::White, PType);
    while (w) {
      int s = ctz64(w);
      w &= (w - 1);
      mg += VAL_MG[pt] + pst_mg(PType, s);
      eg += VAL_EG[pt] + pst_eg(PType, s);
      phase += PHASE_W[pt];
      switch (PType) {
        case PieceType::Pawn:
          P[0]++;
          break;
        case PieceType::Knight:
          N[0]++;
          break;
        case PieceType::Bishop:
          B[0]++;
          break;
        case PieceType::Rook:
          R[0]++;
          break;
        case PieceType::Queen:
          Q[0]++;
          break;
        case PieceType::King:
          kingSq[0] = s;
          break;
        default:
          break;
      }
    }
    // Black
    Bitboard bl = b.getPieces(Color::Black, PType);
    while (bl) {
      int s = ctz64(bl);
      bl &= (bl - 1);
      mg -= VAL_MG[pt] + pst_mg(PType, mirror_sq_black(s));
      eg -= VAL_EG[pt] + pst_eg(PType, mirror_sq_black(s));
      phase += PHASE_W[pt];
      switch (PType) {
        case PieceType::Pawn:
          P[1]++;
          break;
        case PieceType::Knight:
          N[1]++;
          break;
        case PieceType::Bishop:
          B[1]++;
          break;
        case PieceType::Rook:
          R[1]++;
          break;
        case PieceType::Queen:
          Q[1]++;
          break;
        case PieceType::King:
          kingSq[1] = s;
          break;
        default:
          break;
      }
    }
  }
}

inline void EvalAcc::add_piece(lilia::core::Color c, lilia::core::PieceType pt, int sq) {
  const int s = (c == lilia::core::Color::White ? 0 : 1);
  const int i = (int)pt;
  if (c == lilia::core::Color::White) {
    mg += VAL_MG[i] + pst_mg(pt, sq);
    eg += VAL_EG[i] + pst_eg(pt, sq);
  } else {
    mg -= VAL_MG[i] + pst_mg(pt, mirror_sq_black(sq));
    eg -= VAL_EG[i] + pst_eg(pt, mirror_sq_black(sq));
  }
  phase += PHASE_W[i];

  switch (pt) {
    case lilia::core::PieceType::Pawn:
      P[s]++;
      break;
    case lilia::core::PieceType::Knight:
      N[s]++;
      break;
    case lilia::core::PieceType::Bishop:
      B[s]++;
      break;
    case lilia::core::PieceType::Rook:
      R[s]++;
      break;
    case lilia::core::PieceType::Queen:
      Q[s]++;
      break;
    case lilia::core::PieceType::King:
      kingSq[s] = sq;
      break;
    default:
      break;
  }
}

inline void EvalAcc::remove_piece(lilia::core::Color c, lilia::core::PieceType pt, int sq) {
  const int s = (c == lilia::core::Color::White ? 0 : 1);
  const int i = (int)pt;
  if (c == lilia::core::Color::White) {
    mg -= VAL_MG[i] + pst_mg(pt, sq);
    eg -= VAL_EG[i] + pst_eg(pt, sq);
  } else {
    mg += VAL_MG[i] + pst_mg(pt, mirror_sq_black(sq));
    eg += VAL_EG[i] + pst_eg(pt, mirror_sq_black(sq));
  }
  phase -= PHASE_W[i];

  switch (pt) {
    case lilia::core::PieceType::Pawn:
      P[s]--;
      break;
    case lilia::core::PieceType::Knight:
      N[s]--;
      break;
    case lilia::core::PieceType::Bishop:
      B[s]--;
      break;
    case lilia::core::PieceType::Rook:
      R[s]--;
      break;
    case lilia::core::PieceType::Queen:
      Q[s]--;
      break;
    case lilia::core::PieceType::King:
      kingSq[s] = -1;
      break;  // optional, for safety
    default:
      break;
  }
}

inline void EvalAcc::move_piece(lilia::core::Color c, lilia::core::PieceType pt, int from, int to) {
  if (c == lilia::core::Color::White) {
    mg -= pst_mg(pt, from);
    eg -= pst_eg(pt, from);
    mg += pst_mg(pt, to);
    eg += pst_eg(pt, to);
  } else {
    mg += pst_mg(pt, mirror_sq_black(from));
    eg += pst_eg(pt, mirror_sq_black(from));
    mg -= pst_mg(pt, mirror_sq_black(to));
    eg -= pst_eg(pt, mirror_sq_black(to));
  }
  if (pt == lilia::core::PieceType::King) kingSq[c == lilia::core::Color::White ? 0 : 1] = to;
}

}  // namespace lilia::engine
