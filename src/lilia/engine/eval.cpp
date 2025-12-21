#include "lilia/engine/eval.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cassert>
#include <cstdint>
#include <limits>

#include "lilia/engine/config.hpp"
#include "lilia/engine/eval_acc.hpp"
#include "lilia/engine/eval_alias.hpp"
#include "lilia/engine/eval_shared.hpp"
#include "lilia/model/core/bitboard.hpp"
#include "lilia/model/core/magic.hpp"
#include "lilia/model/position.hpp"

using namespace lilia::core;
using namespace lilia::model;
using namespace lilia::model::bb;

namespace lilia::engine {

// =============================================================================
// Utility
// =============================================================================

inline int popcnt(Bitboard b) noexcept {
  return popcount(b);
}
inline int lsb_i(Bitboard b) noexcept {
  return b ? ctz64(b) : -1;
}
inline int msb_i(Bitboard b) noexcept {
  return b ? 63 - clz64(b) : -1;
}
inline int clampi(int x, int lo, int hi) {
  return x < lo ? lo : (x > hi ? hi : x);
}
inline int king_manhattan(int a, int b) {
  if (a < 0 || b < 0) return 7;
  const int af = a & 7, bf = b & 7;
  const int ar = a >> 3, br = b >> 3;
  const int df = af > bf ? af - bf : bf - af;
  const int dr = ar > br ? ar - br : br - ar;
  return df + dr;
}

// micro helpers
template <typename T>
inline void prefetch_ro(const T* p) noexcept {
#if defined(__GNUC__) || defined(__clang__)
  __builtin_prefetch(static_cast<const void*>(p), 0 /*read*/, 2 /*temporal*/);
#endif
}

// =============================================================================
// Masks
// =============================================================================
struct Masks {
  std::array<Bitboard, 64> file{};
  std::array<Bitboard, 64> adjFiles{};
  std::array<Bitboard, 64> wPassed{}, bPassed{}, wFront{}, bFront{};
  std::array<Bitboard, 64> kingRing{};
  std::array<Bitboard, 64> wShield{}, bShield{};
  std::array<Bitboard, 64> frontSpanW{}, frontSpanB{};
};

consteval Masks init_masks() {
  Masks m{};
  for (int sq = 0; sq < 64; ++sq) {
    int f = file_of(static_cast<Square>(sq));
    int r = rank_of(static_cast<Square>(sq));

    Bitboard fm = 0;
    for (int rr = 0; rr < 8; ++rr) fm |= sq_bb(static_cast<Square>((rr << 3) | f));
    m.file[sq] = fm;

    Bitboard adj = 0;
    if (f > 0)
      for (int rr = 0; rr < 8; ++rr) adj |= sq_bb(static_cast<Square>((rr << 3) | (f - 1)));
    if (f < 7)
      for (int rr = 0; rr < 8; ++rr) adj |= sq_bb(static_cast<Square>((rr << 3) | (f + 1)));
    m.adjFiles[sq] = adj;

    Bitboard pw = 0;
    for (int rr = r + 1; rr < 8; ++rr)
      for (int ff = std::max(0, f - 1); ff <= std::min(7, f + 1); ++ff)
        pw |= sq_bb(static_cast<Square>((rr << 3) | ff));
    Bitboard pb = 0;
    for (int rr = r - 1; rr >= 0; --rr)
      for (int ff = std::max(0, f - 1); ff <= std::min(7, f + 1); ++ff)
        pb |= sq_bb(static_cast<Square>((rr << 3) | ff));
    m.wPassed[sq] = pw;
    m.bPassed[sq] = pb;

    Bitboard wf = 0;
    for (int rr = r + 1; rr < 8; ++rr) wf |= sq_bb(static_cast<Square>((rr << 3) | f));
    m.wFront[sq] = wf;
    m.frontSpanW[sq] = wf;

    Bitboard bf = 0;
    for (int rr = r - 1; rr >= 0; --rr) bf |= sq_bb(static_cast<Square>((rr << 3) | f));
    m.bFront[sq] = bf;
    m.frontSpanB[sq] = bf;

    // King-Ring
    Bitboard ring = 0;
    for (int dr = -KING_RING_RADIUS; dr <= KING_RING_RADIUS; ++dr)
      for (int df = -KING_RING_RADIUS; df <= KING_RING_RADIUS; ++df) {
        int nr = r + dr, nf = f + df;
        if (0 <= nr && nr < 8 && 0 <= nf && nf < 8)
          ring |= sq_bb(static_cast<Square>((nr << 3) | nf));
      }
    m.kingRing[sq] = ring;

    // Shields per color
    Bitboard shW = 0;
    for (int dr = 1; dr <= KING_SHIELD_DEPTH; ++dr) {
      int nr = r + dr;
      if (nr >= 8) break;
      for (int df = -1; df <= 1; ++df) {
        int nf = f + df;
        if (0 <= nf && nf < 8) shW |= sq_bb(static_cast<Square>((nr << 3) | nf));
      }
    }
    m.wShield[sq] = shW;

    Bitboard shB = 0;
    for (int dr = 1; dr <= KING_SHIELD_DEPTH; ++dr) {
      int nr = r - dr;
      if (nr < 0) break;
      for (int df = -1; df <= 1; ++df) {
        int nf = f + df;
        if (0 <= nf && nf < 8) shB |= sq_bb(static_cast<Square>((nr << 3) | nf));
      }
    }
    m.bShield[sq] = shB;
  }
  return m;
}
static constexpr Masks M = init_masks();

// =============================================================================
// Tunables – structure & style
// =============================================================================
constexpr Bitboard CENTER4 =
    sq_bb(Square(27)) | sq_bb(Square(28)) | sq_bb(Square(35)) | sq_bb(Square(36));

// Material imbalance (leicht)
struct MaterialCounts {
  int P[2]{}, N[2]{}, B[2]{}, R[2]{}, Q[2]{};
};
static int material_imbalance(const MaterialCounts& mc) {
  auto s = [&](int w, int b, int kw, int kb) {
    return (kw * (w * (w - 1)) / 2) - (kb * (b * (b - 1)) / 2);
  };
  int sc = 0;
  sc += s(mc.N[0], mc.N[1], 3, 3);
  sc += s(mc.B[0], mc.B[1], 4, 4);
  sc += (mc.B[0] >= 2 ? +16 : 0) + (mc.B[1] >= 2 ? -16 : 0);
  sc += (mc.R[0] * mc.N[0] * 2) - (mc.R[1] * mc.N[1] * 2);
  sc += (mc.R[0] * mc.B[0] * 1) - (mc.R[1] * mc.B[1] * 1);
  sc += (mc.Q[0] * mc.R[0] * (-2)) - (mc.Q[1] * mc.R[1] * (-2));
  return sc;
}

// =============================================================================
// Space
// =============================================================================
static int space_term(const std::array<Bitboard, 6>& W, const std::array<Bitboard, 6>& B,
                      Bitboard wPA, Bitboard bPA) {
  Bitboard wocc = W[0] | W[1] | W[2] | W[3] | W[4] | W[5];
  Bitboard bocc = B[0] | B[1] | B[2] | B[3] | B[4] | B[5];

  // Own-half masks (relative)
  Bitboard wMask = RANK_2 | RANK_3 | RANK_4;
  Bitboard bMask = RANK_7 | RANK_6 | RANK_5;

  Bitboard occ = wocc | bocc;
  Bitboard empty = ~occ;

  int wSafe = popcount((wMask & empty) & ~bPA);
  int bSafe = popcount((bMask & empty) & ~wPA);

  int wMin = popcount(W[1] | W[2]), bMin = popcount(B[1] | B[2]);
  int wScale = SPACE_SCALE_BASE + std::min(wMin, SPACE_MINOR_SATURATION);
  int bScale = SPACE_SCALE_BASE + std::min(bMin, SPACE_MINOR_SATURATION);

  int raw = SPACE_BASE * (wSafe * wScale - bSafe * bScale);
  raw = std::clamp(raw, -SPACE_CLAMP, SPACE_CLAMP);  // e.g.  ±200
  return raw;
}

// =============================================================================
// Pawn structure (MG/EG SPLIT + PawnTT mg/eg)
// =============================================================================

struct PawnOnly {
  int mg = 0, eg = 0;
  Bitboard wPass = 0, bPass = 0;
};

static PawnOnly pawn_structure_pawnhash_only(Bitboard wp, Bitboard bp, Bitboard wPA, Bitboard bPA) {
  PawnOnly out{};
  int& mg = out.mg;
  int& eg = out.eg;

  // Isolani & doubled (file-wise)
  for (int f = 0; f < 8; ++f) {
    Bitboard F = M.file[f];
    Bitboard ADJ = (f > 0 ? M.file[f - 1] : 0) | (f < 7 ? M.file[f + 1] : 0);
    int wc = popcount(wp & F), bc = popcount(bp & F);
    if (wc) {
      if (!(wp & ADJ)) {
        mg -= ISO_P * wc;
        eg -= (ISO_P * wc) / 2;
      }
      if (wc > 1) {
        mg -= DOUBLED_P * (wc - 1);
        eg -= (DOUBLED_P * (wc - 1)) / 2;
      }
    }
    if (bc) {
      if (!(bp & ADJ)) {
        mg += ISO_P * bc;
        eg += (ISO_P * bc) / 2;
      }
      if (bc > 1) {
        mg += DOUBLED_P * (bc - 1);
        eg += (DOUBLED_P * (bc - 1)) / 2;
      }
    }
  }

  // Phalanx/Candidate (pawn-only)
  Bitboard t = wp;
  while (t) {
    int s = lsb_i(t);
    t &= t - 1;
    int f = file_of(s), r = rank_of(s);
    if (f > 0 && (wp & sq_bb(Square(s - 1)))) {
      mg += PHALANX;
      eg += PHALANX / 2;
    }
    if (f < 7 && (wp & sq_bb(Square(s + 1)))) {
      mg += PHALANX;
      eg += PHALANX / 2;
    }
    bool passed = (M.wPassed[s] & bp) == 0;
    bool candidate = !passed && ((M.wPassed[s] & bp & ~M.wFront[s]) == 0);
    if (candidate) {
      mg += CANDIDATE_P;
      eg += CANDIDATE_P / 2;
    }
    if (passed) {
      mg += PASSED_MG[r];
      eg += PASSED_EG[r];
      out.wPass |= sq_bb(Square(s));
      int steps = 7 - r;
      if (steps <= 2) {
        mg += PASS_NEAR_PROMO_STEP2_MG;
        eg += PASS_NEAR_PROMO_STEP2_EG;
      } else if (steps == 3) {
        mg += PASS_NEAR_PROMO_STEP3_MG;
        eg += PASS_NEAR_PROMO_STEP3_EG;
      }
    }
  }
  t = bp;
  while (t) {
    int s = lsb_i(t);
    t &= t - 1;
    int f = file_of(s), r = rank_of(s);
    if (f > 0 && (bp & sq_bb(Square(s - 1)))) {
      mg -= PHALANX;
      eg -= PHALANX / 2;
    }
    if (f < 7 && (bp & sq_bb(Square(s + 1)))) {
      mg -= PHALANX;
      eg -= PHALANX / 2;
    }
    bool passed = (M.bPassed[s] & wp) == 0;
    bool candidate = !passed && ((M.bPassed[s] & wp & ~M.bFront[s]) == 0);
    if (candidate) {
      mg -= CANDIDATE_P;
      eg -= CANDIDATE_P / 2;
    }
    if (passed) {
      mg -= PASSED_MG[7 - rank_of(s)];
      eg -= PASSED_EG[7 - rank_of(s)];
      out.bPass |= sq_bb(Square(s));
      int steps = rank_of(s);
      if (steps <= 2) {
        mg -= PASS_NEAR_PROMO_STEP2_MG;
        eg -= PASS_NEAR_PROMO_STEP2_EG;
      } else if (steps == 3) {
        mg -= PASS_NEAR_PROMO_STEP3_MG;
        eg -= PASS_NEAR_PROMO_STEP3_EG;
      }
    }
  }

  // helpers stay as you wrote them
  auto rank_ge_mask = [](int r) {
    Bitboard m = 0;
    for (int rr = r; rr < 8; ++rr) m |= (RANK_1 << (8 * rr));
    return m;
  };
  auto rank_le_mask = [](int r) {
    Bitboard m = 0;
    for (int rr = 0; rr <= r; ++rr) m |= (RANK_1 << (8 * rr));
    return m;
  };

  // --- White backward pawns ---
  {
    Bitboard t = wp;
    while (t) {
      int s = lsb_i(t);
      t &= t - 1;

      // promotion rank has no forward square
      if (rank_of(s) == 7) continue;

      // not passed (exclude genuine passers)
      if ((M.wPassed[s] & bp) == 0) continue;

      const int r = rank_of(s);
      const int front = s + 8;
      Bitboard frontBB = sq_bb((Square)front);

      // enemy controls the front square, own pawns do not
      bool enemyControls = (bPA & frontBB) != 0;
      bool ownControls = (wPA & frontBB) != 0;
      if (!enemyControls || ownControls) continue;

      // any friendly pawn on adjacent files at or ahead of this rank?
      Bitboard supportersSame = (M.adjFiles[s] & wp & rank_le_mask(r));
      if (supportersSame) continue;

      // it's backward: penalize white
      mg -= BACKWARD_P;
      eg -= BACKWARD_P / 2;
    }
  }

  // --- Black backward pawns ---
  {
    Bitboard t = bp;
    while (t) {
      int s = lsb_i(t);
      t &= t - 1;

      if (rank_of(s) == 0) continue;
      if ((M.bPassed[s] & wp) == 0) continue;

      const int r = rank_of(s);
      const int front = s - 8;
      Bitboard frontBB = sq_bb((Square)front);

      bool enemyControls = (wPA & frontBB) != 0;
      bool ownControls = (bPA & frontBB) != 0;
      if (!enemyControls || ownControls) continue;

      Bitboard supportersSame = (M.adjFiles[s] & bp & rank_ge_mask(r));
      if (supportersSame) continue;

      // it's backward: penalize black (i.e., bonus for white)
      mg += BACKWARD_P;
      eg += BACKWARD_P / 2;
    }
  }

  // connected passers (pawn-only) – prevent a/h wrap
  Bitboard wConn =
      (((out.wPass & ~FILE_H) << 1) & out.wPass) | (((out.wPass & ~FILE_A) >> 1) & out.wPass);
  Bitboard bConn =
      (((out.bPass & ~FILE_H) << 1) & out.bPass) | (((out.bPass & ~FILE_A) >> 1) & out.bPass);
  int wC = popcount(wConn), bC = popcount(bConn);
  mg += (CONNECTED_PASSERS / 2) * (wC - bC);
  eg += CONNECTED_PASSERS * (wC - bC);

  return out;
}

struct PasserDyn {
  int mg = 0, eg = 0;
};

// =============================================================================
// Threats & hanging
// =============================================================================
struct AttackMap {
  Bitboard wAll{0}, bAll{0};
  Bitboard wPA{0}, bPA{0};
  Bitboard wKAtt{0}, bKAtt{0};
  Bitboard wPass{0}, bPass{0};

