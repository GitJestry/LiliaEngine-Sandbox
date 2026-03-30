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
#include "lilia/engine/search_position.hpp"
#include "lilia/chess/core/bitboard.hpp"
#include "lilia/chess/core/magic.hpp"

namespace lilia::engine
{

  // =============================================================================
  // Utility
  // =============================================================================

  inline int popcnt(chess::bb::Bitboard b) noexcept
  {
    return chess::bb::popcount(b);
  }
  inline int lsb_i(chess::bb::Bitboard b) noexcept
  {
    return b ? chess::bb::ctz64(b) : -1;
  }
  inline int msb_i(chess::bb::Bitboard b) noexcept
  {
    return b ? 63 - chess::bb::clz64(b) : -1;
  }
  inline int clampi(int x, int lo, int hi)
  {
    return x < lo ? lo : (x > hi ? hi : x);
  }
  inline int king_manhattan(int a, int b)
  {
    if (a < 0 || b < 0)
      return 7;
    const int af = a & 7, bf = b & 7;
    const int ar = a >> 3, br = b >> 3;
    const int df = af > bf ? af - bf : bf - af;
    const int dr = ar > br ? ar - br : br - ar;
    return df + dr;
  }

  // micro helpers
  template <typename T>
  inline void prefetch_ro(const T *p) noexcept
  {
#if defined(__GNUC__) || defined(__clang__)
    __builtin_prefetch(static_cast<const void *>(p), 0 /*read*/, 2 /*temporal*/);
#endif
  }

  // =============================================================================
  // Masks
  // =============================================================================
  struct Masks
  {
    std::array<chess::bb::Bitboard, 64> file{};
    std::array<chess::bb::Bitboard, 64> adjFiles{};
    std::array<chess::bb::Bitboard, 64> wPassed{}, bPassed{}, wFront{}, bFront{};
    std::array<chess::bb::Bitboard, 64> kingRing{};
    std::array<chess::bb::Bitboard, 64> wShield{}, bShield{};
    std::array<chess::bb::Bitboard, 64> frontSpanW{}, frontSpanB{};
  };

  consteval Masks init_masks()
  {
    Masks m{};
    for (int sq = 0; sq < 64; ++sq)
    {
      int f = chess::bb::file_of(static_cast<chess::Square>(sq));
      int r = chess::bb::rank_of(static_cast<chess::Square>(sq));

      chess::bb::Bitboard fm = 0;
      for (int rr = 0; rr < 8; ++rr)
        fm |= chess::bb::sq_bb(static_cast<chess::Square>((rr << 3) | f));
      m.file[sq] = fm;

      chess::bb::Bitboard adj = 0;
      if (f > 0)
        for (int rr = 0; rr < 8; ++rr)
          adj |= chess::bb::sq_bb(static_cast<chess::Square>((rr << 3) | (f - 1)));
      if (f < 7)
        for (int rr = 0; rr < 8; ++rr)
          adj |= chess::bb::sq_bb(static_cast<chess::Square>((rr << 3) | (f + 1)));
      m.adjFiles[sq] = adj;

      chess::bb::Bitboard pw = 0;
      for (int rr = r + 1; rr < 8; ++rr)
        for (int ff = std::max(0, f - 1); ff <= std::min(7, f + 1); ++ff)
          pw |= chess::bb::sq_bb(static_cast<chess::Square>((rr << 3) | ff));
      chess::bb::Bitboard pb = 0;
      for (int rr = r - 1; rr >= 0; --rr)
        for (int ff = std::max(0, f - 1); ff <= std::min(7, f + 1); ++ff)
          pb |= chess::bb::sq_bb(static_cast<chess::Square>((rr << 3) | ff));
      m.wPassed[sq] = pw;
      m.bPassed[sq] = pb;

      chess::bb::Bitboard wf = 0;
      for (int rr = r + 1; rr < 8; ++rr)
        wf |= chess::bb::sq_bb(static_cast<chess::Square>((rr << 3) | f));
      m.wFront[sq] = wf;
      m.frontSpanW[sq] = wf;

      chess::bb::Bitboard bf = 0;
      for (int rr = r - 1; rr >= 0; --rr)
        bf |= chess::bb::sq_bb(static_cast<chess::Square>((rr << 3) | f));
      m.bFront[sq] = bf;
      m.frontSpanB[sq] = bf;

      // King-Ring
      chess::bb::Bitboard ring = 0;
      for (int dr = -KING_RING_RADIUS; dr <= KING_RING_RADIUS; ++dr)
        for (int df = -KING_RING_RADIUS; df <= KING_RING_RADIUS; ++df)
        {
          int nr = r + dr, nf = f + df;
          if (0 <= nr && nr < 8 && 0 <= nf && nf < 8)
            ring |= chess::bb::sq_bb(static_cast<chess::Square>((nr << 3) | nf));
        }
      m.kingRing[sq] = ring;

      // Shields per color
      chess::bb::Bitboard shW = 0;
      for (int dr = 1; dr <= KING_SHIELD_DEPTH; ++dr)
      {
        int nr = r + dr;
        if (nr >= 8)
          break;
        for (int df = -1; df <= 1; ++df)
        {
          int nf = f + df;
          if (0 <= nf && nf < 8)
            shW |= chess::bb::sq_bb(static_cast<chess::Square>((nr << 3) | nf));
        }
      }
      m.wShield[sq] = shW;

      chess::bb::Bitboard shB = 0;
      for (int dr = 1; dr <= KING_SHIELD_DEPTH; ++dr)
      {
        int nr = r - dr;
        if (nr < 0)
          break;
        for (int df = -1; df <= 1; ++df)
        {
          int nf = f + df;
          if (0 <= nf && nf < 8)
            shB |= chess::bb::sq_bb(static_cast<chess::Square>((nr << 3) | nf));
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
  constexpr chess::bb::Bitboard CENTER4 =
      chess::bb::sq_bb(chess::Square(27)) | chess::bb::sq_bb(chess::Square(28)) | chess::bb::sq_bb(chess::Square(35)) | chess::bb::sq_bb(chess::Square(36));

  // Material imbalance (leicht)
  struct MaterialCounts
  {
    int P[2]{}, N[2]{}, B[2]{}, R[2]{}, Q[2]{};
  };
  static int material_imbalance(const MaterialCounts &mc)
  {
    auto s = [&](int w, int b, int kw, int kb)
    {
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
  static int space_term(const std::array<chess::bb::Bitboard, 6> &W, const std::array<chess::bb::Bitboard, 6> &B,
                        chess::bb::Bitboard wPA, chess::bb::Bitboard bPA)
  {
    chess::bb::Bitboard wocc = W[0] | W[1] | W[2] | W[3] | W[4] | W[5];
    chess::bb::Bitboard bocc = B[0] | B[1] | B[2] | B[3] | B[4] | B[5];

    // Own-half masks (relative)
    chess::bb::Bitboard wMask = chess::bb::RANK_2 | chess::bb::RANK_3 | chess::bb::RANK_4;
    chess::bb::Bitboard bMask = chess::bb::RANK_7 | chess::bb::RANK_6 | chess::bb::RANK_5;

    chess::bb::Bitboard occ = wocc | bocc;
    chess::bb::Bitboard empty = ~occ;

    int wSafe = chess::bb::popcount((wMask & empty) & ~bPA);
    int bSafe = chess::bb::popcount((bMask & empty) & ~wPA);

    int wMin = chess::bb::popcount(W[1] | W[2]), bMin = chess::bb::popcount(B[1] | B[2]);
    int wScale = SPACE_SCALE_BASE + std::min(wMin, SPACE_MINOR_SATURATION);
    int bScale = SPACE_SCALE_BASE + std::min(bMin, SPACE_MINOR_SATURATION);

    int raw = SPACE_BASE * (wSafe * wScale - bSafe * bScale);
    raw = std::clamp(raw, -SPACE_CLAMP, SPACE_CLAMP); // e.g.  ±200
    return raw;
  }

  // =============================================================================
  // Pawn structure (MG/EG SPLIT + PawnTT mg/eg)
  // =============================================================================

  struct PawnOnly
  {
    int mg = 0, eg = 0;
    chess::bb::Bitboard wPass = 0, bPass = 0;
  };

  static PawnOnly pawn_structure_pawnhash_only(chess::bb::Bitboard wp, chess::bb::Bitboard bp, chess::bb::Bitboard wPA, chess::bb::Bitboard bPA)
  {
    PawnOnly out{};
    int &mg = out.mg;
    int &eg = out.eg;

    // Isolani & doubled (file-wise)
    for (int f = 0; f < 8; ++f)
    {
      chess::bb::Bitboard F = M.file[f];
      chess::bb::Bitboard ADJ = (f > 0 ? M.file[f - 1] : 0) | (f < 7 ? M.file[f + 1] : 0);
      int wc = chess::bb::popcount(wp & F), bc = chess::bb::popcount(bp & F);
      if (wc)
      {
        if (!(wp & ADJ))
        {
          mg -= ISO_P * wc;
          eg -= (ISO_P * wc) / 2;
        }
        if (wc > 1)
        {
          mg -= DOUBLED_P * (wc - 1);
          eg -= (DOUBLED_P * (wc - 1)) / 2;
        }
      }
      if (bc)
      {
        if (!(bp & ADJ))
        {
          mg += ISO_P * bc;
          eg += (ISO_P * bc) / 2;
        }
        if (bc > 1)
        {
          mg += DOUBLED_P * (bc - 1);
          eg += (DOUBLED_P * (bc - 1)) / 2;
        }
      }
    }

    // Phalanx/Candidate (pawn-only)
    chess::bb::Bitboard t = wp;
    while (t)
    {
      int s = lsb_i(t);
      t &= t - 1;
      int f = chess::bb::file_of(s), r = chess::bb::rank_of(s);
      if (f > 0 && (wp & chess::bb::sq_bb(chess::Square(s - 1))))
      {
        mg += PHALANX;
        eg += PHALANX / 2;
      }
      if (f < 7 && (wp & chess::bb::sq_bb(chess::Square(s + 1))))
      {
        mg += PHALANX;
        eg += PHALANX / 2;
      }
      bool passed = (M.wPassed[s] & bp) == 0;
      bool candidate = !passed && ((M.wPassed[s] & bp & ~M.wFront[s]) == 0);
      if (candidate)
      {
        mg += CANDIDATE_P;
        eg += CANDIDATE_P / 2;
      }
      if (passed)
      {
        mg += PASSED_MG[r];
        eg += PASSED_EG[r];
        out.wPass |= chess::bb::sq_bb(chess::Square(s));
        int steps = 7 - r;
        if (steps <= 2)
        {
          mg += PASS_NEAR_PROMO_STEP2_MG;
          eg += PASS_NEAR_PROMO_STEP2_EG;
        }
        else if (steps == 3)
        {
          mg += PASS_NEAR_PROMO_STEP3_MG;
          eg += PASS_NEAR_PROMO_STEP3_EG;
        }
      }
    }
    t = bp;
    while (t)
    {
      int s = lsb_i(t);
      t &= t - 1;
      int f = chess::bb::file_of(s), r = chess::bb::rank_of(s);
      if (f > 0 && (bp & chess::bb::sq_bb(chess::Square(s - 1))))
      {
        mg -= PHALANX;
        eg -= PHALANX / 2;
      }
      if (f < 7 && (bp & chess::bb::sq_bb(chess::Square(s + 1))))
      {
        mg -= PHALANX;
        eg -= PHALANX / 2;
      }
      bool passed = (M.bPassed[s] & wp) == 0;
      bool candidate = !passed && ((M.bPassed[s] & wp & ~M.bFront[s]) == 0);
      if (candidate)
      {
        mg -= CANDIDATE_P;
        eg -= CANDIDATE_P / 2;
      }
      if (passed)
      {
        mg -= PASSED_MG[7 - chess::bb::rank_of(s)];
        eg -= PASSED_EG[7 - chess::bb::rank_of(s)];
        out.bPass |= chess::bb::sq_bb(chess::Square(s));
        int steps = chess::bb::rank_of(s);
        if (steps <= 2)
        {
          mg -= PASS_NEAR_PROMO_STEP2_MG;
          eg -= PASS_NEAR_PROMO_STEP2_EG;
        }
        else if (steps == 3)
        {
          mg -= PASS_NEAR_PROMO_STEP3_MG;
          eg -= PASS_NEAR_PROMO_STEP3_EG;
        }
      }
    }

    // helpers stay as you wrote them
    auto rank_ge_mask = [](int r)
    {
      chess::bb::Bitboard m = 0;
      for (int rr = r; rr < 8; ++rr)
        m |= (chess::bb::RANK_1 << (8 * rr));
      return m;
    };
    auto rank_le_mask = [](int r)
    {
      chess::bb::Bitboard m = 0;
      for (int rr = 0; rr <= r; ++rr)
        m |= (chess::bb::RANK_1 << (8 * rr));
      return m;
    };

    // --- White backward pawns ---
    {
      chess::bb::Bitboard t = wp;
      while (t)
      {
        int s = lsb_i(t);
        t &= t - 1;

        // promotion rank has no forward chess::square
        if (chess::bb::rank_of(s) == 7)
          continue;

        // not passed (exclude genuine passers)
        if ((M.wPassed[s] & bp) == 0)
          continue;

        const int r = chess::bb::rank_of(s);
        const int front = s + 8;
        chess::bb::Bitboard frontBB = chess::bb::sq_bb((chess::Square)front);

        // enemy controls the front chess::square, own pawns do not
        bool enemyControls = (bPA & frontBB) != 0;
        bool ownControls = (wPA & frontBB) != 0;
        if (!enemyControls || ownControls)
          continue;

        // any friendly pawn on adjacent files at or ahead of this rank?
        chess::bb::Bitboard supportersSame = (M.adjFiles[s] & wp & rank_le_mask(r));
        if (supportersSame)
          continue;

        // it's backward: penalize white
        mg -= BACKWARD_P;
        eg -= BACKWARD_P / 2;
      }
    }

    // --- Black backward pawns ---
    {
      chess::bb::Bitboard t = bp;
      while (t)
      {
        int s = lsb_i(t);
        t &= t - 1;

        if (chess::bb::rank_of(s) == 0)
          continue;
        if ((M.bPassed[s] & wp) == 0)
          continue;

        const int r = chess::bb::rank_of(s);
        const int front = s - 8;
        chess::bb::Bitboard frontBB = chess::bb::sq_bb((chess::Square)front);

        bool enemyControls = (wPA & frontBB) != 0;
        bool ownControls = (bPA & frontBB) != 0;
        if (!enemyControls || ownControls)
          continue;

        chess::bb::Bitboard supportersSame = (M.adjFiles[s] & bp & rank_ge_mask(r));
        if (supportersSame)
          continue;

        // it's backward: penalize black (i.e., bonus for white)
        mg += BACKWARD_P;
        eg += BACKWARD_P / 2;
      }
    }

    // connected passers (pawn-only) – prevent a/h wrap
    chess::bb::Bitboard wConn =
        (((out.wPass & ~chess::bb::FILE_H) << 1) & out.wPass) | (((out.wPass & ~chess::bb::FILE_A) >> 1) & out.wPass);
    chess::bb::Bitboard bConn =
        (((out.bPass & ~chess::bb::FILE_H) << 1) & out.bPass) | (((out.bPass & ~chess::bb::FILE_A) >> 1) & out.bPass);
    int wC = chess::bb::popcount(wConn), bC = chess::bb::popcount(bConn);
    mg += (CONNECTED_PASSERS / 2) * (wC - bC);
    eg += CONNECTED_PASSERS * (wC - bC);

    return out;
  }

  struct PasserDyn
  {
    int mg = 0, eg = 0;
  };

  // =============================================================================
  // Threats & hanging
  // =============================================================================
  struct AttackMap
  {
    chess::bb::Bitboard wAll{0}, bAll{0};
    chess::bb::Bitboard wPA{0}, bPA{0};
    chess::bb::Bitboard wKAtt{0}, bKAtt{0};
    chess::bb::Bitboard wPass{0}, bPass{0};

    // NEU: per-Typ Angriffe (occ-abhängig)
    chess::bb::Bitboard wN{0}, wB{0}, wR{0}, wQ{0};
    chess::bb::Bitboard bN{0}, bB{0}, bR{0}, bQ{0};

    // Cached slider rays (per piece chess::square)
    chess::bb::Bitboard wBPos{0}, wRPos{0}, wQPos{0};
    chess::bb::Bitboard bBPos{0}, bRPos{0}, bQPos{0};
    std::array<chess::bb::Bitboard, 64> wBishopRays{};
    std::array<chess::bb::Bitboard, 64> bBishopRays{};
    std::array<chess::bb::Bitboard, 64> wRookRays{};
    std::array<chess::bb::Bitboard, 64> bRookRays{};
    std::array<chess::bb::Bitboard, 64> wQueenBishopRays{};
    std::array<chess::bb::Bitboard, 64> bQueenBishopRays{};
    std::array<chess::bb::Bitboard, 64> wQueenRookRays{};
    std::array<chess::bb::Bitboard, 64> bQueenRookRays{};
  };

  inline chess::bb::Bitboard cached_slider_attacks(const AttackMap *A, bool white, chess::magic::Slider s, int sq,
                                                   chess::bb::Bitboard occ)
  {
    if (!A || sq < 0)
      return chess::magic::sliding_attacks(s, static_cast<chess::Square>(sq), occ);
    chess::bb::Bitboard mask = chess::bb::sq_bb(static_cast<chess::Square>(sq));
    if (s == chess::magic::Slider::Bishop)
    {
      if (white)
      {
        if (A->wBPos & mask)
          return A->wBishopRays[sq];
        if (A->wQPos & mask)
          return A->wQueenBishopRays[sq];
      }
      else
      {
        if (A->bBPos & mask)
          return A->bBishopRays[sq];
        if (A->bQPos & mask)
          return A->bQueenBishopRays[sq];
      }
    }
    else
    {
      if (white)
      {
        if (A->wRPos & mask)
          return A->wRookRays[sq];
        if (A->wQPos & mask)
          return A->wQueenRookRays[sq];
      }
      else
      {
        if (A->bRPos & mask)
          return A->bRookRays[sq];
        if (A->bQPos & mask)
          return A->bQueenRookRays[sq];
      }
    }
    return chess::magic::sliding_attacks(s, static_cast<chess::Square>(sq), occ);
  }

  static PasserDyn passer_dynamic_bonus(const AttackMap &A, chess::bb::Bitboard occ, int wK, int bK,
                                        chess::bb::Bitboard wPass, chess::bb::Bitboard bPass)
  {
    PasserDyn d{};

    auto add_side = [&](bool white)
    {
      chess::bb::Bitboard pass = white ? wPass : bPass;
      int K = white ? wK : bK;
      chess::bb::Bitboard ownNBRQ = white ? (A.wN | A.wB | A.wR | A.wQ) : (A.bN | A.bB | A.bR | A.bQ);
      chess::bb::Bitboard oppKBB = white ? chess::bb::sq_bb(chess::Square(bK)) : chess::bb::sq_bb(chess::Square(wK));
      while (pass)
      {
        int s = lsb_i(pass);
        pass &= pass - 1;
        int stop = white ? s + 8 : s - 8;
        // block on stop chess::square
        int mgB = 0, egB = 0;
        if (stop >= 0 && stop < 64 && (occ & chess::bb::sq_bb(chess::Square(stop))))
        {
          mgB -= PASS_BLOCK;
          egB -= PASS_BLOCK;
        }
        // free path ahead
        if (((white ? M.wFront[s] : M.bFront[s]) & occ) == 0)
        {
          mgB += PASS_FREE;
          egB += PASS_FREE;
        }
        // piece support (use A, O(1))
        if (ownNBRQ & chess::bb::sq_bb(chess::Square(s)))
        {
          mgB += PASS_PIECE_SUPP;
          egB += PASS_PIECE_SUPP;
        }
        // king boost / king block
        if (K >= 0 && king_manhattan(K, s) <= 3)
        {
          mgB += PASS_KBOOST;
          egB += PASS_KBOOST;
        }
        if (oppKBB & ((white ? (M.wFront[s] | (stop < 64 ? chess::bb::sq_bb(chess::Square(stop)) : 0ULL))
                             : (M.bFront[s] | (stop >= 0 ? chess::bb::sq_bb(chess::Square(stop)) : 0ULL)))))
        {
          mgB -= PASS_KBLOCK;
          egB -= PASS_KBLOCK;
        }
        int oppK = white ? bK : wK;
        if (oppK >= 0)
        {
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
  struct AttInfo
  {
    chess::bb::Bitboard wAll = 0, bAll = 0;
    int mg = 0, eg = 0;
  };
  static AttInfo mobility(chess::bb::Bitboard occ, chess::bb::Bitboard wocc, chess::bb::Bitboard bocc,
                          const std::array<chess::bb::Bitboard, 6> &W, const std::array<chess::bb::Bitboard, 6> &B,
                          chess::bb::Bitboard wPA, chess::bb::Bitboard bPA, AttackMap *A /* optional */)
  {
    AttInfo ai{};

    const chess::bb::Bitboard safeMaskW = ~wocc & ~bPA;
    const chess::bb::Bitboard safeMaskB = ~bocc & ~wPA;

    // --- Knights ---
    {
      chess::bb::Bitboard bb = W[(int)chess::PieceType::Knight];
      while (bb)
      {
        const int s = lsb_i(bb);
        bb &= bb - 1;
        const chess::bb::Bitboard a = chess::bb::knight_attacks_from((chess::Square)s);
        ai.wAll |= a;
        if (A)
          A->wN |= a;
        int c = popcnt(a & safeMaskW);
        if (c > 8)
          c = 8;
        ai.mg += KN_MOB_MG[c];
        ai.eg += KN_MOB_EG[c];
      }
    }
    {
      chess::bb::Bitboard bb = B[(int)chess::PieceType::Knight];
      while (bb)
      {
        const int s = lsb_i(bb);
        bb &= bb - 1;
        const chess::bb::Bitboard a = chess::bb::knight_attacks_from((chess::Square)s);
        ai.bAll |= a;
        if (A)
          A->bN |= a;
        int c = popcnt(a & safeMaskB);
        if (c > 8)
          c = 8;
        ai.mg -= KN_MOB_MG[c];
        ai.eg -= KN_MOB_EG[c];
      }
    }

    // --- Bishops ---
    {
      chess::bb::Bitboard bb = W[(int)chess::PieceType::Bishop];
      while (bb)
      {
        const int s = lsb_i(bb);
        bb &= bb - 1;
        const chess::bb::Bitboard a = chess::magic::sliding_attacks(chess::magic::Slider::Bishop, (chess::Square)s, occ);
        ai.wAll |= a;
        if (A)
        {
          chess::bb::Bitboard sq = chess::bb::sq_bb(static_cast<chess::Square>(s));
          A->wB |= a;
          A->wBPos |= sq;
          A->wBishopRays[s] = a;
        }
        int c = popcnt(a & safeMaskW);
        if (c > 13)
          c = 13;
        ai.mg += BI_MOB_MG[c];
        ai.eg += BI_MOB_EG[c];
      }
    }
    {
      chess::bb::Bitboard bb = B[(int)chess::PieceType::Bishop];
      while (bb)
      {
        const int s = lsb_i(bb);
        bb &= bb - 1;
        const chess::bb::Bitboard a = chess::magic::sliding_attacks(chess::magic::Slider::Bishop, (chess::Square)s, occ);
        ai.bAll |= a;
        if (A)
        {
          chess::bb::Bitboard sq = chess::bb::sq_bb(static_cast<chess::Square>(s));
          A->bB |= a;
          A->bBPos |= sq;
          A->bBishopRays[s] = a;
        }
        int c = popcnt(a & safeMaskB);
        if (c > 13)
          c = 13;
        ai.mg -= BI_MOB_MG[c];
        ai.eg -= BI_MOB_EG[c];
      }
    }

    // --- Rooks ---
    {
      chess::bb::Bitboard bb = W[(int)chess::PieceType::Rook];
      while (bb)
      {
        const int s = lsb_i(bb);
        bb &= bb - 1;
        const chess::bb::Bitboard a = chess::magic::sliding_attacks(chess::magic::Slider::Rook, (chess::Square)s, occ);
        ai.wAll |= a;
        if (A)
        {
          chess::bb::Bitboard sq = chess::bb::sq_bb(static_cast<chess::Square>(s));
          A->wR |= a;
          A->wRPos |= sq;
          A->wRookRays[s] = a;
        }
        int c = popcnt(a & safeMaskW);
        if (c > 14)
          c = 14;
        ai.mg += RO_MOB_MG[c];
        ai.eg += RO_MOB_EG[c];
      }
    }
    {
      chess::bb::Bitboard bb = B[(int)chess::PieceType::Rook];
      while (bb)
      {
        const int s = lsb_i(bb);
        bb &= bb - 1;
        const chess::bb::Bitboard a = chess::magic::sliding_attacks(chess::magic::Slider::Rook, (chess::Square)s, occ);
        ai.bAll |= a;
        if (A)
        {
          chess::bb::Bitboard sq = chess::bb::sq_bb(static_cast<chess::Square>(s));
          A->bR |= a;
          A->bRPos |= sq;
          A->bRookRays[s] = a;
        }
        int c = popcnt(a & safeMaskB);
        if (c > 14)
          c = 14;
        ai.mg -= RO_MOB_MG[c];
        ai.eg -= RO_MOB_EG[c];
      }
    }

    // --- Queens ---
    {
      chess::bb::Bitboard bb = W[(int)chess::PieceType::Queen];
      while (bb)
      {
        const int s = lsb_i(bb);
        bb &= bb - 1;
        const chess::bb::Bitboard r = chess::magic::sliding_attacks(chess::magic::Slider::Rook, (chess::Square)s, occ);
        const chess::bb::Bitboard b = chess::magic::sliding_attacks(chess::magic::Slider::Bishop, (chess::Square)s, occ);
        const chess::bb::Bitboard a = r | b;
        ai.wAll |= a;
        if (A)
        {
          chess::bb::Bitboard sq = chess::bb::sq_bb(static_cast<chess::Square>(s));
          A->wQ |= a;
          A->wQPos |= sq;
          A->wQueenRookRays[s] = r;
          A->wQueenBishopRays[s] = b;
        }
        int c = popcnt(a & safeMaskW);
        if (c > 27)
          c = 27;
        ai.mg += QU_MOB_MG[c];
        ai.eg += QU_MOB_EG[c];
      }
    }
    {
      chess::bb::Bitboard bb = B[(int)chess::PieceType::Queen];
      while (bb)
      {
        const int s = lsb_i(bb);
        bb &= bb - 1;
        const chess::bb::Bitboard r = chess::magic::sliding_attacks(chess::magic::Slider::Rook, (chess::Square)s, occ);
        const chess::bb::Bitboard b = chess::magic::sliding_attacks(chess::magic::Slider::Bishop, (chess::Square)s, occ);
        const chess::bb::Bitboard a = r | b;
        ai.bAll |= a;
        if (A)
        {
          chess::bb::Bitboard sq = chess::bb::sq_bb(static_cast<chess::Square>(s));
          A->bQ |= a;
          A->bQPos |= sq;
          A->bQueenRookRays[s] = r;
          A->bQueenBishopRays[s] = b;
        }
        int c = popcnt(a & safeMaskB);
        if (c > 27)
          c = 27;
        ai.mg -= QU_MOB_MG[c];
        ai.eg -= QU_MOB_EG[c];
      }
    }

    // final clamp
    if (ai.mg > MOBILITY_CLAMP)
      ai.mg = MOBILITY_CLAMP;
    if (ai.mg < -MOBILITY_CLAMP)
      ai.mg = -MOBILITY_CLAMP;
    if (ai.eg > MOBILITY_CLAMP)
      ai.eg = MOBILITY_CLAMP;
    if (ai.eg < -MOBILITY_CLAMP)
      ai.eg = -MOBILITY_CLAMP;

    return ai;
  }

  static int threats(const std::array<chess::bb::Bitboard, 6> &W, const std::array<chess::bb::Bitboard, 6> &B,
                     const AttackMap &A, chess::bb::Bitboard occ)
  {
    int sc = 0;

    auto pawn_threat_score = [&](chess::bb::Bitboard pa, const std::array<chess::bb::Bitboard, 6> &side)
    {
      int s = 0;
      if (pa & side[1])
        s += THR_PAWN_MINOR;
      if (pa & side[2])
        s += THR_PAWN_MINOR;
      if (pa & side[3])
        s += THR_PAWN_ROOK;
      if (pa & side[4])
        s += THR_PAWN_QUEEN;
      return s;
    };
    sc += pawn_threat_score(A.wPA, B);
    sc -= pawn_threat_score(A.bPA, W);

    int wKsq = lsb_i(W[5]), bKsq = lsb_i(B[5]);
    chess::bb::Bitboard wocc = W[0] | W[1] | W[2] | W[3] | W[4] | W[5];
    chess::bb::Bitboard bocc = B[0] | B[1] | B[2] | B[3] | B[4] | B[5];

    chess::bb::Bitboard wDef = A.wAll | A.wPA | (wKsq >= 0 ? chess::bb::king_attacks_from((chess::Square)wKsq) : 0);
    chess::bb::Bitboard bDef = A.bAll | A.bPA | (bKsq >= 0 ? chess::bb::king_attacks_from((chess::Square)bKsq) : 0);

    chess::bb::Bitboard wHang = ((A.bAll | A.bPA) & wocc) & ~wDef;
    chess::bb::Bitboard bHang = ((A.wAll | A.wPA) & bocc) & ~bDef;

    auto hang_score = [&](chess::bb::Bitboard h, const std::array<chess::bb::Bitboard, 6> &side)
    {
      int s = 0;
      if (h & side[1])
        s += HANG_MINOR;
      if (h & side[2])
        s += HANG_MINOR;
      if (h & side[3])
        s += HANG_ROOK;
      if (h & side[4])
        s += HANG_QUEEN;
      return s;
    };
    sc += hang_score(bHang, B);
    sc -= hang_score(wHang, W);

    if ((A.wN | A.wB) & B[4])
      sc += MINOR_ON_QUEEN;
    if ((A.bN | A.bB) & W[4])
      sc -= MINOR_ON_QUEEN;

    auto queen_pawn_chase_penalty = [&](bool whiteSide)
    {
      chess::bb::Bitboard queens = whiteSide ? W[4] : B[4];
      if (!queens)
        return 0;

      chess::bb::Bitboard enemyPawns = whiteSide ? B[0] : W[0];
      if (!enemyPawns)
        return 0;

      int penalty = 0;
      const auto pawn_attacks = whiteSide ? chess::bb::black_pawn_attacks : chess::bb::white_pawn_attacks;
      const auto pawn_push_one = whiteSide ? chess::bb::south : chess::bb::north;
      const chess::bb::Bitboard startRank = whiteSide ? chess::bb::RANK_7 : chess::bb::RANK_2;

      chess::bb::Bitboard direct = pawn_attacks(enemyPawns);

      while (queens)
      {
        int sq = lsb_i(queens);
        queens &= queens - 1;
        chess::bb::Bitboard target = chess::bb::sq_bb(static_cast<chess::Square>(sq));

        if (direct & target)
        {
          penalty += QUEEN_PAWN_CHASE_IMMEDIATE;
          continue;
        }

        chess::bb::Bitboard pushOne = pawn_push_one(enemyPawns) & ~occ;
        if (pawn_attacks(pushOne) & target)
        {
          penalty += QUEEN_PAWN_CHASE_SINGLE;
          continue;
        }

        chess::bb::Bitboard startPawns = enemyPawns & startRank;
        chess::bb::Bitboard mid = pawn_push_one(startPawns) & ~occ;
        chess::bb::Bitboard pushTwo = pawn_push_one(mid) & ~occ;
        if (pawn_attacks(pushTwo) & target)
        {
          penalty += QUEEN_PAWN_CHASE_DOUBLE;
        }
      }

      return penalty;
    };

    sc -= queen_pawn_chase_penalty(true);
    sc += queen_pawn_chase_penalty(false);

    return sc;
  }

  static int king_safety_raw(const std::array<chess::bb::Bitboard, 6> &W, const std::array<chess::bb::Bitboard, 6> &B,
                             chess::bb::Bitboard /*occ*/, const AttackMap &A, int wK, int bK)
  {
    auto ring_attacks_fast = [&](int ksq, bool kingIsWhite) -> int
    {
      if (ksq < 0)
        return 0;
      chess::bb::Bitboard ring = M.kingRing[ksq];

      int cN = chess::bb::popcount((kingIsWhite ? A.bN : A.wN) & ring);
      int cB = chess::bb::popcount((kingIsWhite ? A.bB : A.wB) & ring);
      int cR = chess::bb::popcount((kingIsWhite ? A.bR : A.wR) & ring);
      int cQ = chess::bb::popcount((kingIsWhite ? A.bQ : A.wQ) & ring);

      int unique =
          chess::bb::popcount((kingIsWhite ? A.bN | A.bB | A.bR | A.bQ : A.wN | A.wB | A.wR | A.wQ) & ring);

      int power = cN * (KS_W_N - 2) + cB * (KS_W_B - 2) + cR * KS_W_R + cQ * (KS_W_Q - 4);
      int score = unique * KS_RING_BONUS +
                  (power * std::min(unique, KS_POWER_COUNT_CLAMP)) / KS_POWER_COUNT_CLAMP;

      // Shield / open file / Escape
      chess::bb::Bitboard wp = W[0], bp = B[0];
      chess::bb::Bitboard shield = kingIsWhite ? M.wShield[ksq] : M.bShield[ksq];
      chess::bb::Bitboard ownP = kingIsWhite ? wp : bp;
      int missing = 6 - std::min(6, chess::bb::popcount(ownP & shield));
      score += missing * KS_MISS_SHIELD;

      chess::bb::Bitboard file = M.file[ksq];
      chess::bb::Bitboard oppP = kingIsWhite ? bp : wp;
      bool ownOn = file & ownP, oppOn = file & oppP;
      if (!ownOn && !oppOn)
        score += KS_OPEN_FILE;
      else if (!ownOn && oppOn)
        score += KS_OPEN_FILE / 2;

      chess::bb::Bitboard kAtt = chess::bb::king_attacks_from((chess::Square)ksq);
      chess::bb::Bitboard oppAll = kingIsWhite ? (A.bAll | A.bPA | A.bKAtt) : (A.wAll | A.wPA | A.wKAtt);

      int esc = chess::bb::popcount(
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

  static int king_shelter_storm(const std::array<chess::bb::Bitboard, 6> &W, const std::array<chess::bb::Bitboard, 6> &B,
                                int wK, int bK)
  {
    if (wK < 0 || bK < 0)
      return 0;
    chess::bb::Bitboard wp = W[0], bp = B[0];

    auto fileShelter = [&](int ksq, bool white)
    {
      const int kFile = chess::bb::file_of(ksq);
      const int kRank = chess::bb::rank_of(ksq);
      const chess::bb::Bitboard ownPawns = white ? wp : bp;
      const chess::bb::Bitboard enemyPawns = white ? bp : wp;
      int total = 0;

      for (int df = -1; df <= 1; ++df)
      {
        int ff = kFile + df;
        if (ff < 0 || ff > 7)
          continue;

        const int baseSq = (kRank << 3) | ff;
        const chess::bb::Bitboard mask = white ? M.frontSpanW[baseSq] : M.frontSpanB[baseSq];

        if (white)
        {
          int nearOwnSq = lsb_i(mask & ownPawns);
          int nearOwnR = (nearOwnSq >= 0 ? (nearOwnSq >> 3) : 8);
          int dist = clampi(nearOwnR - kRank, 0, 7);
          int shelterIdx = 7 - dist; // closer own pawn => bigger shelter
          total += SHELTER[shelterIdx];

          int nearEnemySq = lsb_i(mask & enemyPawns);
          int nearEnemyR = (nearEnemySq >= 0 ? (nearEnemySq >> 3) : 8);
          int edist = clampi(nearEnemyR - kRank, 0, 7);
          int stormIdx = 7 - edist; // closer enemy pawn => bigger storm
          total -= STORM[stormIdx] / 2;
        }
        else
        {
          int nearOwnSq = msb_i(mask & ownPawns);
          int nearOwnR = (nearOwnSq >= 0 ? (nearOwnSq >> 3) : -1);
          int dist = clampi(kRank - nearOwnR, 0, 7);
          int shelterIdx = 7 - dist; // closer own pawn => bigger shelter
          total += SHELTER[shelterIdx];

          int nearEnemySq = msb_i(mask & enemyPawns);
          int nearEnemyR = (nearEnemySq >= 0 ? (nearEnemySq >> 3) : -1);
          int edist = clampi(kRank - nearEnemyR, 0, 7);
          int stormIdx = 7 - edist; // closer enemy pawn => bigger storm
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

  inline bool pawns_on_both_wings(chess::bb::Bitboard pawns) noexcept
  {
    constexpr chess::bb::Bitboard LEFT = chess::bb::FILE_A | chess::bb::FILE_B | chess::bb::FILE_C | chess::bb::FILE_D;
    constexpr chess::bb::Bitboard RIGHT = chess::bb::FILE_E | chess::bb::FILE_F | chess::bb::FILE_G | chess::bb::FILE_H;
    return (pawns & LEFT) && (pawns & RIGHT);
  }

  static int bishop_pair_term(const std::array<chess::bb::Bitboard, 6> &W, const std::array<chess::bb::Bitboard, 6> &B)
  {
    int s = 0;
    if (popcnt(W[2]) >= 2)
      s += BISHOP_PAIR + (pawns_on_both_wings(W[0]) ? 6 : 0);
    if (popcnt(B[2]) >= 2)
      s -= BISHOP_PAIR + (pawns_on_both_wings(B[0]) ? 6 : 0);
    return s;
  }

  static int bad_bishop(const std::array<chess::bb::Bitboard, 6> &W, const std::array<chess::bb::Bitboard, 6> &B)
  {
    auto is_light = [&](int sq)
    { return ((chess::bb::file_of(sq) + chess::bb::rank_of(sq)) & 1) != 0; };
    int sc = 0;
    auto apply = [&](const std::array<chess::bb::Bitboard, 6> &bb, int sign)
    {
      chess::bb::Bitboard paw = bb[0];
      bool closedCenter = ((paw & M.file[3]) && (paw & M.file[4])); // d & e File

      int light = 0, dark = 0;
      chess::bb::Bitboard t = paw;
      while (t)
      {
        int s = lsb_i(t);
        t &= t - 1;
        (is_light(s) ? ++light : ++dark);
      }
      chess::bb::Bitboard bishops = bb[2];
      while (bishops)
      {
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

  static int outposts_center(const std::array<chess::bb::Bitboard, 6> &W, const std::array<chess::bb::Bitboard, 6> &B,
                             chess::bb::Bitboard bPA, chess::bb::Bitboard wPA)
  {
    int s = 0;
    chess::bb::Bitboard wSup = chess::bb::white_pawn_attacks(W[0]);
    chess::bb::Bitboard bSup = chess::bb::black_pawn_attacks(B[0]);

    auto add_kn = [&](int sq, bool white)
    {
      bool notAttackedByEnemyPawn = white ? !(bPA & chess::bb::sq_bb((chess::Square)sq)) : !(wPA & chess::bb::sq_bb((chess::Square)sq));
      bool supportedByOwnPawn = white ? (wSup & chess::bb::sq_bb((chess::Square)sq)) : (bSup & chess::bb::sq_bb((chess::Square)sq));
      int r = chess::bb::rank_of(sq);
      bool deepOutpost = white ? (r >= OUTPOST_DEEP_RANK_WHITE) : (r <= OUTPOST_DEEP_RANK_BLACK);

      int add = 0;
      if (notAttackedByEnemyPawn && supportedByOwnPawn && deepOutpost)
        add += OUTPOST_KN + OUTPOST_DEEP_EXTRA;
      if (chess::bb::knight_attacks_from((chess::Square)sq) & CENTER4)
        add += CENTER_CTRL;
      if (chess::bb::sq_bb((chess::Square)sq) & CENTER4)
        add += OUTPOST_CENTER_SQ_BONUS;
      return add;
    };

    chess::bb::Bitboard t = W[1];
    while (t)
    {
      int sq = lsb_i(t);
      t &= t - 1;
      s += add_kn(sq, true);
    }
    t = B[1];
    while (t)
    {
      int sq = lsb_i(t);
      t &= t - 1;
      s -= add_kn(sq, false);
    }
    return s;
  }

  static int rim_knights(const std::array<chess::bb::Bitboard, 6> &W, const std::array<chess::bb::Bitboard, 6> &B)
  {
    chess::bb::Bitboard aF = M.file[0], hF = M.file[7];
    int s = 0;
    s -= popcnt(W[1] & (aF | hF)) * KNIGHT_RIM;
    s += popcnt(B[1] & (aF | hF)) * KNIGHT_RIM;
    return s;
  }

  static int rook_activity(const std::array<chess::bb::Bitboard, 6> &W, const std::array<chess::bb::Bitboard, 6> &B,
                           chess::bb::Bitboard wp, chess::bb::Bitboard bp, chess::bb::Bitboard wPass, chess::bb::Bitboard bPass, chess::bb::Bitboard wPA,
                           chess::bb::Bitboard bPA, chess::bb::Bitboard occ, int wK, int bK, const AttackMap *A)
  {
    int s = 0;
    chess::bb::Bitboard wr = W[3], br = B[3];
    if (!wr && !br)
      return 0;
    auto rank = [&](int sq)
    { return chess::bb::rank_of(sq); };
    auto openScore = [&](int sq, bool white)
    {
      chess::bb::Bitboard f = M.file[sq];
      bool own = white ? (f & wp) : (f & bp);
      bool opp = white ? (f & bp) : (f & wp);
      if (!own && !opp)
        return ROOK_OPEN;
      if (!own && opp)
        return ROOK_SEMI;
      return 0;
    };
    // base activity and 7th rank
    chess::bb::Bitboard t = wr;
    while (t)
    {
      int sq = lsb_i(t);
      t &= t - 1;
      s += openScore(sq, true);
      if (rank(sq) == 6)
      {
        bool tgt = (B[5] & chess::bb::RANK_8) || (B[0] & chess::bb::RANK_7);
        if (tgt)
          s += ROOK_ON_7TH;
      }
    }
    t = br;
    while (t)
    {
      int sq = lsb_i(t);
      t &= t - 1;
      s -= openScore(sq, false);
      if (rank(sq) == 1)
      {
        bool tgt = (W[5] & chess::bb::RANK_1) || (W[0] & chess::bb::RANK_2);
        if (tgt)
          s -= ROOK_ON_7TH;
      }
    }

    // connected rooks
    auto connected = [&](chess::bb::Bitboard rooks, chess::bb::Bitboard occAll, bool /*whiteSide*/)
    {
      if (popcnt(rooks) != 2)
        return false;
      int s1 = lsb_i(rooks);
      chess::bb::Bitboard r2 = rooks & (rooks - 1);
      int s2 = lsb_i(r2);
      chess::bb::Bitboard occ2 = occAll & ~chess::bb::sq_bb((chess::Square)s2);
      chess::bb::Bitboard ray = chess::magic::sliding_attacks(chess::magic::Slider::Rook, (chess::Square)s1, occ2);
      return (ray & chess::bb::sq_bb((chess::Square)s2)) != 0;
    };
    chess::bb::Bitboard occAll = occ;
    if (connected(wr, occAll, true))
      s += CONNECTED_ROOKS;
    if (connected(br, occAll, false))
      s -= CONNECTED_ROOKS;

    // rook behind passers (uses wPass/bPass)
    auto behind = [&](int rSq, int pSq, bool rookWhite, bool pawnWhite, int full, int half)
    {
      if (chess::bb::file_of(rSq) != chess::bb::file_of(pSq))
        return 0;
      chess::bb::Bitboard ray = cached_slider_attacks(A, rookWhite, chess::magic::Slider::Rook, rSq, occAll);
      if (!(ray & chess::bb::sq_bb((chess::Square)pSq)))
        return 0;
      if (pawnWhite)
        return (chess::bb::rank_of(rSq) < chess::bb::rank_of(pSq) ? full : half);
      else
        return (chess::bb::rank_of(rSq) > chess::bb::rank_of(pSq) ? full : half);
    };
    t = wr;
    while (t)
    {
      int rs = lsb_i(t);
      t &= t - 1;
      chess::bb::Bitboard f = M.file[rs] & wPass;
      while (f)
      {
        int ps = lsb_i(f);
        f &= f - 1;
        s += behind(rs, ps, true, true, ROOK_BEHIND_PASSER, ROOK_BEHIND_PASSER_HALF);
      }
      f = M.file[rs] & bPass;
      while (f)
      {
        int ps = lsb_i(f);
        f &= f - 1;
        s += behind(rs, ps, true, false, ROOK_BEHIND_PASSER_HALF, ROOK_BEHIND_PASSER_THIRD);
      }
    }
    t = br;
    while (t)
    {
      int rs = lsb_i(t);
      t &= t - 1;
      chess::bb::Bitboard f = M.file[rs] & bPass;
      while (f)
      {
        int ps = lsb_i(f);
        f &= f - 1;
        s -= behind(rs, ps, false, false, ROOK_BEHIND_PASSER, ROOK_BEHIND_PASSER_HALF);
      }
      f = M.file[rs] & wPass;
      while (f)
      {
        int ps = lsb_i(f);
        f &= f - 1;
        s -= behind(rs, ps, false, true, ROOK_BEHIND_PASSER_HALF, ROOK_BEHIND_PASSER_THIRD);
      }
    }

    chess::bb::Bitboard centralFiles = M.file[2] | M.file[3] | M.file[4] | M.file[5];
    auto centra_file_bonus = [&](chess::bb::Bitboard rooks, bool white)
    {
      int sc = 0;
      chess::bb::Bitboard tt = rooks;
      while (tt)
      {
        int sq = lsb_i(tt);
        tt &= tt - 1;
        chess::bb::Bitboard attacks = cached_slider_attacks(A, white, chess::magic::Slider::Rook, sq, occ);
        if (centralFiles & (chess::bb::sq_bb((chess::Square)sq) | attacks))
          sc += ROOK_CENTRAL_FILE;
      }
      return white ? sc : -sc;
    };
    s += centra_file_bonus(wr, true);
    s += centra_file_bonus(br, false);

    if (wK >= 0)
    {
      chess::bb::Bitboard f = M.file[wK];
      bool own = f & wp;
      bool opp = f & bp;
      if (!own && opp)
        s += ROOK_SEMI_ON_KING_FILE;
      if (!own && !opp)
        s += ROOK_OPEN_ON_KING_FILE;
    }
    if (bK >= 0)
    {
      chess::bb::Bitboard f = M.file[bK];
      bool own = f & bp;
      bool opp = f & wp;
      if (!own && opp)
        s -= ROOK_SEMI_ON_KING_FILE;
      if (!own && !opp)
        s -= ROOK_OPEN_ON_KING_FILE;
    }

    auto king_file_pressure = [&](bool white)
    {
      int ksq = white ? lsb_i(B[5]) : lsb_i(W[5]);
      if (ksq < 0)
        return 0;
      chess::bb::Bitboard rooks = white ? W[3] : B[3];
      chess::bb::Bitboard oppPA = white ? bPA : wPA;

      int total = 0;
      chess::bb::Bitboard t = rooks;
      while (t)
      {
        int rsq = lsb_i(t);
        t &= t - 1;

        // rook ray and the king’s file
        chess::bb::Bitboard ray = cached_slider_attacks(A, white, chess::magic::Slider::Rook, rsq, occ);
        chess::bb::Bitboard fileToKing = ray & M.file[ksq];
        if (!(fileToKing & chess::bb::sq_bb((chess::Square)ksq)))
          continue; // not pointing at king

        // chess::squares strictly between rook and king:
        chess::bb::Bitboard between =
            fileToKing &
            cached_slider_attacks(A, !white, chess::magic::Slider::Rook, ksq, occ | chess::bb::sq_bb((chess::Square)rsq)) &
            ~chess::bb::sq_bb((chess::Square)rsq) & ~chess::bb::sq_bb((chess::Square)ksq);

        int len = popcnt(between);
        int blockedByPawnAtt = popcnt(between & oppPA);
        int free = len - blockedByPawnAtt;

        // Tune these (small numbers):
        total += ROOK_KFILE_PRESS_FREE * free;
        total -= ROOK_KFILE_PRESS_PAWNATT * blockedByPawnAtt;
      }
      return white ? total : -total;
    };

    auto rook_lift_safety = [&](bool white)
    {
      chess::bb::Bitboard rooks = white ? W[3] : B[3];
      chess::bb::Bitboard oppPA = white ? bPA : wPA;
      int targetRank = white ? 2 : 5; // zero-based: rank 3 / rank 6
      int sc = 0;
      chess::bb::Bitboard t = rooks;
      while (t)
      {
        int rsq = lsb_i(t);
        t &= t - 1;
        if (chess::bb::rank_of(rsq) != targetRank)
          continue;
        if ((chess::bb::sq_bb((chess::Square)rsq) & oppPA) == 0)
          sc += ROOK_LIFT_SAFE; // small bump
      }
      return white ? sc : -sc;
    };

    s += rook_lift_safety(true);
    s += rook_lift_safety(false);

    s += king_file_pressure(true);
    s += king_file_pressure(false);

    return s;
  }

  static int rook_endgame_extras_eg(const std::array<chess::bb::Bitboard, 6> &W,
                                    const std::array<chess::bb::Bitboard, 6> &B, chess::bb::Bitboard wp, chess::bb::Bitboard bp,
                                    chess::bb::Bitboard occ, const AttackMap *A, chess::bb::Bitboard wPass_cached,
                                    chess::bb::Bitboard bPass_cached)
  {
    int eg = 0;
    chess::bb::Bitboard wr = W[3], br = B[3];

    auto add_progress = [&](bool white)
    {
      chess::bb::Bitboard rooks = white ? wr : br;
      chess::bb::Bitboard pass = white ? wPass_cached : bPass_cached;
      chess::bb::Bitboard t = rooks;
      while (t)
      {
        int rs = lsb_i(t);
        t &= t - 1;
        chess::bb::Bitboard f = M.file[rs] & pass;
        while (f)
        {
          int ps = lsb_i(f);
          f &= f - 1;
          bool beh = (cached_slider_attacks(A, white, chess::magic::Slider::Rook, rs, occ) &
                      chess::bb::sq_bb((chess::Square)ps)) != 0;
          if (!beh)
            continue;
          auto progress_from_home = [&](int ps, bool w)
          {
            return w ? chess::bb::rank_of(ps) : (7 - chess::bb::rank_of(ps));
          };
          int advance = std::max(0, progress_from_home(ps, white) - ROOK_PASSER_PROGRESS_START_RANK);
          eg += (white ? +1 : -1) * (advance * ROOK_PASSER_PROGRESS_MULT);
        }
      }
    };
    add_progress(true);
    add_progress(false);

    auto cut_score = [&](bool white)
    {
      bool rookEnd =
          (popcnt(W[3]) == 1 && popcnt(B[3]) == 1 && (W[1] | W[2] | W[4] | B[1] | B[2] | B[4]) == 0);
      if (!rookEnd)
        return 0;
      int wK = lsb_i(W[5]), bK = lsb_i(B[5]);
      if (wK < 0 || bK < 0)
        return 0;
      int sc = 0;
      auto cut_by = [&](chess::bb::Bitboard r, int ksq, int sign)
      {
        int rsq = lsb_i(r);
        if (chess::bb::file_of(rsq) == chess::bb::file_of(ksq))
        {
          int diff = std::abs(chess::bb::rank_of(rsq) - chess::bb::rank_of(ksq));
          if (diff >= ROOK_CUT_MIN_SEPARATION)
            sc += sign * ROOK_CUT_BONUS;
        }
        else if (chess::bb::rank_of(rsq) == chess::bb::rank_of(ksq))
        {
          int diff = std::abs(chess::bb::file_of(rsq) - chess::bb::file_of(ksq));
          if (diff >= ROOK_CUT_MIN_SEPARATION)
            sc += sign * ROOK_CUT_BONUS;
        }
      };
      if (white)
      {
        if (wr)
          cut_by(wr, bK, +1);
      }
      else
      {
        if (br)
          cut_by(br, wK, -1);
      }
      return sc;
    };
    eg += cut_score(true);
    eg += cut_score(false);

    return eg;
  }

  static int passer_blocker_quality(const std::array<chess::bb::Bitboard, 6> &W,
                                    const std::array<chess::bb::Bitboard, 6> &B, chess::bb::Bitboard wp, chess::bb::Bitboard bp,
                                    chess::bb::Bitboard occ)
  {
    int sc = 0;

    auto add_side = [&](bool white)
    {
      chess::bb::Bitboard paw = white ? wp : bp;
      chess::bb::Bitboard opp = white ? bp : wp;
      int sgn = white ? +1 : -1; // white POV: white passer help = +, hindrance = -

      chess::bb::Bitboard t = paw;
      while (t)
      {
        int s = lsb_i(t);
        t &= t - 1;
        bool passed = white ? ((M.wPassed[s] & opp) == 0) : ((M.bPassed[s] & opp) == 0);
        if (!passed)
          continue;

        int stop = white ? (s + 8) : (s - 8);
        if (stop < 0 || stop > 63)
          continue;
        chess::bb::Bitboard stopBB = chess::bb::sq_bb((chess::Square)stop);
        if (!(occ & stopBB))
          continue;

        int advance = white ? chess::bb::rank_of((chess::Square)s) : (7 - chess::bb::rank_of((chess::Square)s));
        int pen = 0;
        if (stopBB & (W[1] | B[1]))
          pen = BLOCK_PASSER_STOP_KNIGHT; // best stopper
        else if (stopBB & (W[2] | B[2]))
          pen = BLOCK_PASSER_STOP_BISHOP;

        sc += sgn * (-pen * advance); // penalty for the passer’s side
      }
    };

    add_side(true);
    add_side(false);
    return sc;
  }

  // =============================================================================
  // King tropism
  // =============================================================================
  static int king_tropism(const std::array<chess::bb::Bitboard, 6> &W, const std::array<chess::bb::Bitboard, 6> &B)
  {
    int wK = lsb_i(W[5]);
    int bK = lsb_i(B[5]);
    if (wK < 0 || bK < 0)
      return 0;
    int sc = 0;
    auto add = [&](chess::bb::Bitboard bb, int target, int sign, int base)
    {
      chess::bb::Bitboard t = bb;
      while (t)
      {
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

  static inline int center_manhattan(int sq)
  {
    if (sq < 0)
      return 6;
    int d1 = king_manhattan(sq, 27); // d4
    int d2 = king_manhattan(sq, 28); // e4
    int d3 = king_manhattan(sq, 35); // d5
    int d4 = king_manhattan(sq, 36); // e5
    return std::min(std::min(d1, d2), std::min(d3, d4));
  }
  static int king_activity_eg(const std::array<chess::bb::Bitboard, 6> &W, const std::array<chess::bb::Bitboard, 6> &B)
  {
    int wK = lsb_i(W[5]);
    int bK = lsb_i(B[5]);
    if (wK < 0 || bK < 0)
      return 0;
    return (center_manhattan(bK) - center_manhattan(wK)) * KING_ACTIVITY_EG_MULT;
  }

  // Passed-pawn-race EG
  static int passed_pawn_race_eg(const std::array<chess::bb::Bitboard, 6> &W, const std::array<chess::bb::Bitboard, 6> &B,
                                 const chess::Position &pos)
  {
    int minorMajor = popcnt(W[1] | W[2] | W[3] | B[1] | B[2] | B[3]);
    if ((PASS_RACE_NEED_QUEENLESS && popcnt(W[4] | B[4]) != 0) ||
        minorMajor > PASS_RACE_MAX_MINORMAJOR)
      return 0;

    int wK = lsb_i(W[5]), bK = lsb_i(B[5]);
    chess::bb::Bitboard wp = W[0], bp = B[0];
    int sc = 0;
    auto prom_sq = [](int sq, bool w)
    { return w ? ((sq & 7) | (7 << 3)) : ((sq & 7) | (0 << 3)); };
    auto eta = [&](bool white, int sq) -> int
    {
      int steps = white ? (7 - chess::bb::rank_of(sq)) : (chess::bb::rank_of(sq));
      int stmAdj = (pos.getState().sideToMove == (white ? chess::Color::White : chess::Color::Black))
                       ? 0
                       : PASS_RACE_STM_ADJ;
      return steps + stmAdj;
    };

    // white
    chess::bb::Bitboard t = wp;
    while (t)
    {
      int s = lsb_i(t);
      t &= t - 1;
      bool passed = (M.wPassed[s] & bp) == 0;
      if (!passed)
        continue;
      int q = prom_sq(s, true);
      int wETA = eta(true, s);
      int bKETA = king_manhattan(bK, q);
      sc += PASS_RACE_MULT * (bKETA - wETA);
    }
    // black
    t = bp;
    while (t)
    {
      int s = lsb_i(t);
      t &= t - 1;
      bool passed = (M.bPassed[s] & wp) == 0;
      if (!passed)
        continue;
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
  static int development(const std::array<chess::bb::Bitboard, 6> &W, const std::array<chess::bb::Bitboard, 6> &B)
  {
    chess::bb::Bitboard wMin = W[1] | W[2];
    chess::bb::Bitboard bMin = B[1] | B[2];
    static constexpr chess::bb::Bitboard wInit =
        chess::bb::sq_bb(chess::Square(1)) | chess::bb::sq_bb(chess::Square(6)) | chess::bb::sq_bb(chess::Square(2)) | chess::bb::sq_bb(chess::Square(5));
    static constexpr chess::bb::Bitboard bInit =
        chess::bb::sq_bb(chess::Square(57)) | chess::bb::sq_bb(chess::Square(62)) | chess::bb::sq_bb(chess::Square(58)) | chess::bb::sq_bb(chess::Square(61));
    int dW = popcnt(wMin & wInit);
    int dB = popcnt(bMin & bInit);
    int score = (dB - dW) * DEVELOPMENT_PIECE_ON_HOME_PENALTY;

    chess::bb::Bitboard wRook = W[3];
    chess::bb::Bitboard bRook = B[3];
    static constexpr chess::bb::Bitboard wRInit = chess::bb::sq_bb(chess::Square(0)) | chess::bb::sq_bb(chess::Square(7));
    static constexpr chess::bb::Bitboard bRInit = chess::bb::sq_bb(chess::Square(56)) | chess::bb::sq_bb(chess::Square(63));
    int rW = popcnt(wRook & wRInit);
    int rB = popcnt(bRook & bRInit);
    score += (rB - rW) * DEVELOPMENT_ROOK_ON_HOME_PENALTY;

    chess::bb::Bitboard wQueen = W[4];
    chess::bb::Bitboard bQueen = B[4];
    static constexpr chess::bb::Bitboard wQInit = chess::bb::sq_bb(chess::Square(3));
    static constexpr chess::bb::Bitboard bQInit = chess::bb::sq_bb(chess::Square(59));
    int qW = popcnt(wQueen & wQInit);
    int qB = popcnt(bQueen & bQInit);
    score += (qB - qW) * DEVELOPMENT_QUEEN_ON_HOME_PENALTY;

    return score;
  }

  static int piece_blocking(const std::array<chess::bb::Bitboard, 6> &W, const std::array<chess::bb::Bitboard, 6> &B)
  {
    int s = 0;
    static constexpr chess::bb::Bitboard wBlock = chess::bb::sq_bb(chess::Square(1 * 8 + 2)) | chess::bb::sq_bb(chess::Square(1 * 8 + 3)); // c2,d2
    static constexpr chess::bb::Bitboard bBlock = chess::bb::sq_bb(chess::Square(6 * 8 + 2)) | chess::bb::sq_bb(chess::Square(6 * 8 + 3)); // c7,d7
    if ((W[1] | W[2]) & wBlock)
      s -= PIECE_BLOCKING_PENALTY;
    if ((B[1] | B[2]) & bBlock)
      s += PIECE_BLOCKING_PENALTY;
    return s;
  }

  // =============================================================================
  // Endgame scalers
  // =============================================================================
  static inline int kdist_cheb(int a, int b)
  {
    if (a < 0 || b < 0)
      return 7;
    int df = std::abs((a & 7) - (b & 7));
    int dr = std::abs((a >> 3) - (b >> 3));
    return std::max(df, dr);
  }

  static int endgame_scale(const std::array<chess::bb::Bitboard, 6> &W, const std::array<chess::bb::Bitboard, 6> &B)
  {
    auto cnt = [&](int pt, int side)
    { return popcnt(side ? B[pt] : W[pt]); };
    int wP = cnt(0, 0), bP = cnt(0, 1), wN = cnt(1, 0), bN = cnt(1, 1), wB = cnt(2, 0),
        bB = cnt(2, 1), wR = cnt(3, 0), bR = cnt(3, 1), wQ = cnt(4, 0), bQ = cnt(4, 1);

    int wK = lsb_i(W[5]);
    int bK = lsb_i(B[5]);

    auto on_fileA = [&](chess::bb::Bitboard paw)
    { return (paw & M.file[0]) != 0; };
    auto on_fileH = [&](chess::bb::Bitboard paw)
    { return (paw & M.file[7]) != 0; };
    auto is_corner_pawn = [&](chess::bb::Bitboard paw)
    { return on_fileA(paw) || on_fileH(paw); };

    // K + Pawn position draw
    if (wP == 1 && bP == 0 && (wN | wB | wR | wQ | bN | bB | bR | bQ) == 0)
    {
      if (on_fileA(W[0]) && bK == 56 /*a8*/)
        return SCALE_DRAW;
      if (on_fileH(W[0]) && bK == 63 /*h8*/)
        return SCALE_DRAW;
    }
    if (bP == 1 && wP == 0 && (wN | wB | wR | wQ | bN | bB | bR | bQ) == 0)
    {
      if (on_fileA(B[0]) && wK == 0 /*a1*/)
        return SCALE_DRAW;
      if (on_fileH(B[0]) && wK == 7 /*h1*/)
        return SCALE_DRAW;
    }

    // Opposite-colored bishops only
    bool onlyBish = ((W[1] | W[3] | W[4] | B[1] | B[3] | B[4]) == 0) && wB == 1 && bB == 1;
    if (onlyBish)
    {
      int wBsq = lsb_i(W[2]);
      int bBsq = lsb_i(B[2]);
      bool wLight = ((chess::bb::file_of(wBsq) + chess::bb::rank_of(wBsq)) & 1) != 0;
      bool bLight = ((chess::bb::file_of(bBsq) + chess::bb::rank_of(bBsq)) & 1) != 0;
      if (wLight != bLight)
        return OPP_BISHOPS_SCALE;
    }

    // wrong bishop + sidepawn (K+B+P(a/h) vs K)
    if (wB == 1 && wP == 1 && is_corner_pawn(W[0]) && (bP | bN | bB | bR | bQ) == 0)
    {
      int corner = on_fileA(W[0]) ? 56 /*a8*/ : 63 /*h8*/;
      int d = kdist_cheb(bK, corner);
      if (d <= 1)
        return SCALE_DRAW;
      if (d <= 2)
        return SCALE_VERY_DRAWISH;
      return SCALE_MEDIUM;
    }
    if (bB == 1 && bP == 1 && is_corner_pawn(B[0]) && (wP | wN | wB | wR | wQ) == 0)
    {
      int corner = on_fileA(B[0]) ? 0 /*a1*/ : 7 /*h1*/;
      int d = kdist_cheb(wK, corner);
      if (d <= 1)
        return SCALE_DRAW;
      if (d <= 2)
        return SCALE_VERY_DRAWISH;
      return SCALE_MEDIUM;
    }

    // R+side-/double pawn vs R
    if (wR == 1 && bR == 1 && (wP <= 2) && is_corner_pawn(W[0]) && bP == 0)
    {
      int corner = on_fileA(W[0]) ? 56 : 63;
      int d = kdist_cheb(bK, corner);
      return (d <= 2 ? SCALE_VERY_DRAWISH : SCALE_REDUCED);
    }
    if (bR == 1 && wR == 1 && (bP <= 2) && is_corner_pawn(B[0]) && wP == 0)
    {
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
    if (wP == 0 && bP == 0 && wR == 0 && bR == 0 && wQ == 0 && bQ == 0)
    {
      int wMin = wN + wB, bMin = bN + bB;
      // K vs K / KN vs K / KB vs K / KN vs KN / KB vs KB
      if (wMin <= 1 && bMin <= 1)
        return SCALE_DRAW;
      // K  2N vs K (generally drawn with best play)
      if ((wN == 2 && wB == 0 && bMin == 0) || (bN == 2 && bB == 0 && wMin == 0))
        return SCALE_VERY_DRAWISH;
      // Minor vs minor of the same type: very drawish without pawns
      if ((wMin == 1 && bMin == 1) && ((wN == bN) || (wB == bB)))
        return SCALE_VERY_DRAWISH;
    }

    return FULL_SCALE;
  }

  // =============================================================================
  // Extra: castles & center
  // =============================================================================
  static void castling_and_center(const std::array<chess::bb::Bitboard, 6> &W, const std::array<chess::bb::Bitboard, 6> &B,
                                  int &mg_add, int &eg_add)
  {
    int wK = lsb_i(W[5]);
    int bK = lsb_i(B[5]);
    bool queensOn = (W[4] | B[4]) != 0;
    auto center_penalty = [&](int ksq, bool white)
    {
      if (ksq < 0)
        return 0;

      // Normalize to "white viewpoint" for the positional test
      int ksq_w = white ? ksq : mirror_sq_black(ksq);
      bool centerBack = (ksq_w == 4 || ksq_w == 3 || ksq_w == 5); // e1/d1/f1 from white POV
      if (!centerBack)
        return 0;

      chess::bb::Bitboard fileE = M.file[white ? 4 : mirror_sq_black(4)];
      chess::bb::Bitboard fileD = M.file[white ? 3 : mirror_sq_black(3)];

      chess::bb::Bitboard ownP = white ? W[0] : B[0];
      chess::bb::Bitboard oppP = white ? B[0] : W[0];

      int amp = 0;
      auto openish = [&](chess::bb::Bitboard f)
      {
        bool own = (f & ownP) != 0;
        bool opp = (f & oppP) != 0;
        if (!own && !opp)
          return CENTER_BACK_OPEN_FILE_OPEN;
        if (!own && opp)
          return CENTER_BACK_OPEN_FILE_SEMI;
        return 0;
      };
      amp += openish(fileD) + openish(fileE);

      int base = ((W[4] | B[4]) != 0) ? CENTER_BACK_PENALTY_Q_ON : CENTER_BACK_PENALTY_Q_OFF;
      return base + amp * CENTER_BACK_OPEN_FILE_WEIGHT;
    };

    auto castle_bonus = [&](int ksq)
    { return (ksq == 6 || ksq == 2) ? CASTLE_BONUS : 0; };
    mg_add += castle_bonus(wK);
    mg_add -= castle_bonus(mirror_sq_black(bK));
    mg_add += center_penalty(bK, false);
    mg_add -= center_penalty(wK, true);
    eg_add += (castle_bonus(wK) / 2) - (castle_bonus(mirror_sq_black(bK)) / 2);

    auto early_queen_malus = [&](const std::array<chess::bb::Bitboard, 6> &S, bool white)
    {
      chess::bb::Bitboard Q = S[4];
      chess::bb::Bitboard minorsHome = white ? (chess::bb::RANK_1 & (S[1] | S[2])) : (chess::bb::RANK_8 & (S[1] | S[2]));
      chess::bb::Bitboard qZone = white ? (chess::bb::RANK_2 | chess::bb::RANK_3) : (chess::bb::RANK_7 | chess::bb::RANK_6);
      return (((Q & qZone) != 0ULL) && (minorsHome != 0ULL)) ? EARLY_QUEEN_MALUS : 0;
    };

    int eqmW = early_queen_malus(W, true);
    int eqmB = early_queen_malus(B, false);
    mg_add += -eqmW + eqmB;

    // not castled heuristic
    if (queensOn)
    {
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
  struct alignas(64) EvalEntry
  {
    std::atomic<uint64_t> key{0};
    std::atomic<int32_t> score{0};
  };
  struct alignas(64) PawnEntry
  {
    std::atomic<uint64_t> key{0};
    std::atomic<int32_t> mg{0};
    std::atomic<int32_t> eg{0};
    std::atomic<uint64_t> wPA{0};
    std::atomic<uint64_t> bPA{0};
    std::atomic<uint64_t> wPass{0};
    std::atomic<uint64_t> bPass{0};
  };
  struct Evaluator::Impl
  {
    std::array<EvalEntry, EVAL_SIZE> eval;
    std::array<PawnEntry, PAWN_SIZE> pawn;
  };
  Evaluator::Evaluator() noexcept
  {
    m_impl = new Impl();
  }
  Evaluator::~Evaluator() noexcept
  {
    delete m_impl;
  }
  void Evaluator::clearCaches() const noexcept
  {
    if (!m_impl)
      return;
    for (auto &e : m_impl->eval)
    {
      e.key.store(0);
      e.score.store(0);
    }
    for (auto &p : m_impl->pawn)
    {
      p.key.store(0);
      p.mg.store(0);
      p.eg.store(0);
      p.wPA.store(0);
      p.bPA.store(0);
      p.wPass.store(0);
      p.bPass.store(0);
    }
  }
  static inline size_t idx_eval(uint64_t k)
  {
    return (size_t)k & (EVAL_SIZE - 1);
  }
  static inline size_t idx_pawn(uint64_t k)
  {
    return (size_t)k & (PAWN_SIZE - 1);
  }

  inline int sgn(int x)
  {
    return (x > 0) - (x < 0);
  }

  inline chess::bb::Bitboard rook_pins(chess::bb::Bitboard occ, chess::bb::Bitboard own, chess::bb::Bitboard oppRQ, int ksq, bool kingWhite,
                                       const AttackMap *A)
  {
    if (ksq < 0)
      return 0ULL;
    chess::bb::Bitboard pins = 0ULL;

    // candidate blockers are own pieces on rook rays from the king
    chess::bb::Bitboard blockers = cached_slider_attacks(A, kingWhite, chess::magic::Slider::Rook, ksq, occ) & own;

    while (blockers)
    {
      int b = lsb_i(blockers);
      blockers &= blockers - 1;

      int df = sgn(chess::bb::file_of(b) - chess::bb::file_of(ksq));
      int dr = sgn(chess::bb::rank_of(b) - chess::bb::rank_of(ksq));

      int f = chess::bb::file_of(b), r = chess::bb::rank_of(b);
      for (;;)
      {
        f += df;
        r += dr;
        if (f < 0 || f > 7 || r < 0 || r > 7)
          break;
        int s = (r << 3) | f;
        chess::bb::Bitboard bb = chess::bb::sq_bb((chess::Square)s);
        if (bb & occ)
        {
          if (bb & oppRQ)
            pins |= chess::bb::sq_bb((chess::Square)b);
          break;
        }
      }
    }
    return pins;
  }

  inline chess::bb::Bitboard bishop_pins(chess::bb::Bitboard occ, chess::bb::Bitboard own, chess::bb::Bitboard oppBQ, int ksq, bool kingWhite,
                                         const AttackMap *A)
  {
    if (ksq < 0)
      return 0ULL;
    chess::bb::Bitboard pins = 0ULL;

    chess::bb::Bitboard blockers = cached_slider_attacks(A, kingWhite, chess::magic::Slider::Bishop, ksq, occ) & own;

    while (blockers)
    {
      int b = lsb_i(blockers);
      blockers &= blockers - 1;

      int df = sgn(chess::bb::file_of(b) - chess::bb::file_of(ksq));
      int dr = sgn(chess::bb::rank_of(b) - chess::bb::rank_of(ksq));

      int f = chess::bb::file_of(b), r = chess::bb::rank_of(b);
      for (;;)
      {
        f += df;
        r += dr;
        if (f < 0 || f > 7 || r < 0 || r > 7)
          break;
        int s = (r << 3) | f;
        chess::bb::Bitboard bb = chess::bb::sq_bb((chess::Square)s);
        if (bb & occ)
        {
          if (bb & oppBQ)
            pins |= chess::bb::sq_bb((chess::Square)b);
          break;
        }
      }
    }
    return pins;
  }

  inline chess::bb::Bitboard no_king_attacks(bool white, const AttackMap &A)
  {
    // chess::squares attacked by the *defending* side excluding its king
    return white ? (A.bAll | A.bPA) : (A.wAll | A.wPA);
  }

  inline int safe_checks(bool white, const std::array<chess::bb::Bitboard, 6> &W,
                         const std::array<chess::bb::Bitboard, 6> &B, chess::bb::Bitboard occ, const AttackMap &A,
                         int oppK)
  {
    if (oppK < 0)
      return 0;
    const chess::bb::Bitboard occAll = occ;
    const chess::bb::Bitboard unsafe = no_king_attacks(white, A);
    int sc = 0;

    // knights: checking chess::squares = chess::bb::knight_attacks_from(oppK)
    chess::bb::Bitboard kn = white ? W[1] : B[1];
    // knights: safe checking moves to chess::squares that would check the king
    chess::bb::Bitboard knChk = chess::bb::knight_attacks_from((chess::Square)oppK); // checking landing chess::squares
    chess::bb::Bitboard ownOcc =
        white ? (W[0] | W[1] | W[2] | W[3] | W[4] | W[5]) : (B[0] | B[1] | B[2] | B[3] | B[4] | B[5]);
    chess::bb::Bitboard canMove = (white ? A.wN : A.bN) & ~ownOcc; // knight move targets (no self-block)
    sc += popcnt(knChk & canMove & ~unsafe) * KS_SAFE_CHECK_N;

    // replace the whole slider chunk in safe_checks() with this:
    auto add_slider_moves = [&](chess::bb::Bitboard attackedByUs, chess::magic::Slider sl, int w)
    {
      chess::bb::Bitboard rayFromK = cached_slider_attacks(&A, !white, sl, oppK, occAll);
      chess::bb::Bitboard origins = rayFromK & ~occAll; // empty chess::squares that would give check
      // additionally require that the origin is not attacked by enemy pawns,
      // which approximates “safe landing chess::square” well and filters noisy hits
      chess::bb::Bitboard notPawnAttacked = origins & ~(white ? A.bPA : A.wPA);
      sc += popcnt(notPawnAttacked & attackedByUs & ~unsafe) * w;
    };

    add_slider_moves(white ? A.wB : A.bB, chess::magic::Slider::Bishop, KS_SAFE_CHECK_B);
    add_slider_moves(white ? A.wR : A.bR, chess::magic::Slider::Rook, KS_SAFE_CHECK_R);
    // queen contributes along both geometries
    add_slider_moves(white ? A.wQ : A.bQ, chess::magic::Slider::Bishop, KS_SAFE_CHECK_QB);
    add_slider_moves(white ? A.wQ : A.bQ, chess::magic::Slider::Rook, KS_SAFE_CHECK_QR);

    // bonus only if those origins are not on ‘unsafe’ chess::squares
    // (Fast approximation: multiply result by a small factor if opp pawns cover ring)
    return sc;
  }

  inline chess::bb::Bitboard holes_for_white(chess::bb::Bitboard bp)
  {
    chess::bb::Bitboard cur = chess::bb::black_pawn_attacks(bp);
    chess::bb::Bitboard future = 0;
    chess::bb::Bitboard t = bp;
    while (t)
    {
      int s = lsb_i(t);
      t &= t - 1;
      int r = chess::bb::rank_of((chess::Square)s);
      // ranks strictly in front of this black pawn
      chess::bb::Bitboard forward = 0;
      for (int rr = r - 1; rr >= 0; --rr)
        forward |= (chess::bb::RANK_1 << (8 * rr));
      chess::bb::Bitboard diag = chess::magic::sliding_attacks(chess::magic::Slider::Bishop, (chess::Square)s, 0ULL);
      future |= (diag & forward);
    }
    return ~(cur | future);
  }

  inline chess::bb::Bitboard holes_for_black(chess::bb::Bitboard wp)
  {
    chess::bb::Bitboard cur = chess::bb::white_pawn_attacks(wp);
    chess::bb::Bitboard future = 0;
    chess::bb::Bitboard t = wp;
    while (t)
    {
      int s = lsb_i(t);
      t &= t - 1;
      int r = chess::bb::rank_of((chess::Square)s);
      // ranks strictly in front of this white pawn
      chess::bb::Bitboard forward = 0;
      for (int rr = r + 1; rr < 8; ++rr)
        forward |= (chess::bb::RANK_1 << (8 * rr));
      chess::bb::Bitboard diag = chess::magic::sliding_attacks(chess::magic::Slider::Bishop, (chess::Square)s, 0ULL);
      future |= (diag & forward);
    }
    return ~(cur | future);
  }

  inline int pawn_levers(chess::bb::Bitboard wp, chess::bb::Bitboard bp)
  {
    chess::bb::Bitboard wLever = chess::bb::white_pawn_attacks(wp) & bp;
    chess::bb::Bitboard bLever = chess::bb::black_pawn_attacks(bp) & wp;
    int centerW = popcnt(wLever & (chess::bb::FILE_C | chess::bb::FILE_D | chess::bb::FILE_E | chess::bb::FILE_F));
    int centerB = popcnt(bLever & (chess::bb::FILE_C | chess::bb::FILE_D | chess::bb::FILE_E | chess::bb::FILE_F));
    int wingW = popcnt(wLever) - centerW;
    int wingB = popcnt(bLever) - centerB; // was subtracting wingW (bug)
    return (centerW - centerB) * PAWN_LEVER_CENTER + (wingW - wingB) * PAWN_LEVER_WING;
  }

  inline int xray_king_file_pressure(bool white, const std::array<chess::bb::Bitboard, 6> &W,
                                     const std::array<chess::bb::Bitboard, 6> &B, chess::bb::Bitboard occ, int ksq,
                                     const AttackMap *A)
  {
    if (ksq < 0)
      return 0;
    chess::bb::Bitboard rooks = white ? W[3] : B[3];
    int sc = 0;
    chess::bb::Bitboard t = rooks;
    chess::bb::Bitboard bbK = chess::bb::sq_bb((chess::Square)ksq);

    while (t)
    {
      int r = lsb_i(t);
      t &= t - 1;

      // Require same rank or file to avoid cross-corner intersections
      if (chess::bb::file_of(r) != chess::bb::file_of(ksq))
        continue;

      chess::bb::Bitboard rookRay = cached_slider_attacks(A, white, chess::magic::Slider::Rook, r, occ);
      chess::bb::Bitboard kingRay = cached_slider_attacks(A, !white, chess::magic::Slider::Rook, ksq, occ);
      chess::bb::Bitboard between = rookRay & kingRay & ~chess::bb::sq_bb((chess::Square)r) & ~bbK;

      if (popcnt(between & occ) == 1)
        sc += XRAY_KFILE;
    }
    return white ? sc : -sc;
  }

  inline int queen_bishop_battery(bool white, const std::array<chess::bb::Bitboard, 6> &W,
                                  const std::array<chess::bb::Bitboard, 6> &B, chess::bb::Bitboard /*occ*/, int oppK,
                                  const AttackMap * /*A*/)
  {
    if (oppK < 0)
      return 0;
    chess::bb::Bitboard Q = white ? W[4] : B[4];
    chess::bb::Bitboard Bs = white ? W[2] : B[2];
    if (!Q || !Bs)
      return 0;

    chess::bb::Bitboard kDiag = chess::magic::sliding_attacks(chess::magic::Slider::Bishop, (chess::Square)oppK, 0ULL);
    bool aligned = (kDiag & Q) && (kDiag & Bs);
    return (white ? +1 : -1) * (aligned ? QB_BATTERY : 0);
  }

  static int central_blockers(const std::array<chess::bb::Bitboard, 6> &W, const std::array<chess::bb::Bitboard, 6> &B,
                              int phase)
  {
    auto block = [&](bool white)
    {
      chess::bb::Bitboard occ = white ? (W[1] | W[2] | W[3] | W[4]) : (B[1] | B[2] | B[3] | B[4]);
      int e = white ? 12 /*e2*/ : 52 /*e7*/;
      int d = white ? 11 /*d2*/ : 51 /*d7*/;
      int pen = 0;
      if (occ & chess::bb::sq_bb((chess::Square)e))
        pen += CENTER_BLOCK_PEN;
      if (occ & chess::bb::sq_bb((chess::Square)d))
        pen += CENTER_BLOCK_PEN;
      return pen;
    };
    int pen = 0;
    pen -= block(true);
    pen += block(false);
    // scale by opening weight:
    return pen * std::min(phase, CENTER_BLOCK_PHASE_MAX) / CENTER_BLOCK_PHASE_DEN;
  }

  inline int weakly_defended(const std::array<chess::bb::Bitboard, 6> &W, const std::array<chess::bb::Bitboard, 6> &B,
                             const AttackMap &A)
  {
    auto score_set = [&](chess::bb::Bitboard pieces, chess::bb::Bitboard atk, chess::bb::Bitboard def, int val, int sign)
    {
      int sc = 0;
      chess::bb::Bitboard p = pieces;
      while (p)
      {
        int s = lsb_i(p);
        p &= p - 1;
        chess::bb::Bitboard bb = chess::bb::sq_bb((chess::Square)s);
        int d = ((def & bb) != 0) - ((atk & bb) != 0); // +1 defended only, -1 attacked only
        if (d < 0)
          sc += sign * val;
      }
      return sc;
    };

    chess::bb::Bitboard wDef = A.wAll | A.wPA | A.wKAtt;
    chess::bb::Bitboard bDef = A.bAll | A.bPA | A.bKAtt;
    chess::bb::Bitboard wAtk = A.bAll | A.bPA;
    chess::bb::Bitboard bAtk = A.wAll | A.wPA;

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
  static int fianchetto_structure_ksmg(const std::array<chess::bb::Bitboard, 6> &W,
                                       const std::array<chess::bb::Bitboard, 6> &B, int wK, int bK)
  {
    auto sqbb = [](int f, int r) -> chess::bb::Bitboard
    { return chess::bb::sq_bb((chess::Square)((r << 3) | f)); };

    auto score_side = [&](bool white) -> int
    {
      int k = white ? wK : bK;
      if (k < 0)
        return 0;

      const int kFile = chess::bb::file_of(k);
      const int kRank = chess::bb::rank_of(k);
      const chess::bb::Bitboard paw = white ? W[0] : B[0];

      // consider king "behind" a fianchetto if it's on g-file (6) for short castles or
      // on the queenside files (c- or b-file) after long castling, near the home ranks
      const bool nearHome = white ? (kRank <= 2) : (kRank >= 5);
      const bool kingSide = (kFile == 6);
      const bool queenSide = (kFile == 1) || (kFile == 2);
      if (!nearHome || (!kingSide && !queenSide))
        return 0;

      const int f = kingSide ? 6 : 1; // use b-file for long castles (king on b/c-file)
      // acceptable fianchetto pawn chess::squares (OK) on the relevant file (g- or b-file):
      const int okR1 = white ? 1 : 6; // g2/b2 or g7/b7
      const int okR2 = white ? 2 : 5; // g3/b3 or g6/b6
      const chess::bb::Bitboard okMask = sqbb(f, okR1) | sqbb(f, okR2);

      // any pawn on that (g- or b-) file at all? (queenside uses the b-file mask)
      const chess::bb::Bitboard anyOnFile = paw & M.file[f];

      if (paw & okMask)
      {
        return FIANCHETTO_OK;
      }
      else if (anyOnFile)
      {
        // pawn exists on that file but not on OK chess::squares (advanced/abandoned shape)
        return -FIANCHETTO_HOLE;
      }
      else
      {
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
  int Evaluator::evaluate(const SearchPosition &pos) const
  {
    const chess::Board &b = pos.getBoard();
    uint64_t key = (uint64_t)pos.hash();
    uint64_t pKey = (uint64_t)pos.getState().pawnKey;

    // prefetch caches
    prefetch_ro(&m_impl->eval[idx_eval(key)]);
    prefetch_ro(&m_impl->pawn[idx_pawn(pKey)]);

    // probe eval cache
    {
      auto &e = m_impl->eval[idx_eval(key)];
      uint64_t k = e.key.load(std::memory_order_acquire);
      if (k == key)
        return e.score.load(std::memory_order_relaxed);
    }

    // chess::bb::bitboards
    std::array<chess::bb::Bitboard, 6> W{}, B{};
    for (int pt = 0; pt < 6; ++pt)
    {
      W[pt] = b.getPieces(chess::Color::White, (chess::PieceType)pt);
      B[pt] = b.getPieces(chess::Color::Black, (chess::PieceType)pt);
    }
    chess::bb::Bitboard occ = b.getAllPieces();
    chess::bb::Bitboard wocc = b.getPieces(chess::Color::White);
    chess::bb::Bitboard bocc = b.getPieces(chess::Color::Black);

    // material, pst, phase, counts
    const auto &ac = pos.evalAcc();
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
    chess::bb::Bitboard wPA = 0, bPA = 0, wPass = 0, bPass = 0;
    {
      auto &ps = m_impl->pawn[idx_pawn(pKey)];
      uint64_t k = ps.key.load(std::memory_order_acquire);
      if (k == pKey)
      {
        pMG = ps.mg.load(std::memory_order_relaxed);
        pEG = ps.eg.load(std::memory_order_relaxed);
        wPA = (chess::bb::Bitboard)ps.wPA.load(std::memory_order_relaxed);
        bPA = (chess::bb::Bitboard)ps.bPA.load(std::memory_order_relaxed);
        wPass = (chess::bb::Bitboard)ps.wPass.load(std::memory_order_relaxed);
        bPass = (chess::bb::Bitboard)ps.bPass.load(std::memory_order_relaxed);
      }
      else
      {
        wPA = chess::bb::white_pawn_attacks(W[0]);
        bPA = chess::bb::black_pawn_attacks(B[0]);
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
    A.wKAtt = (wK >= 0) ? chess::bb::king_attacks_from((chess::Square)wK) : 0;
    A.bKAtt = (bK >= 0) ? chess::bb::king_attacks_from((chess::Square)bK) : 0;

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
    ksMG = std::clamp(ksMG, -KS_MG_CLAMP, KS_MG_CLAMP); // e.g.  ±400
    ksEG = std::clamp(ksEG, -KS_EG_CLAMP, KS_EG_CLAMP); // e.g.  ±200

    int shelterMG = shelter;
    int shelterEG = shelter / SHELTER_EG_DEN;

    // ---------------------------------------------------------------------------
    // Pins
    chess::bb::Bitboard wPins = rook_pins(occ, wocc, (B[3] | B[4]), wK, true, &A) |
                                bishop_pins(occ, wocc, (B[2] | B[4]), wK, true, &A);
    chess::bb::Bitboard bPins = rook_pins(occ, bocc, (W[3] | W[4]), bK, false, &A) |
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
    chess::bb::Bitboard wHoles = holes_for_white(B[0]); // usable by white
    chess::bb::Bitboard bHoles = holes_for_black(W[0]); // usable by black
    const chess::bb::Bitboard W_ENEMY_HALF = chess::bb::RANK_4 | chess::bb::RANK_5 | chess::bb::RANK_6 | chess::bb::RANK_7;
    const chess::bb::Bitboard B_ENEMY_HALF = chess::bb::RANK_1 | chess::bb::RANK_2 | chess::bb::RANK_3 | chess::bb::RANK_4;

    int holeScore = 0;
    holeScore += popcnt((W[1] & wHoles) & W_ENEMY_HALF) * HOLE_OCC_KN;
    holeScore -= popcnt((B[1] & bHoles) & B_ENEMY_HALF) * HOLE_OCC_KN;

    if (bK >= 0)
    {
      int c = popcnt((A.wB & wHoles) & M.kingRing[bK]);
      holeScore += c * HOLE_ATT_BI;
    }
    if (wK >= 0)
    {
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
        sc + (xray / 2) + qbatt;                                                    // halve xray to reduce double-counting with rook_activity
    kingAtkMG = std::clamp(kingAtkMG, -KS_TACTICAL_MG_CLAMP, KS_TACTICAL_MG_CLAMP); // new soft guard
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
    eg_add += passed_pawn_race_eg(W, B, pos.position());

    // castles & center (existing)
    castling_and_center(W, B, mg_add, eg_add);

    mg += mg_add;
    eg += eg_add;

    // scale only the EG component
    {
      const int scale = endgame_scale(W, B); // FULL_SCALE == 64 (or whatever you use)
      eg = (eg * scale) / FULL_SCALE;
    }
    int score = taper(mg, eg, curPhase);
    // tempo (phase-aware)
    const bool wtm = (pos.getState().sideToMove == chess::Color::White);
    const int tempo = taper(TEMPO_MG, TEMPO_EG, curPhase);
    score += (wtm ? +tempo : -tempo);

    score = clampi(score, -MATE + 1, MATE - 1);

    // store eval
    auto &e = m_impl->eval[idx_eval(key)];
    e.score.store(score, std::memory_order_relaxed);
    e.key.store(key, std::memory_order_release);
    return score;
  }

} // namespace lilia::engine