  // NEU: per-Typ Angriffe (occ-abhängig)
  Bitboard wN{0}, wB{0}, wR{0}, wQ{0};
  Bitboard bN{0}, bB{0}, bR{0}, bQ{0};

  // Cached slider rays (per piece square)
  Bitboard wBPos{0}, wRPos{0}, wQPos{0};
  Bitboard bBPos{0}, bRPos{0}, bQPos{0};
  std::array<Bitboard, 64> wBishopRays{};
  std::array<Bitboard, 64> bBishopRays{};
  std::array<Bitboard, 64> wRookRays{};
  std::array<Bitboard, 64> bRookRays{};
  std::array<Bitboard, 64> wQueenBishopRays{};
  std::array<Bitboard, 64> bQueenBishopRays{};
  std::array<Bitboard, 64> wQueenRookRays{};
  std::array<Bitboard, 64> bQueenRookRays{};
};

inline Bitboard cached_slider_attacks(const AttackMap* A, bool white, magic::Slider s, int sq,
                                      Bitboard occ) {
  if (!A || sq < 0) return magic::sliding_attacks(s, static_cast<Square>(sq), occ);
  Bitboard mask = sq_bb(static_cast<Square>(sq));
  if (s == magic::Slider::Bishop) {
    if (white) {
      if (A->wBPos & mask) return A->wBishopRays[sq];
      if (A->wQPos & mask) return A->wQueenBishopRays[sq];
    } else {
      if (A->bBPos & mask) return A->bBishopRays[sq];
      if (A->bQPos & mask) return A->bQueenBishopRays[sq];
    }
  } else {
    if (white) {
      if (A->wRPos & mask) return A->wRookRays[sq];
      if (A->wQPos & mask) return A->wQueenRookRays[sq];
    } else {
      if (A->bRPos & mask) return A->bRookRays[sq];
      if (A->bQPos & mask) return A->bQueenRookRays[sq];
    }
  }
  return magic::sliding_attacks(s, static_cast<Square>(sq), occ);
}

static PasserDyn passer_dynamic_bonus(const AttackMap& A, Bitboard occ, int wK, int bK,
                                      Bitboard wPass, Bitboard bPass) {
  PasserDyn d{};

  auto add_side = [&](bool white) {
    Bitboard pass = white ? wPass : bPass;
    int K = white ? wK : bK;
    Bitboard ownNBRQ = white ? (A.wN | A.wB | A.wR | A.wQ) : (A.bN | A.bB | A.bR | A.bQ);
    Bitboard oppKBB = white ? sq_bb(Square(bK)) : sq_bb(Square(wK));
    while (pass) {
      int s = lsb_i(pass);
      pass &= pass - 1;
      int stop = white ? s + 8 : s - 8;
      // block on stop square
      int mgB = 0, egB = 0;
      if (stop >= 0 && stop < 64 && (occ & sq_bb(Square(stop)))) {
        mgB -= PASS_BLOCK;
        egB -= PASS_BLOCK;
      }
      // free path ahead
      if (((white ? M.wFront[s] : M.bFront[s]) & occ) == 0) {
        mgB += PASS_FREE;
        egB += PASS_FREE;
      }
      // piece support (use A, O(1))
      if (ownNBRQ & sq_bb(Square(s))) {
        mgB += PASS_PIECE_SUPP;
        egB += PASS_PIECE_SUPP;
      }
      // king boost / king block
      if (K >= 0 && king_manhattan(K, s) <= 3) {
        mgB += PASS_KBOOST;
        egB += PASS_KBOOST;
      }
      if (oppKBB & ((white ? (M.wFront[s] | (stop < 64 ? sq_bb(Square(stop)) : 0ULL))
                           : (M.bFront[s] | (stop >= 0 ? sq_bb(Square(stop)) : 0ULL))))) {
        mgB -= PASS_KBLOCK;
        egB -= PASS_KBLOCK;
      }
      int oppK = white ? bK : wK;
      if (oppK >= 0) {
        int dist = king_manhattan(oppK, s);
        int prox = std::max(0, 4 - dist) * PASS_KPROX;
        mgB -= prox;
        egB -= prox;
      }
      d.mg += white ? mgB : -mgB;
      d.eg += white ? egB : -egB;
    }
  };

  add_side(true);
  add_side(false);
  return d;
}

// =============================================================================
// Mobility & attacks (safe mobility)
// =============================================================================
struct AttInfo {
  Bitboard wAll = 0, bAll = 0;
  int mg = 0, eg = 0;
};
static AttInfo mobility(Bitboard occ, Bitboard wocc, Bitboard bocc,
                        const std::array<Bitboard, 6>& W, const std::array<Bitboard, 6>& B,
                        Bitboard wPA, Bitboard bPA, AttackMap* A /* optional */) {
  AttInfo ai{};

  const Bitboard safeMaskW = ~wocc & ~bPA;
  const Bitboard safeMaskB = ~bocc & ~wPA;

  // --- Knights ---
  {
    Bitboard bb = W[(int)PieceType::Knight];
    while (bb) {
      const int s = lsb_i(bb);
      bb &= bb - 1;
      const Bitboard a = knight_attacks_from((Square)s);
      ai.wAll |= a;
      if (A) A->wN |= a;
      int c = popcnt(a & safeMaskW);
      if (c > 8) c = 8;
      ai.mg += KN_MOB_MG[c];
      ai.eg += KN_MOB_EG[c];
    }
  }
  {
    Bitboard bb = B[(int)PieceType::Knight];
    while (bb) {
      const int s = lsb_i(bb);
      bb &= bb - 1;
      const Bitboard a = knight_attacks_from((Square)s);
      ai.bAll |= a;
      if (A) A->bN |= a;
      int c = popcnt(a & safeMaskB);
      if (c > 8) c = 8;
      ai.mg -= KN_MOB_MG[c];
      ai.eg -= KN_MOB_EG[c];
    }
  }

  // --- Bishops ---
  {
    Bitboard bb = W[(int)PieceType::Bishop];
    while (bb) {
      const int s = lsb_i(bb);
      bb &= bb - 1;
      const Bitboard a = magic::sliding_attacks(magic::Slider::Bishop, (Square)s, occ);
      ai.wAll |= a;
      if (A) {
        Bitboard sq = sq_bb(static_cast<Square>(s));
        A->wB |= a;
        A->wBPos |= sq;
        A->wBishopRays[s] = a;
      }
      int c = popcnt(a & safeMaskW);
      if (c > 13) c = 13;
      ai.mg += BI_MOB_MG[c];
      ai.eg += BI_MOB_EG[c];
    }
  }
  {
    Bitboard bb = B[(int)PieceType::Bishop];
    while (bb) {
      const int s = lsb_i(bb);
      bb &= bb - 1;
      const Bitboard a = magic::sliding_attacks(magic::Slider::Bishop, (Square)s, occ);
      ai.bAll |= a;
      if (A) {
        Bitboard sq = sq_bb(static_cast<Square>(s));
        A->bB |= a;
        A->bBPos |= sq;
        A->bBishopRays[s] = a;
      }
      int c = popcnt(a & safeMaskB);
      if (c > 13) c = 13;
      ai.mg -= BI_MOB_MG[c];
      ai.eg -= BI_MOB_EG[c];
    }
  }

  // --- Rooks ---
  {
    Bitboard bb = W[(int)PieceType::Rook];
    while (bb) {
      const int s = lsb_i(bb);
      bb &= bb - 1;
      const Bitboard a = magic::sliding_attacks(magic::Slider::Rook, (Square)s, occ);
      ai.wAll |= a;
      if (A) {
        Bitboard sq = sq_bb(static_cast<Square>(s));
        A->wR |= a;
        A->wRPos |= sq;
        A->wRookRays[s] = a;
      }
      int c = popcnt(a & safeMaskW);
      if (c > 14) c = 14;
      ai.mg += RO_MOB_MG[c];
      ai.eg += RO_MOB_EG[c];
    }
  }
  {
    Bitboard bb = B[(int)PieceType::Rook];
    while (bb) {
      const int s = lsb_i(bb);
      bb &= bb - 1;
      const Bitboard a = magic::sliding_attacks(magic::Slider::Rook, (Square)s, occ);
      ai.bAll |= a;
      if (A) {
        Bitboard sq = sq_bb(static_cast<Square>(s));
        A->bR |= a;
        A->bRPos |= sq;
        A->bRookRays[s] = a;
      }
      int c = popcnt(a & safeMaskB);
      if (c > 14) c = 14;
      ai.mg -= RO_MOB_MG[c];
      ai.eg -= RO_MOB_EG[c];
    }
  }

  // --- Queens ---
  {
    Bitboard bb = W[(int)PieceType::Queen];
    while (bb) {
      const int s = lsb_i(bb);
      bb &= bb - 1;
      const Bitboard r = magic::sliding_attacks(magic::Slider::Rook, (Square)s, occ);
      const Bitboard b = magic::sliding_attacks(magic::Slider::Bishop, (Square)s, occ);
      const Bitboard a = r | b;
      ai.wAll |= a;
      if (A) {
        Bitboard sq = sq_bb(static_cast<Square>(s));
        A->wQ |= a;
        A->wQPos |= sq;
        A->wQueenRookRays[s] = r;
        A->wQueenBishopRays[s] = b;
      }
      int c = popcnt(a & safeMaskW);
      if (c > 27) c = 27;
      ai.mg += QU_MOB_MG[c];
      ai.eg += QU_MOB_EG[c];
    }
  }
  {
    Bitboard bb = B[(int)PieceType::Queen];
    while (bb) {
      const int s = lsb_i(bb);
      bb &= bb - 1;
      const Bitboard r = magic::sliding_attacks(magic::Slider::Rook, (Square)s, occ);
      const Bitboard b = magic::sliding_attacks(magic::Slider::Bishop, (Square)s, occ);
      const Bitboard a = r | b;
      ai.bAll |= a;
      if (A) {
        Bitboard sq = sq_bb(static_cast<Square>(s));
        A->bQ |= a;
        A->bQPos |= sq;
        A->bQueenRookRays[s] = r;
        A->bQueenBishopRays[s] = b;
      }
      int c = popcnt(a & safeMaskB);
      if (c > 27) c = 27;
      ai.mg -= QU_MOB_MG[c];
      ai.eg -= QU_MOB_EG[c];
    }
  }

  // final clamp
  if (ai.mg > MOBILITY_CLAMP) ai.mg = MOBILITY_CLAMP;
  if (ai.mg < -MOBILITY_CLAMP) ai.mg = -MOBILITY_CLAMP;
  if (ai.eg > MOBILITY_CLAMP) ai.eg = MOBILITY_CLAMP;
  if (ai.eg < -MOBILITY_CLAMP) ai.eg = -MOBILITY_CLAMP;

  return ai;
}

static int threats(const std::array<Bitboard, 6>& W, const std::array<Bitboard, 6>& B,
                   const AttackMap& A, Bitboard occ) {
  int sc = 0;

  auto pawn_threat_score = [&](Bitboard pa, const std::array<Bitboard, 6>& side) {
    int s = 0;
    if (pa & side[1]) s += THR_PAWN_MINOR;
    if (pa & side[2]) s += THR_PAWN_MINOR;
    if (pa & side[3]) s += THR_PAWN_ROOK;
    if (pa & side[4]) s += THR_PAWN_QUEEN;
    return s;
  };
  sc += pawn_threat_score(A.wPA, B);
  sc -= pawn_threat_score(A.bPA, W);

  int wKsq = lsb_i(W[5]), bKsq = lsb_i(B[5]);
  Bitboard wocc = W[0] | W[1] | W[2] | W[3] | W[4] | W[5];
  Bitboard bocc = B[0] | B[1] | B[2] | B[3] | B[4] | B[5];

  Bitboard wDef = A.wAll | A.wPA | (wKsq >= 0 ? king_attacks_from((Square)wKsq) : 0);
  Bitboard bDef = A.bAll | A.bPA | (bKsq >= 0 ? king_attacks_from((Square)bKsq) : 0);

  Bitboard wHang = ((A.bAll | A.bPA) & wocc) & ~wDef;
  Bitboard bHang = ((A.wAll | A.wPA) & bocc) & ~bDef;

  auto hang_score = [&](Bitboard h, const std::array<Bitboard, 6>& side) {
    int s = 0;
    if (h & side[1]) s += HANG_MINOR;
    if (h & side[2]) s += HANG_MINOR;
    if (h & side[3]) s += HANG_ROOK;
    if (h & side[4]) s += HANG_QUEEN;
    return s;
  };
  sc += hang_score(bHang, B);
  sc -= hang_score(wHang, W);

  if ((A.wN | A.wB) & B[4]) sc += MINOR_ON_QUEEN;
  if ((A.bN | A.bB) & W[4]) sc -= MINOR_ON_QUEEN;

  auto queen_pawn_chase_penalty = [&](bool whiteSide) {
    Bitboard queens = whiteSide ? W[4] : B[4];
    if (!queens) return 0;

    Bitboard enemyPawns = whiteSide ? B[0] : W[0];
    if (!enemyPawns) return 0;

    int penalty = 0;
    const auto pawn_attacks = whiteSide ? black_pawn_attacks : white_pawn_attacks;
    const auto pawn_push_one = whiteSide ? south : north;
    const Bitboard startRank = whiteSide ? RANK_7 : RANK_2;

    Bitboard direct = pawn_attacks(enemyPawns);

    while (queens) {
      int sq = lsb_i(queens);
      queens &= queens - 1;
      Bitboard target = sq_bb(static_cast<Square>(sq));

      if (direct & target) {
        penalty += QUEEN_PAWN_CHASE_IMMEDIATE;
        continue;
      }

      Bitboard pushOne = pawn_push_one(enemyPawns) & ~occ;
      if (pawn_attacks(pushOne) & target) {
        penalty += QUEEN_PAWN_CHASE_SINGLE;
        continue;
      }

      Bitboard startPawns = enemyPawns & startRank;
      Bitboard mid = pawn_push_one(startPawns) & ~occ;
      Bitboard pushTwo = pawn_push_one(mid) & ~occ;
      if (pawn_attacks(pushTwo) & target) {
        penalty += QUEEN_PAWN_CHASE_DOUBLE;
      }
    }

    return penalty;
  };

  sc -= queen_pawn_chase_penalty(true);
  sc += queen_pawn_chase_penalty(false);

  return sc;
}

static int king_safety_raw(const std::array<Bitboard, 6>& W, const std::array<Bitboard, 6>& B,
                           Bitboard /*occ*/, const AttackMap& A, int wK, int bK) {
  auto ring_attacks_fast = [&](int ksq, bool kingIsWhite) -> int {
    if (ksq < 0) return 0;
    Bitboard ring = M.kingRing[ksq];

    int cN = popcount((kingIsWhite ? A.bN : A.wN) & ring);
    int cB = popcount((kingIsWhite ? A.bB : A.wB) & ring);
    int cR = popcount((kingIsWhite ? A.bR : A.wR) & ring);
    int cQ = popcount((kingIsWhite ? A.bQ : A.wQ) & ring);

    int unique =
        popcount((kingIsWhite ? A.bN | A.bB | A.bR | A.bQ : A.wN | A.wB | A.wR | A.wQ) & ring);

    int power = cN * (KS_W_N - 2) + cB * (KS_W_B - 2) + cR * KS_W_R + cQ * (KS_W_Q - 4);
    int score = unique * KS_RING_BONUS +
                (power * std::min(unique, KS_POWER_COUNT_CLAMP)) / KS_POWER_COUNT_CLAMP;

    // Shield / open file / Escape
    Bitboard wp = W[0], bp = B[0];
    Bitboard shield = kingIsWhite ? M.wShield[ksq] : M.bShield[ksq];
    Bitboard ownP = kingIsWhite ? wp : bp;
    int missing = 6 - std::min(6, popcount(ownP & shield));
    score += missing * KS_MISS_SHIELD;

    Bitboard file = M.file[ksq];
    Bitboard oppP = kingIsWhite ? bp : wp;
    bool ownOn = file & ownP, oppOn = file & oppP;
    if (!ownOn && !oppOn)
      score += KS_OPEN_FILE;
    else if (!ownOn && oppOn)
      score += KS_OPEN_FILE / 2;

    Bitboard kAtt = king_attacks_from((Square)ksq);
    Bitboard oppAll = kingIsWhite ? (A.bAll | A.bPA | A.bKAtt) : (A.wAll | A.wPA | A.wKAtt);

    int esc = popcount(
        kAtt &
        ~(W[0] | W[1] | W[2] | W[3] | W[4] | W[5] | B[0] | B[1] | B[2] | B[3] | B[4] | B[5]) &
        ~oppAll);
    score += (KS_ESCAPE_EMPTY - KS_ESCAPE_FACTOR * std::min(esc, 5));

    return std::min(score, KS_CLAMP);
  };

  int sc = 0;
  sc -= ring_attacks_fast(wK, true);
  sc += ring_attacks_fast(bK, false);
  return sc;
}

static int king_shelter_storm(const std::array<Bitboard, 6>& W, const std::array<Bitboard, 6>& B,
                              int wK, int bK) {
  if (wK < 0 || bK < 0) return 0;
  Bitboard wp = W[0], bp = B[0];

  auto fileShelter = [&](int ksq, bool white) {
    const int kFile = file_of(ksq);
    const int kRank = rank_of(ksq);
    const Bitboard ownPawns = white ? wp : bp;
    const Bitboard enemyPawns = white ? bp : wp;
    int total = 0;

    for (int df = -1; df <= 1; ++df) {
      int ff = kFile + df;
      if (ff < 0 || ff > 7) continue;

      const int baseSq = (kRank << 3) | ff;
      const Bitboard mask = white ? M.frontSpanW[baseSq] : M.frontSpanB[baseSq];

      if (white) {
        int nearOwnSq = lsb_i(mask & ownPawns);
        int nearOwnR = (nearOwnSq >= 0 ? (nearOwnSq >> 3) : 8);
        int dist = clampi(nearOwnR - kRank, 0, 7);
        int shelterIdx = 7 - dist;  // closer own pawn => bigger shelter
        total += SHELTER[shelterIdx];

        int nearEnemySq = lsb_i(mask & enemyPawns);
        int nearEnemyR = (nearEnemySq >= 0 ? (nearEnemySq >> 3) : 8);
        int edist = clampi(nearEnemyR - kRank, 0, 7);
        int stormIdx = 7 - edist;  // closer enemy pawn => bigger storm
        total -= STORM[stormIdx] / 2;

      } else {
        int nearOwnSq = msb_i(mask & ownPawns);
        int nearOwnR = (nearOwnSq >= 0 ? (nearOwnSq >> 3) : -1);
        int dist = clampi(kRank - nearOwnR, 0, 7);
        int shelterIdx = 7 - dist;  // closer own pawn => bigger shelter
        total += SHELTER[shelterIdx];

        int nearEnemySq = msb_i(mask & enemyPawns);
        int nearEnemyR = (nearEnemySq >= 0 ? (nearEnemySq >> 3) : -1);
        int edist = clampi(kRank - nearEnemyR, 0, 7);
        int stormIdx = 7 - edist;  // closer enemy pawn => bigger storm
        total -= STORM[stormIdx] / 2;
      }
    }
    return total;
  };

  int sc = fileShelter(wK, true) - fileShelter(bK, false);
  return sc / 2;
}

// =============================================================================
// Style terms
// =============================================================================

inline bool pawns_on_both_wings(Bitboard pawns) noexcept {
  constexpr Bitboard LEFT = FILE_A | FILE_B | FILE_C | FILE_D;
  constexpr Bitboard RIGHT = FILE_E | FILE_F | FILE_G | FILE_H;
  return (pawns & LEFT) && (pawns & RIGHT);
}

static int bishop_pair_term(const std::array<Bitboard, 6>& W, const std::array<Bitboard, 6>& B) {
  int s = 0;
  if (popcnt(W[2]) >= 2) s += BISHOP_PAIR + (pawns_on_both_wings(W[0]) ? 6 : 0);
  if (popcnt(B[2]) >= 2) s -= BISHOP_PAIR + (pawns_on_both_wings(B[0]) ? 6 : 0);
  return s;
}

static int bad_bishop(const std::array<Bitboard, 6>& W, const std::array<Bitboard, 6>& B) {
  auto is_light = [&](int sq) { return ((file_of(sq) + rank_of(sq)) & 1) != 0; };
  int sc = 0;
  auto apply = [&](const std::array<Bitboard, 6>& bb, int sign) {
    Bitboard paw = bb[0];
    bool closedCenter = ((paw & M.file[3]) && (paw & M.file[4]));  // d & e File

    int light = 0, dark = 0;
    Bitboard t = paw;
    while (t) {
      int s = lsb_i(t);
      t &= t - 1;
      (is_light(s) ? ++light : ++dark);
    }
    Bitboard bishops = bb[2];
    while (bishops) {
      int s = lsb_i(bishops);
      bishops &= bishops - 1;
      int same = is_light(s) ? light : dark;
      int pen = (same > BAD_BISHOP_SAME_COLOR_THRESHOLD
                     ? (same - BAD_BISHOP_SAME_COLOR_THRESHOLD) * BAD_BISHOP_PER_PAWN
                     : 0);
      if (pen)
        sc += -(closedCenter ? pen : (pen * BAD_BISHOP_OPEN_NUM / BAD_BISHOP_OPEN_DEN)) * sign;
    }
  };
  apply(W, +1);
  apply(B, -1);
  return sc;
}

static int outposts_center(const std::array<Bitboard, 6>& W, const std::array<Bitboard, 6>& B,
                           Bitboard bPA, Bitboard wPA) {
  int s = 0;
  Bitboard wSup = white_pawn_attacks(W[0]);
  Bitboard bSup = black_pawn_attacks(B[0]);

  auto add_kn = [&](int sq, bool white) {
    bool notAttackedByEnemyPawn = white ? !(bPA & sq_bb((Square)sq)) : !(wPA & sq_bb((Square)sq));
    bool supportedByOwnPawn = white ? (wSup & sq_bb((Square)sq)) : (bSup & sq_bb((Square)sq));
    int r = rank_of(sq);
    bool deepOutpost = white ? (r >= OUTPOST_DEEP_RANK_WHITE) : (r <= OUTPOST_DEEP_RANK_BLACK);

    int add = 0;
    if (notAttackedByEnemyPawn && supportedByOwnPawn && deepOutpost)
      add += OUTPOST_KN + OUTPOST_DEEP_EXTRA;
    if (knight_attacks_from((Square)sq) & CENTER4) add += CENTER_CTRL;
    if (sq_bb((Square)sq) & CENTER4) add += OUTPOST_CENTER_SQ_BONUS;
    return add;
  };

  Bitboard t = W[1];
  while (t) {
    int sq = lsb_i(t);
    t &= t - 1;
    s += add_kn(sq, true);
  }
  t = B[1];
  while (t) {
    int sq = lsb_i(t);
    t &= t - 1;
    s -= add_kn(sq, false);
  }
  return s;
}

static int rim_knights(const std::array<Bitboard, 6>& W, const std::array<Bitboard, 6>& B) {
  Bitboard aF = M.file[0], hF = M.file[7];
  int s = 0;
  s -= popcnt(W[1] & (aF | hF)) * KNIGHT_RIM;
  s += popcnt(B[1] & (aF | hF)) * KNIGHT_RIM;
  return s;
}

static int rook_activity(const std::array<Bitboard, 6>& W, const std::array<Bitboard, 6>& B,
                         Bitboard wp, Bitboard bp, Bitboard wPass, Bitboard bPass, Bitboard wPA,
                         Bitboard bPA, Bitboard occ, int wK, int bK, const AttackMap* A) {
  int s = 0;
  Bitboard wr = W[3], br = B[3];
  if (!wr && !br) return 0;
  auto rank = [&](int sq) { return rank_of(sq); };
  auto openScore = [&](int sq, bool white) {
    Bitboard f = M.file[sq];
    bool own = white ? (f & wp) : (f & bp);
    bool opp = white ? (f & bp) : (f & wp);
    if (!own && !opp) return ROOK_OPEN;
    if (!own && opp) return ROOK_SEMI;
    return 0;
  };
  // base activity and 7th rank
  Bitboard t = wr;
  while (t) {
    int sq = lsb_i(t);
    t &= t - 1;
    s += openScore(sq, true);
    if (rank(sq) == 6) {
      bool tgt = (B[5] & RANK_8) || (B[0] & RANK_7);
      if (tgt) s += ROOK_ON_7TH;
    }
  }
  t = br;
  while (t) {
    int sq = lsb_i(t);
    t &= t - 1;
    s -= openScore(sq, false);
    if (rank(sq) == 1) {
      bool tgt = (W[5] & RANK_1) || (W[0] & RANK_2);
      if (tgt) s -= ROOK_ON_7TH;
    }
  }

  // connected rooks
  auto connected = [&](Bitboard rooks, Bitboard occAll, bool /*whiteSide*/) {
    if (popcnt(rooks) != 2) return false;
    int s1 = lsb_i(rooks);
    Bitboard r2 = rooks & (rooks - 1);
    int s2 = lsb_i(r2);
    Bitboard occ2 = occAll & ~sq_bb((Square)s2);
    Bitboard ray = magic::sliding_attacks(magic::Slider::Rook, (Square)s1, occ2);
    return (ray & sq_bb((Square)s2)) != 0;
  };
  Bitboard occAll = occ;
  if (connected(wr, occAll, true)) s += CONNECTED_ROOKS;
  if (connected(br, occAll, false)) s -= CONNECTED_ROOKS;

  // rook behind passers (uses wPass/bPass)
  auto behind = [&](int rSq, int pSq, bool rookWhite, bool pawnWhite, int full, int half) {
    if (file_of(rSq) != file_of(pSq)) return 0;
    Bitboard ray = cached_slider_attacks(A, rookWhite, magic::Slider::Rook, rSq, occAll);
    if (!(ray & sq_bb((Square)pSq))) return 0;
    if (pawnWhite)
      return (rank_of(rSq) < rank_of(pSq) ? full : half);
    else
      return (rank_of(rSq) > rank_of(pSq) ? full : half);
  };
  t = wr;
  while (t) {
    int rs = lsb_i(t);
    t &= t - 1;
    Bitboard f = M.file[rs] & wPass;
    while (f) {
      int ps = lsb_i(f);
      f &= f - 1;
      s += behind(rs, ps, true, true, ROOK_BEHIND_PASSER, ROOK_BEHIND_PASSER_HALF);
    }
    f = M.file[rs] & bPass;
    while (f) {
      int ps = lsb_i(f);
      f &= f - 1;
      s += behind(rs, ps, true, false, ROOK_BEHIND_PASSER_HALF, ROOK_BEHIND_PASSER_THIRD);
    }
  }
  t = br;
  while (t) {
    int rs = lsb_i(t);
    t &= t - 1;
    Bitboard f = M.file[rs] & bPass;
    while (f) {
      int ps = lsb_i(f);
      f &= f - 1;
      s -= behind(rs, ps, false, false, ROOK_BEHIND_PASSER, ROOK_BEHIND_PASSER_HALF);
    }
    f = M.file[rs] & wPass;
    while (f) {
      int ps = lsb_i(f);
      f &= f - 1;
      s -= behind(rs, ps, false, true, ROOK_BEHIND_PASSER_HALF, ROOK_BEHIND_PASSER_THIRD);
    }
  }

  Bitboard centralFiles = M.file[2] | M.file[3] | M.file[4] | M.file[5];
  auto central_file_bonus = [&](Bitboard rooks, bool white) {
    int sc = 0;
    Bitboard tt = rooks;
    while (tt) {
      int sq = lsb_i(tt);
      tt &= tt - 1;
      Bitboard attacks = cached_slider_attacks(A, white, magic::Slider::Rook, sq, occ);
      if (centralFiles & (sq_bb((Square)sq) | attacks)) sc += ROOK_CENTRAL_FILE;
    }
    return white ? sc : -sc;
  };
  s += central_file_bonus(wr, true);
  s += central_file_bonus(br, false);

  if (wK >= 0) {
    Bitboard f = M.file[wK];
    bool own = f & wp;
    bool opp = f & bp;
    if (!own && opp) s += ROOK_SEMI_ON_KING_FILE;
    if (!own && !opp) s += ROOK_OPEN_ON_KING_FILE;
  }
  if (bK >= 0) {
    Bitboard f = M.file[bK];
    bool own = f & bp;
    bool opp = f & wp;
    if (!own && opp) s -= ROOK_SEMI_ON_KING_FILE;
    if (!own && !opp) s -= ROOK_OPEN_ON_KING_FILE;
  }

  auto king_file_pressure = [&](bool white) {
    int ksq = white ? lsb_i(B[5]) : lsb_i(W[5]);
    if (ksq < 0) return 0;
    Bitboard rooks = white ? W[3] : B[3];
    Bitboard oppPA = white ? bPA : wPA;

    int total = 0;
    Bitboard t = rooks;
    while (t) {
      int rsq = lsb_i(t);
      t &= t - 1;

      // rook ray and the king’s file
      Bitboard ray = cached_slider_attacks(A, white, magic::Slider::Rook, rsq, occ);
      Bitboard fileToKing = ray & M.file[ksq];
      if (!(fileToKing & sq_bb((Square)ksq))) continue;  // not pointing at king

      // squares strictly between rook and king:
      Bitboard between =
          fileToKing &
          cached_slider_attacks(A, !white, magic::Slider::Rook, ksq, occ | sq_bb((Square)rsq)) &
          ~sq_bb((Square)rsq) & ~sq_bb((Square)ksq);

      int len = popcnt(between);
      int blockedByPawnAtt = popcnt(between & oppPA);
      int free = len - blockedByPawnAtt;

      // Tune these (small numbers):
      total += ROOK_KFILE_PRESS_FREE * free;
      total -= ROOK_KFILE_PRESS_PAWNATT * blockedByPawnAtt;
    }
    return white ? total : -total;
  };

  auto rook_lift_safety = [&](bool white) {
    Bitboard rooks = white ? W[3] : B[3];
    Bitboard oppPA = white ? bPA : wPA;
    int targetRank = white ? 2 : 5;  // zero-based: rank 3 / rank 6
    int sc = 0;
    Bitboard t = rooks;
    while (t) {
      int rsq = lsb_i(t);
      t &= t - 1;
      if (rank_of(rsq) != targetRank) continue;
      if ((sq_bb((Square)rsq) & oppPA) == 0) sc += ROOK_LIFT_SAFE;  // small bump
    }
    return white ? sc : -sc;
  };

  s += rook_lift_safety(true);
  s += rook_lift_safety(false);

  s += king_file_pressure(true);
  s += king_file_pressure(false);

  return s;
}

static int rook_endgame_extras_eg(const std::array<Bitboard, 6>& W,
                                  const std::array<Bitboard, 6>& B, Bitboard wp, Bitboard bp,
                                  Bitboard occ, const AttackMap* A, Bitboard wPass_cached,
                                  Bitboard bPass_cached) {
  int eg = 0;
  Bitboard wr = W[3], br = B[3];

  auto add_progress = [&](bool white) {
    Bitboard rooks = white ? wr : br;
    Bitboard pass = white ? wPass_cached : bPass_cached;
    Bitboard t = rooks;
    while (t) {
      int rs = lsb_i(t);
      t &= t - 1;
      Bitboard f = M.file[rs] & pass;
      while (f) {
        int ps = lsb_i(f);
        f &= f - 1;
        bool beh = (cached_slider_attacks(A, white, magic::Slider::Rook, rs, occ) &
                    sq_bb((Square)ps)) != 0;
        if (!beh) continue;
        auto progress_from_home = [&](int ps, bool w) {
          return w ? rank_of(ps) : (7 - rank_of(ps));
        };
        int advance = std::max(0, progress_from_home(ps, white) - ROOK_PASSER_PROGRESS_START_RANK);
        eg += (white ? +1 : -1) * (advance * ROOK_PASSER_PROGRESS_MULT);
      }
    }
  };
  add_progress(true);
  add_progress(false);

  auto cut_score = [&](bool white) {
    bool rookEnd =
        (popcnt(W[3]) == 1 && popcnt(B[3]) == 1 && (W[1] | W[2] | W[4] | B[1] | B[2] | B[4]) == 0);
    if (!rookEnd) return 0;
    int wK = lsb_i(W[5]), bK = lsb_i(B[5]);
    if (wK < 0 || bK < 0) return 0;
    int sc = 0;
    auto cut_by = [&](Bitboard r, int ksq, int sign) {
      int rsq = lsb_i(r);
      if (file_of(rsq) == file_of(ksq)) {
        int diff = std::abs(rank_of(rsq) - rank_of(ksq));
        if (diff >= ROOK_CUT_MIN_SEPARATION) sc += sign * ROOK_CUT_BONUS;
      } else if (rank_of(rsq) == rank_of(ksq)) {
        int diff = std::abs(file_of(rsq) - file_of(ksq));
        if (diff >= ROOK_CUT_MIN_SEPARATION) sc += sign * ROOK_CUT_BONUS;
      }
    };
    if (white) {
      if (wr) cut_by(wr, bK, +1);
    } else {
      if (br) cut_by(br, wK, -1);
    }
    return sc;
  };
  eg += cut_score(true);
  eg += cut_score(false);

  return eg;
}

static int passer_blocker_quality(const std::array<Bitboard, 6>& W,
                                  const std::array<Bitboard, 6>& B, Bitboard wp, Bitboard bp,
                                  Bitboard occ) {
  int sc = 0;

  auto add_side = [&](bool white) {
    Bitboard paw = white ? wp : bp;
    Bitboard opp = white ? bp : wp;
    int sgn = white ? +1 : -1;  // white POV: white passer help = +, hindrance = -

    Bitboard t = paw;
    while (t) {
      int s = lsb_i(t);
      t &= t - 1;
      bool passed = white ? ((M.wPassed[s] & opp) == 0) : ((M.bPassed[s] & opp) == 0);
      if (!passed) continue;

      int stop = white ? (s + 8) : (s - 8);
      if (stop < 0 || stop > 63) continue;
      Bitboard stopBB = sq_bb((Square)stop);
      if (!(occ & stopBB)) continue;

      int advance = white ? rank_of((Square)s) : (7 - rank_of((Square)s));
      int pen = 0;
      if (stopBB & (W[1] | B[1]))
        pen = BLOCK_PASSER_STOP_KNIGHT;  // best stopper
      else if (stopBB & (W[2] | B[2]))
        pen = BLOCK_PASSER_STOP_BISHOP;

      sc += sgn * (-pen * advance);  // penalty for the passer’s side
    }
  };

  add_side(true);
  add_side(false);
  return sc;
}

// =============================================================================
// King tropism
// =============================================================================
static int king_tropism(const std::array<Bitboard, 6>& W, const std::array<Bitboard, 6>& B) {
  int wK = lsb_i(W[5]);
  int bK = lsb_i(B[5]);
  if (wK < 0 || bK < 0) return 0;
  int sc = 0;
  auto add = [&](Bitboard bb, int target, int sign, int base) {
    Bitboard t = bb;
    while (t) {
      int s = lsb_i(t);
      t &= t - 1;
      int d = king_manhattan(s, target);
      sc += sign * std::max(0, base - TROPISM_DIST_FACTOR * d);
    }
  };
  add(W[1], bK, +1, TROPISM_BASE_KN);
  add(W[2], bK, +1, TROPISM_BASE_BI);
  add(W[3], bK, +1, TROPISM_BASE_RO);
  add(W[4], bK, +1, TROPISM_BASE_QU);
  add(B[1], wK, -1, TROPISM_BASE_KN);
  add(B[2], wK, -1, TROPISM_BASE_BI);
  add(B[3], wK, -1, TROPISM_BASE_RO);
  add(B[4], wK, -1, TROPISM_BASE_QU);
  return sc / TROPISM_EG_DEN;
}

static inline int center_manhattan(int sq) {
  if (sq < 0) return 6;
  int d1 = king_manhattan(sq, 27);  // d4
  int d2 = king_manhattan(sq, 28);  // e4
  int d3 = king_manhattan(sq, 35);  // d5
  int d4 = king_manhattan(sq, 36);  // e5
  return std::min(std::min(d1, d2), std::min(d3, d4));
}
static int king_activity_eg(const std::array<Bitboard, 6>& W, const std::array<Bitboard, 6>& B) {
  int wK = lsb_i(W[5]);
  int bK = lsb_i(B[5]);
  if (wK < 0 || bK < 0) return 0;
  return (center_manhattan(bK) - center_manhattan(wK)) * KING_ACTIVITY_EG_MULT;
}

// Passed-pawn-race EG
static int passed_pawn_race_eg(const std::array<Bitboard, 6>& W, const std::array<Bitboard, 6>& B,
                               const Position& pos) {
  int minorMajor = popcnt(W[1] | W[2] | W[3] | B[1] | B[2] | B[3]);
  if ((PASS_RACE_NEED_QUEENLESS && popcnt(W[4] | B[4]) != 0) ||
      minorMajor > PASS_RACE_MAX_MINORMAJOR)
    return 0;

  int wK = lsb_i(W[5]), bK = lsb_i(B[5]);
  Bitboard wp = W[0], bp = B[0];
  int sc = 0;
  auto prom_sq = [](int sq, bool w) { return w ? ((sq & 7) | (7 << 3)) : ((sq & 7) | (0 << 3)); };
  auto eta = [&](bool white, int sq) -> int {
    int steps = white ? (7 - rank_of(sq)) : (rank_of(sq));
    int stmAdj = (pos.getState().sideToMove == (white ? Color::White : Color::Black))
                     ? 0
                     : PASS_RACE_STM_ADJ;
    return steps + stmAdj;
  };

  // white
  Bitboard t = wp;
  while (t) {
    int s = lsb_i(t);
    t &= t - 1;
    bool passed = (M.wPassed[s] & bp) == 0;
    if (!passed) continue;
    int q = prom_sq(s, true);
    int wETA = eta(true, s);
    int bKETA = king_manhattan(bK, q);
    sc += PASS_RACE_MULT * (bKETA - wETA);
  }
  // black
  t = bp;
  while (t) {
    int s = lsb_i(t);
    t &= t - 1;
    bool passed = (M.bPassed[s] & wp) == 0;
    if (!passed) continue;
    int q = prom_sq(s, false);
    int bETA = eta(false, s);
    int wKETA = king_manhattan(wK, q);
    sc -= PASS_RACE_MULT * (wKETA - bETA);
  }
  return sc;
}

// =============================================================================
// Development & piece blocking
// =============================================================================
static int development(const std::array<Bitboard, 6>& W, const std::array<Bitboard, 6>& B) {
  Bitboard wMin = W[1] | W[2];
  Bitboard bMin = B[1] | B[2];
  static constexpr Bitboard wInit =
      sq_bb(Square(1)) | sq_bb(Square(6)) | sq_bb(Square(2)) | sq_bb(Square(5));
  static constexpr Bitboard bInit =
      sq_bb(Square(57)) | sq_bb(Square(62)) | sq_bb(Square(58)) | sq_bb(Square(61));
  int dW = popcnt(wMin & wInit);
  int dB = popcnt(bMin & bInit);
  int score = (dB - dW) * DEVELOPMENT_PIECE_ON_HOME_PENALTY;

  Bitboard wRook = W[3];
  Bitboard bRook = B[3];
  static constexpr Bitboard wRInit = sq_bb(Square(0)) | sq_bb(Square(7));
  static constexpr Bitboard bRInit = sq_bb(Square(56)) | sq_bb(Square(63));
  int rW = popcnt(wRook & wRInit);
  int rB = popcnt(bRook & bRInit);
  score += (rB - rW) * DEVELOPMENT_ROOK_ON_HOME_PENALTY;

  Bitboard wQueen = W[4];
  Bitboard bQueen = B[4];
  static constexpr Bitboard wQInit = sq_bb(Square(3));
  static constexpr Bitboard bQInit = sq_bb(Square(59));
  int qW = popcnt(wQueen & wQInit);
  int qB = popcnt(bQueen & bQInit);
  score += (qB - qW) * DEVELOPMENT_QUEEN_ON_HOME_PENALTY;

  return score;
}

static int piece_blocking(const std::array<Bitboard, 6>& W, const std::array<Bitboard, 6>& B) {
  int s = 0;
  static constexpr Bitboard wBlock = sq_bb(Square(1 * 8 + 2)) | sq_bb(Square(1 * 8 + 3));  // c2,d2
  static constexpr Bitboard bBlock = sq_bb(Square(6 * 8 + 2)) | sq_bb(Square(6 * 8 + 3));  // c7,d7
  if ((W[1] | W[2]) & wBlock) s -= PIECE_BLOCKING_PENALTY;
  if ((B[1] | B[2]) & bBlock) s += PIECE_BLOCKING_PENALTY;
  return s;
}

// =============================================================================
// Endgame scalers
// =============================================================================
static inline int kdist_cheb(int a, int b) {
  if (a < 0 || b < 0) return 7;
  int df = std::abs((a & 7) - (b & 7));
  int dr = std::abs((a >> 3) - (b >> 3));
  return std::max(df, dr);
}

static int endgame_scale(const std::array<Bitboard, 6>& W, const std::array<Bitboard, 6>& B) {
  auto cnt = [&](int pt, int side) { return popcnt(side ? B[pt] : W[pt]); };
  int wP = cnt(0, 0), bP = cnt(0, 1), wN = cnt(1, 0), bN = cnt(1, 1), wB = cnt(2, 0),
      bB = cnt(2, 1), wR = cnt(3, 0), bR = cnt(3, 1), wQ = cnt(4, 0), bQ = cnt(4, 1);

  int wK = lsb_i(W[5]);
  int bK = lsb_i(B[5]);

  auto on_fileA = [&](Bitboard paw) { return (paw & M.file[0]) != 0; };
  auto on_fileH = [&](Bitboard paw) { return (paw & M.file[7]) != 0; };
  auto is_corner_pawn = [&](Bitboard paw) { return on_fileA(paw) || on_fileH(paw); };

  // K + Pawn position draw
  if (wP == 1 && bP == 0 && (wN | wB | wR | wQ | bN | bB | bR | bQ) == 0) {
    if (on_fileA(W[0]) && bK == 56 /*a8*/) return SCALE_DRAW;
    if (on_fileH(W[0]) && bK == 63 /*h8*/) return SCALE_DRAW;
  }
  if (bP == 1 && wP == 0 && (wN | wB | wR | wQ | bN | bB | bR | bQ) == 0) {
    if (on_fileA(B[0]) && wK == 0 /*a1*/) return SCALE_DRAW;
    if (on_fileH(B[0]) && wK == 7 /*h1*/) return SCALE_DRAW;
  }

  // Opposite-colored bishops only
  bool onlyBish = ((W[1] | W[3] | W[4] | B[1] | B[3] | B[4]) == 0) && wB == 1 && bB == 1;
  if (onlyBish) {
    int wBsq = lsb_i(W[2]);
    int bBsq = lsb_i(B[2]);
    bool wLight = ((file_of(wBsq) + rank_of(wBsq)) & 1) != 0;
    bool bLight = ((file_of(bBsq) + rank_of(bBsq)) & 1) != 0;
    if (wLight != bLight) return OPP_BISHOPS_SCALE;
  }

  // wrong bishop + sidepawn (K+B+P(a/h) vs K)
  if (wB == 1 && wP == 1 && is_corner_pawn(W[0]) && (bP | bN | bB | bR | bQ) == 0) {
    int corner = on_fileA(W[0]) ? 56 /*a8*/ : 63 /*h8*/;
    int d = kdist_cheb(bK, corner);
    if (d <= 1) return SCALE_DRAW;
    if (d <= 2) return SCALE_VERY_DRAWISH;
    return SCALE_MEDIUM;
  }
  if (bB == 1 && bP == 1 && is_corner_pawn(B[0]) && (wP | wN | wB | wR | wQ) == 0) {
    int corner = on_fileA(B[0]) ? 0 /*a1*/ : 7 /*h1*/;
    int d = kdist_cheb(wK, corner);
    if (d <= 1) return SCALE_DRAW;
    if (d <= 2) return SCALE_VERY_DRAWISH;
    return SCALE_MEDIUM;
  }

  // R+side-/double pawn vs R
  if (wR == 1 && bR == 1 && (wP <= 2) && is_corner_pawn(W[0]) && bP == 0) {
    int corner = on_fileA(W[0]) ? 56 : 63;
    int d = kdist_cheb(bK, corner);
    return (d <= 2 ? SCALE_VERY_DRAWISH : SCALE_REDUCED);
  }
  if (bR == 1 && wR == 1 && (bP <= 2) && is_corner_pawn(B[0]) && wP == 0) {
    int corner = on_fileA(B[0]) ? 0 : 7;
    int d = kdist_cheb(wK, corner);
    return (d <= 2 ? SCALE_VERY_DRAWISH : SCALE_REDUCED);
  }

  // N+sidepawn vs K
  if (wN == 1 && wP == 1 && is_corner_pawn(W[0]) && (bN | bB | bR | bQ | bP) == 0)
    return KN_CORNER_PAWN_SCALE;
  if (bN == 1 && bP == 1 && is_corner_pawn(B[0]) && (wN | wB | wR | wQ | wP) == 0)
    return KN_CORNER_PAWN_SCALE;

  // No pawns, no heavy pieces -> scale strongly toward draw in bare-minor cases.
  if (wP == 0 && bP == 0 && wR == 0 && bR == 0 && wQ == 0 && bQ == 0) {
    int wMin = wN + wB, bMin = bN + bB;
    // K vs K / KN vs K / KB vs K / KN vs KN / KB vs KB
    if (wMin <= 1 && bMin <= 1) return SCALE_DRAW;
    // K  2N vs K (generally drawn with best play)
    if ((wN == 2 && wB == 0 && bMin == 0) || (bN == 2 && bB == 0 && wMin == 0))
      return SCALE_VERY_DRAWISH;
    // Minor vs minor of the same type: very drawish without pawns
    if ((wMin == 1 && bMin == 1) && ((wN == bN) || (wB == bB))) return SCALE_VERY_DRAWISH;
  }

  return FULL_SCALE;
}

// =============================================================================
// Extra: castles & center
// =============================================================================
static void castling_and_center(const std::array<Bitboard, 6>& W, const std::array<Bitboard, 6>& B,
                                int& mg_add, int& eg_add) {
  int wK = lsb_i(W[5]);
  int bK = lsb_i(B[5]);
  bool queensOn = (W[4] | B[4]) != 0;
  auto center_penalty = [&](int ksq, bool white) {
    if (ksq < 0) return 0;

    // Normalize to "white viewpoint" for the positional test
    int ksq_w = white ? ksq : mirror_sq_black(ksq);
    bool centerBack = (ksq_w == 4 || ksq_w == 3 || ksq_w == 5);  // e1/d1/f1 from white POV
    if (!centerBack) return 0;

    Bitboard fileE = M.file[white ? 4 : mirror_sq_black(4)];
    Bitboard fileD = M.file[white ? 3 : mirror_sq_black(3)];

    Bitboard ownP = white ? W[0] : B[0];
    Bitboard oppP = white ? B[0] : W[0];

    int amp = 0;
    auto openish = [&](Bitboard f) {
      bool own = (f & ownP) != 0;
      bool opp = (f & oppP) != 0;
      if (!own && !opp) return CENTER_BACK_OPEN_FILE_OPEN;
      if (!own && opp) return CENTER_BACK_OPEN_FILE_SEMI;
      return 0;
    };
    amp += openish(fileD) + openish(fileE);

    int base = ((W[4] | B[4]) != 0) ? CENTER_BACK_PENALTY_Q_ON : CENTER_BACK_PENALTY_Q_OFF;
    return base + amp * CENTER_BACK_OPEN_FILE_WEIGHT;
  };

  auto castle_bonus = [&](int ksq) { return (ksq == 6 || ksq == 2) ? CASTLE_BONUS : 0; };
  mg_add += castle_bonus(wK);
  mg_add -= castle_bonus(mirror_sq_black(bK));
  mg_add += center_penalty(bK, false);
  mg_add -= center_penalty(wK, true);
  eg_add += (castle_bonus(wK) / 2) - (castle_bonus(mirror_sq_black(bK)) / 2);

  auto early_queen_malus = [&](const std::array<Bitboard, 6>& S, bool white) {
    Bitboard Q = S[4];
    Bitboard minorsHome = white ? (RANK_1 & (S[1] | S[2])) : (RANK_8 & (S[1] | S[2]));
    Bitboard qZone = white ? (RANK_2 | RANK_3) : (RANK_7 | RANK_6);
    return (((Q & qZone) != 0ULL) && (minorsHome != 0ULL)) ? EARLY_QUEEN_MALUS : 0;
  };

  int eqmW = early_queen_malus(W, true);
  int eqmB = early_queen_malus(B, false);
  mg_add += -eqmW + eqmB;

  // not castled heuristic
  if (queensOn) {
    bool wUncastled = (wK == 4) && rook_on_start_square(W[3], true);
    bool bUncastled = (bK == 60) && rook_on_start_square(B[3], false);
    mg_add +=
        (bUncastled ? +UNCASTLED_PENALTY_Q_ON : 0) - (wUncastled ? +UNCASTLED_PENALTY_Q_ON : 0);
  }
}

// =============================================================================
// Eval caches
// =============================================================================
constexpr size_t EVAL_BITS = 14, EVAL_SIZE = 1ULL << EVAL_BITS;
constexpr size_t PAWN_BITS = 12, PAWN_SIZE = 1ULL << PAWN_BITS;
struct alignas(64) EvalEntry {
  std::atomic<uint64_t> key{0};
  std::atomic<int32_t> score{0};
};
struct alignas(64) PawnEntry {
  std::atomic<uint64_t> key{0};
  std::atomic<int32_t> mg{0};
  std::atomic<int32_t> eg{0};
  std::atomic<uint64_t> wPA{0};
  std::atomic<uint64_t> bPA{0};
  std::atomic<uint64_t> wPass{0};
  std::atomic<uint64_t> bPass{0};
};
struct Evaluator::Impl {
  std::array<EvalEntry, EVAL_SIZE> eval;
  std::array<PawnEntry, PAWN_SIZE> pawn;
};
Evaluator::Evaluator() noexcept {
  m_impl = new Impl();
}
Evaluator::~Evaluator() noexcept {
  delete m_impl;
}
void Evaluator::clearCaches() const noexcept {
  if (!m_impl) return;
  for (auto& e : m_impl->eval) {
    e.key.store(0);
    e.score.store(0);
  }
  for (auto& p : m_impl->pawn) {
    p.key.store(0);
    p.mg.store(0);
    p.eg.store(0);
    p.wPA.store(0);
    p.bPA.store(0);
    p.wPass.store(0);
    p.bPass.store(0);
  }
}
static inline size_t idx_eval(uint64_t k) {
  return (size_t)k & (EVAL_SIZE - 1);
}
static inline size_t idx_pawn(uint64_t k) {
  return (size_t)k & (PAWN_SIZE - 1);
}

inline int sgn(int x) {
  return (x > 0) - (x < 0);
}

inline Bitboard rook_pins(Bitboard occ, Bitboard own, Bitboard oppRQ, int ksq, bool kingWhite,
                          const AttackMap* A) {
  if (ksq < 0) return 0ULL;
  Bitboard pins = 0ULL;

  // candidate blockers are own pieces on rook rays from the king
  Bitboard blockers = cached_slider_attacks(A, kingWhite, magic::Slider::Rook, ksq, occ) & own;

  while (blockers) {
    int b = lsb_i(blockers);
    blockers &= blockers - 1;

    int df = sgn(file_of(b) - file_of(ksq));
    int dr = sgn(rank_of(b) - rank_of(ksq));

    int f = file_of(b), r = rank_of(b);
    for (;;) {
      f += df;
      r += dr;
      if (f < 0 || f > 7 || r < 0 || r > 7) break;
      int s = (r << 3) | f;
      Bitboard bb = sq_bb((Square)s);
      if (bb & occ) {
        if (bb & oppRQ) pins |= sq_bb((Square)b);
        break;
      }
    }
  }
  return pins;
}

inline Bitboard bishop_pins(Bitboard occ, Bitboard own, Bitboard oppBQ, int ksq, bool kingWhite,
                            const AttackMap* A) {
  if (ksq < 0) return 0ULL;
  Bitboard pins = 0ULL;

  Bitboard blockers = cached_slider_attacks(A, kingWhite, magic::Slider::Bishop, ksq, occ) & own;

  while (blockers) {
    int b = lsb_i(blockers);
    blockers &= blockers - 1;

    int df = sgn(file_of(b) - file_of(ksq));
    int dr = sgn(rank_of(b) - rank_of(ksq));

    int f = file_of(b), r = rank_of(b);
    for (;;) {
      f += df;
      r += dr;
      if (f < 0 || f > 7 || r < 0 || r > 7) break;
      int s = (r << 3) | f;
      Bitboard bb = sq_bb((Square)s);
      if (bb & occ) {
        if (bb & oppBQ) pins |= sq_bb((Square)b);
        break;
      }
    }
  }
  return pins;
}

inline Bitboard no_king_attacks(bool white, const AttackMap& A) {
  // squares attacked by the *defending* side excluding its king
  return white ? (A.bAll | A.bPA) : (A.wAll | A.wPA);
}

inline int safe_checks(bool white, const std::array<Bitboard, 6>& W,
                       const std::array<Bitboard, 6>& B, Bitboard occ, const AttackMap& A,
                       int oppK) {
  if (oppK < 0) return 0;
  const Bitboard occAll = occ;
  const Bitboard unsafe = no_king_attacks(white, A);
  int sc = 0;

  // knights: checking squares = knight_attacks_from(oppK)
  Bitboard kn = white ? W[1] : B[1];
  // knights: safe checking moves to squares that would check the king
  Bitboard knChk = knight_attacks_from((Square)oppK);  // checking landing squares
  Bitboard ownOcc =
      white ? (W[0] | W[1] | W[2] | W[3] | W[4] | W[5]) : (B[0] | B[1] | B[2] | B[3] | B[4] | B[5]);
  Bitboard canMove = (white ? A.wN : A.bN) & ~ownOcc;  // knight move targets (no self-block)
  sc += popcnt(knChk & canMove & ~unsafe) * KS_SAFE_CHECK_N;

  // replace the whole slider chunk in safe_checks() with this:
  auto add_slider_moves = [&](Bitboard attackedByUs, magic::Slider sl, int w) {
    Bitboard rayFromK = cached_slider_attacks(&A, !white, sl, oppK, occAll);
    Bitboard origins = rayFromK & ~occAll;  // empty squares that would give check
    // additionally require that the origin is not attacked by enemy pawns,
    // which approximates “safe landing square” well and filters noisy hits
    Bitboard notPawnAttacked = origins & ~(white ? A.bPA : A.wPA);
    sc += popcnt(notPawnAttacked & attackedByUs & ~unsafe) * w;
  };

  add_slider_moves(white ? A.wB : A.bB, magic::Slider::Bishop, KS_SAFE_CHECK_B);
  add_slider_moves(white ? A.wR : A.bR, magic::Slider::Rook, KS_SAFE_CHECK_R);
  // queen contributes along both geometries
  add_slider_moves(white ? A.wQ : A.bQ, magic::Slider::Bishop, KS_SAFE_CHECK_QB);
  add_slider_moves(white ? A.wQ : A.bQ, magic::Slider::Rook, KS_SAFE_CHECK_QR);

  // bonus only if those origins are not on ‘unsafe’ squares
  // (Fast approximation: multiply result by a small factor if opp pawns cover ring)
  return sc;
}

inline Bitboard holes_for_white(Bitboard bp) {
  Bitboard cur = black_pawn_attacks(bp);
  Bitboard future = 0;
  Bitboard t = bp;
  while (t) {
    int s = lsb_i(t);
    t &= t - 1;
    int r = rank_of((Square)s);
    // ranks strictly in front of this black pawn
    Bitboard forward = 0;
    for (int rr = r - 1; rr >= 0; --rr) forward |= (RANK_1 << (8 * rr));
    Bitboard diag = magic::sliding_attacks(magic::Slider::Bishop, (Square)s, 0ULL);
    future |= (diag & forward);
  }
  return ~(cur | future);
}

inline Bitboard holes_for_black(Bitboard wp) {
  Bitboard cur = white_pawn_attacks(wp);
  Bitboard future = 0;
  Bitboard t = wp;
  while (t) {
    int s = lsb_i(t);
    t &= t - 1;
    int r = rank_of((Square)s);
    // ranks strictly in front of this white pawn
    Bitboard forward = 0;
    for (int rr = r + 1; rr < 8; ++rr) forward |= (RANK_1 << (8 * rr));
    Bitboard diag = magic::sliding_attacks(magic::Slider::Bishop, (Square)s, 0ULL);
    future |= (diag & forward);
  }
  return ~(cur | future);
}

inline int pawn_levers(Bitboard wp, Bitboard bp) {
  Bitboard wLever = white_pawn_attacks(wp) & bp;
  Bitboard bLever = black_pawn_attacks(bp) & wp;
  int centerW = popcnt(wLever & (FILE_C | FILE_D | FILE_E | FILE_F));
  int centerB = popcnt(bLever & (FILE_C | FILE_D | FILE_E | FILE_F));
  int wingW = popcnt(wLever) - centerW;
  int wingB = popcnt(bLever) - centerB;  // was subtracting wingW (bug)
  return (centerW - centerB) * PAWN_LEVER_CENTER + (wingW - wingB) * PAWN_LEVER_WING;
}

inline int xray_king_file_pressure(bool white, const std::array<Bitboard, 6>& W,
                                   const std::array<Bitboard, 6>& B, Bitboard occ, int ksq,
                                   const AttackMap* A) {
  if (ksq < 0) return 0;
  Bitboard rooks = white ? W[3] : B[3];
  int sc = 0;
  Bitboard t = rooks;
  Bitboard bbK = sq_bb((Square)ksq);

  while (t) {
    int r = lsb_i(t);
    t &= t - 1;

    // Require same rank or file to avoid cross-corner intersections
    if (file_of(r) != file_of(ksq)) continue;

    Bitboard rookRay = cached_slider_attacks(A, white, magic::Slider::Rook, r, occ);
    Bitboard kingRay = cached_slider_attacks(A, !white, magic::Slider::Rook, ksq, occ);
    Bitboard between = rookRay & kingRay & ~sq_bb((Square)r) & ~bbK;

    if (popcnt(between & occ) == 1) sc += XRAY_KFILE;
  }
  return white ? sc : -sc;
}

inline int queen_bishop_battery(bool white, const std::array<Bitboard, 6>& W,
                                const std::array<Bitboard, 6>& B, Bitboard /*occ*/, int oppK,
                                const AttackMap* /*A*/) {
  if (oppK < 0) return 0;
  Bitboard Q = white ? W[4] : B[4];
  Bitboard Bs = white ? W[2] : B[2];
  if (!Q || !Bs) return 0;

  Bitboard kDiag = magic::sliding_attacks(magic::Slider::Bishop, (Square)oppK, 0ULL);
  bool aligned = (kDiag & Q) && (kDiag & Bs);
  return (white ? +1 : -1) * (aligned ? QB_BATTERY : 0);
}

static int central_blockers(const std::array<Bitboard, 6>& W, const std::array<Bitboard, 6>& B,
                            int phase) {
  auto block = [&](bool white) {
    Bitboard occ = white ? (W[1] | W[2] | W[3] | W[4]) : (B[1] | B[2] | B[3] | B[4]);
    int e = white ? 12 /*e2*/ : 52 /*e7*/;
    int d = white ? 11 /*d2*/ : 51 /*d7*/;
    int pen = 0;
    if (occ & sq_bb((Square)e)) pen += CENTER_BLOCK_PEN;
    if (occ & sq_bb((Square)d)) pen += CENTER_BLOCK_PEN;
    return pen;
  };
  int pen = 0;
  pen -= block(true);
  pen += block(false);
  // scale by opening weight:
  return pen * std::min(phase, CENTER_BLOCK_PHASE_MAX) / CENTER_BLOCK_PHASE_DEN;
}

inline int weakly_defended(const std::array<Bitboard, 6>& W, const std::array<Bitboard, 6>& B,
                           const AttackMap& A) {
  auto score_set = [&](Bitboard pieces, Bitboard atk, Bitboard def, int val, int sign) {
    int sc = 0;
    Bitboard p = pieces;
    while (p) {
      int s = lsb_i(p);
      p &= p - 1;
      Bitboard bb = sq_bb((Square)s);
      int d = ((def & bb) != 0) - ((atk & bb) != 0);  // +1 defended only, -1 attacked only
      if (d < 0) sc += sign * val;
    }
    return sc;
  };

  Bitboard wDef = A.wAll | A.wPA | A.wKAtt;
  Bitboard bDef = A.bAll | A.bPA | A.bKAtt;
  Bitboard wAtk = A.bAll | A.bPA;
  Bitboard bAtk = A.wAll | A.wPA;

  int sc = 0;
  sc += score_set(W[1] | W[2], wAtk, wDef, WEAK_MINOR, -1);
  sc += score_set(W[3], wAtk, wDef, WEAK_ROOK, -1);
  sc += score_set(W[4], wAtk, wDef, WEAK_QUEEN, -1);

  sc += score_set(B[1] | B[2], bAtk, bDef, WEAK_MINOR, +1);
  sc += score_set(B[3], bAtk, bDef, WEAK_ROOK, +1);
  sc += score_set(B[4], bAtk, bDef, WEAK_QUEEN, +1);
  return sc;
}

// Fianchetto structure (MG only):
// - If king sits on g-file (short castle) or has long-castled onto the c- or b-file,
//   give +FIANCHETTO_OK when the "fianchetto pawn" is on rank 2/3 (white) or 7/6 (black).
// - If that pawn is missing OR on the same file but elsewhere (advanced/abandoned), give
// -FIANCHETTO_HOLE. Call: mg_add += fianchetto_structure_ksmg(W, B, wK, bK);
static int fianchetto_structure_ksmg(const std::array<Bitboard, 6>& W,
                                     const std::array<Bitboard, 6>& B, int wK, int bK) {
  auto sqbb = [](int f, int r) -> Bitboard { return sq_bb((Square)((r << 3) | f)); };

  auto score_side = [&](bool white) -> int {
    int k = white ? wK : bK;
    if (k < 0) return 0;

    const int kFile = file_of(k);
    const int kRank = rank_of(k);
    const Bitboard paw = white ? W[0] : B[0];

    // consider king "behind" a fianchetto if it's on g-file (6) for short castles or
    // on the queenside files (c- or b-file) after long castling, near the home ranks
    const bool nearHome = white ? (kRank <= 2) : (kRank >= 5);
    const bool kingSide = (kFile == 6);
    const bool queenSide = (kFile == 1) || (kFile == 2);
    if (!nearHome || (!kingSide && !queenSide)) return 0;

    const int f = kingSide ? 6 : 1;  // use b-file for long castles (king on b/c-file)
    // acceptable fianchetto pawn squares (OK) on the relevant file (g- or b-file):
    const int okR1 = white ? 1 : 6;  // g2/b2 or g7/b7
    const int okR2 = white ? 2 : 5;  // g3/b3 or g6/b6
    const Bitboard okMask = sqbb(f, okR1) | sqbb(f, okR2);

    // any pawn on that (g- or b-) file at all? (queenside uses the b-file mask)
    const Bitboard anyOnFile = paw & M.file[f];

    if (paw & okMask) {
      return FIANCHETTO_OK;
    } else if (anyOnFile) {
      // pawn exists on that file but not on OK squares (advanced/abandoned shape)
      return -FIANCHETTO_HOLE;
    } else {
      // missing g/b pawn entirely
      return -FIANCHETTO_HOLE;
    }
  };

  // White POV: white’s good shape is +, black’s weakness is +
  return score_side(true) - score_side(false);
}

// =============================================================================
// evaluate() – white POV
// =============================================================================
int Evaluator::evaluate(model::Position& pos) const {
  const Board& b = pos.getBoard();
  uint64_t key = (uint64_t)pos.hash();
  uint64_t pKey = (uint64_t)pos.getState().pawnKey;

  // prefetch caches
  prefetch_ro(&m_impl->eval[idx_eval(key)]);
  prefetch_ro(&m_impl->pawn[idx_pawn(pKey)]);

  // probe eval cache
  {
    auto& e = m_impl->eval[idx_eval(key)];
    uint64_t k = e.key.load(std::memory_order_acquire);
    if (k == key) return e.score.load(std::memory_order_relaxed);
  }

  // bitboards
  std::array<Bitboard, 6> W{}, B{};
  for (int pt = 0; pt < 6; ++pt) {
    W[pt] = b.getPieces(Color::White, (PieceType)pt);
    B[pt] = b.getPieces(Color::Black, (PieceType)pt);
  }
  Bitboard occ = b.getAllPieces();
  Bitboard wocc = b.getPieces(Color::White);
  Bitboard bocc = b.getPieces(Color::Black);

  // material, pst, phase, counts
  const auto& ac = pos.getEvalAcc();
  MaterialCounts mc{};
  mc.P[0] = ac.P[0];
  mc.P[1] = ac.P[1];
  mc.N[0] = ac.N[0];
  mc.N[1] = ac.N[1];
  mc.B[0] = ac.B[0];
  mc.B[1] = ac.B[1];
  mc.R[0] = ac.R[0];
  mc.R[1] = ac.R[1];
  mc.Q[0] = ac.Q[0];
  mc.Q[1] = ac.Q[1];

  int mg = ac.mg;
  int eg = ac.eg;
  int phase = ac.phase;
  int curPhase = clampi(phase, 0, MAX_PHASE);

  int wK = ac.kingSq[0], bK = ac.kingSq[1];

  // --- Pawn hash: pawn-only structure + cached PA / passers ---
  int pMG = 0, pEG = 0;
  Bitboard wPA = 0, bPA = 0, wPass = 0, bPass = 0;
  {
    auto& ps = m_impl->pawn[idx_pawn(pKey)];
    uint64_t k = ps.key.load(std::memory_order_acquire);
    if (k == pKey) {
      pMG = ps.mg.load(std::memory_order_relaxed);
      pEG = ps.eg.load(std::memory_order_relaxed);
      wPA = (Bitboard)ps.wPA.load(std::memory_order_relaxed);
      bPA = (Bitboard)ps.bPA.load(std::memory_order_relaxed);
      wPass = (Bitboard)ps.wPass.load(std::memory_order_relaxed);
      bPass = (Bitboard)ps.bPass.load(std::memory_order_relaxed);
    } else {
      wPA = white_pawn_attacks(W[0]);
      bPA = black_pawn_attacks(B[0]);
      PawnOnly po = pawn_structure_pawnhash_only(W[0], B[0], wPA, bPA);
      pMG = po.mg;
      pEG = po.eg;
      wPass = po.wPass;
      bPass = po.bPass;
      ps.mg.store(pMG, std::memory_order_relaxed);
      ps.eg.store(pEG, std::memory_order_relaxed);
      ps.wPA.store((uint64_t)wPA, std::memory_order_relaxed);
      ps.bPA.store((uint64_t)bPA, std::memory_order_relaxed);
      ps.wPass.store((uint64_t)wPass, std::memory_order_relaxed);
      ps.bPass.store((uint64_t)bPass, std::memory_order_relaxed);
      ps.key.store(pKey, std::memory_order_release);
    }
  }

  // Build attack map (occ-dependent) and mobility
  AttackMap A;
  A.wPA = wPA;
  A.bPA = bPA;
  A.wPass = wPass;
  A.bPass = bPass;

  AttInfo att = mobility(occ, wocc, bocc, W, B, wPA, bPA, &A);
  A.wAll = att.wAll;
  A.bAll = att.bAll;
  A.wKAtt = (wK >= 0) ? king_attacks_from((Square)wK) : 0;
  A.bKAtt = (bK >= 0) ? king_attacks_from((Square)bK) : 0;

  // threats
  int thr = threats(W, B, A, occ);

  // king safety raw + shelter
  int ksRaw = king_safety_raw(W, B, occ, A, wK, bK);
  int shelter = king_shelter_storm(W, B, wK, bK);

  // style & structure (existing)
  int bp = bishop_pair_term(W, B);
  int badB = bad_bishop(W, B);
  int outp = outposts_center(W, B, bPA, wPA);
  int rim = rim_knights(W, B);
  int ract = rook_activity(W, B, W[0], B[0], wPass, bPass, wPA, bPA, occ, wK, bK, &A);
  int spc = space_term(W, B, wPA, bPA);
  int trop = king_tropism(W, B);
  int dev = development(W, B);
  int block = piece_blocking(W, B);

  // material imbalance
  int imb = material_imbalance(mc);

  // KS mixing
  bool queensOn = (W[4] | B[4]) != 0;
  int heavyPieces = mc.Q[0] + mc.Q[1] + mc.R[0] + mc.R[1];
  int ksMulMG = queensOn ? KS_MIX_MG_Q_ON : KS_MIX_MG_Q_OFF;
  int ksMulEG =
      (heavyPieces >= KS_MIX_EG_HEAVY_THRESHOLD) ? KS_MIX_EG_IF_HEAVY : KS_MIX_EG_IF_LIGHT;
  int ksMG = ksRaw * ksMulMG / 100;
  int ksEG = ksRaw * ksMulEG / 100;
  ksMG = std::clamp(ksMG, -KS_MG_CLAMP, KS_MG_CLAMP);  // e.g.  ±400
  ksEG = std::clamp(ksEG, -KS_EG_CLAMP, KS_EG_CLAMP);  // e.g.  ±200

  int shelterMG = shelter;
  int shelterEG = shelter / SHELTER_EG_DEN;

  // ---------------------------------------------------------------------------
  // Pins
  Bitboard wPins = rook_pins(occ, wocc, (B[3] | B[4]), wK, true, &A) |
                   bishop_pins(occ, wocc, (B[2] | B[4]), wK, true, &A);
  Bitboard bPins = rook_pins(occ, bocc, (W[3] | W[4]), bK, false, &A) |
                   bishop_pins(occ, bocc, (W[2] | W[4]), bK, false, &A);

  int pinScore = 0;
  pinScore -= popcnt(wPins & (W[1] | W[2])) * PIN_MINOR + popcnt(wPins & W[3]) * PIN_ROOK +
              popcnt(wPins & W[4]) * PIN_QUEEN;
  pinScore += popcnt(bPins & (B[1] | B[2])) * PIN_MINOR + popcnt(bPins & B[3]) * PIN_ROOK +
              popcnt(bPins & B[4]) * PIN_QUEEN;

  // Safe checks
  int scW = safe_checks(true, W, B, occ, A, bK);
  int scB = safe_checks(false, W, B, occ, A, wK);
  int sc = scW - scB;

  // Holes (knight occupation + bishop attack near enemy king ring)
  Bitboard wHoles = holes_for_white(B[0]);  // usable by white
  Bitboard bHoles = holes_for_black(W[0]);  // usable by black
  const Bitboard W_ENEMY_HALF = RANK_4 | RANK_5 | RANK_6 | RANK_7;
  const Bitboard B_ENEMY_HALF = RANK_1 | RANK_2 | RANK_3 | RANK_4;

  int holeScore = 0;
  holeScore += popcnt((W[1] & wHoles) & W_ENEMY_HALF) * HOLE_OCC_KN;
  holeScore -= popcnt((B[1] & bHoles) & B_ENEMY_HALF) * HOLE_OCC_KN;

  if (bK >= 0) {
    int c = popcnt((A.wB & wHoles) & M.kingRing[bK]);
    holeScore += c * HOLE_ATT_BI;
  }
  if (wK >= 0) {
    int c = popcnt((A.bB & bHoles) & M.kingRing[wK]);
    holeScore -= c * HOLE_ATT_BI;
  }

  // Pawn levers (center/wing)
  int lever = pawn_levers(W[0], B[0]);

  // X-ray pressure along king file
  int xray = xray_king_file_pressure(true, W, B, occ, bK, &A) +
             xray_king_file_pressure(false, W, B, occ, wK, &A);

  // Queen + bishop battery toward king
  int qbatt = queen_bishop_battery(true, W, B, occ, bK, &A) +
              queen_bishop_battery(false, W, B, occ, wK, &A);

  // Central blockers (opening-weighted)
  int cblock = central_blockers(W, B, curPhase);

  // Weakly defended pieces (soft pressure)
  int weak = weakly_defended(W, B, A);

  // Fianchetto structure (MG king-safety oriented)
  int fian = fianchetto_structure_ksmg(W, B, wK, bK);
  // ---------------------------------------------------------------------------

  // Accumulate MG/EG
  int mg_add = 0, eg_add = 0;

  // passer blocker quality
  int blkq = passer_blocker_quality(W, B, W[0], B[0], occ);
  mg_add += blkq;
  eg_add += blkq / 2;

  // rook activity (EG lighter)
  mg_add += ract;
  eg_add += ract / 3;

  // space
  mg_add += spc;
  eg_add += spc / SPACE_EG_DEN;

  // outposts
  mg_add += outp;
  eg_add += outp / 2;

  // pawn-only (from TT)
  mg_add += pMG;
  eg_add += pEG;

  // mobility
  mg_add += att.mg;
  eg_add += att.eg;

  // dynamic passer adds (needs A/occ/kings)
  {
    PasserDyn pd = passer_dynamic_bonus(A, occ, wK, bK, wPass, bPass);
    mg_add += pd.mg;
    eg_add += pd.eg;
  }

  // king safety + shelter
  mg_add += ksMG + shelterMG;
  eg_add += ksEG + shelterEG;

  // threats/tropism
  mg_add += (thr * THREATS_MG_NUM) / THREATS_MG_DEN;
  eg_add += thr / THREATS_EG_DEN;

  // bishop pair + imbalance
  mg_add += bp + imb;
  eg_add += bp / 2 + imb / 2;

  // development
  mg_add += dev * std::min(curPhase, DEV_MG_PHASE_CUTOFF) / DEV_MG_PHASE_DEN;
  eg_add += dev / DEV_EG_DEN;

  // misc style
  mg_add += rim + badB + block + trop;
  eg_add += (rim / 2) + (badB / 3) + (block / 2) + (trop / 6);

  // Pins: strong in MG, still relevant in EG (reduced)
  mg_add += pinScore;
  eg_add += pinScore / 2;

  // after computing sc (safe checks), xray, qbatt
  int kingAtkMG =
      sc + (xray / 2) + qbatt;  // halve xray to reduce double-counting with rook_activity
  kingAtkMG = std::clamp(kingAtkMG, -KS_TACTICAL_MG_CLAMP, KS_TACTICAL_MG_CLAMP);  // new soft guard
  mg_add += kingAtkMG;
  eg_add += kingAtkMG / 4;

  // Holes (mostly positional MG; tiny EG)
  mg_add += holeScore;
  eg_add += holeScore / 4;

  // Pawn levers: mostly MG; a touch in EG (less)
  mg_add += lever;
  eg_add += lever / 3;

  // Central blockers: opening-weighted (already scaled), MG only
  mg_add += cblock;

  // Weakly-defended: MG only (soft)
  mg_add += weak;

  // Fianchetto structure bonus/malus: MG only
  mg_add += fian;
  // ---------------------------------------------------------------

  // EG extras
  eg_add += rook_endgame_extras_eg(W, B, W[0], B[0], occ, &A, wPass, bPass);
  eg_add += king_activity_eg(W, B);
  eg_add += passed_pawn_race_eg(W, B, pos);

  // castles & center (existing)
  castling_and_center(W, B, mg_add, eg_add);

  mg += mg_add;
  eg += eg_add;

  // scale only the EG component
  {
    const int scale = endgame_scale(W, B);  // FULL_SCALE == 64 (or whatever you use)
    eg = (eg * scale) / FULL_SCALE;
  }
  int score = taper(mg, eg, curPhase);
  // tempo (phase-aware)
  const bool wtm = (pos.getState().sideToMove == Color::White);
  const int tempo = taper(TEMPO_MG, TEMPO_EG, curPhase);
  score += (wtm ? +tempo : -tempo);

  score = clampi(score, -MATE + 1, MATE - 1);

  // store eval
  auto& e = m_impl->eval[idx_eval(key)];
  e.score.store(score, std::memory_order_relaxed);
  e.key.store(key, std::memory_order_release);
  return score;
}

}  // namespace lilia::engine
