#include "lilia/engine/eval.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cassert>
#include <cstdint>
#include <limits>
#include <cstdlib>

#include "lilia/engine/config.hpp"
#include "lilia/engine/eval_acc.hpp"
#include "lilia/engine/eval_alias.hpp"
#include "lilia/engine/eval_shared.hpp"
#include "lilia/engine/search_position.hpp"
#include "lilia/chess/core/bitboard.hpp"
#include "lilia/chess/core/magic.hpp"
#include "lilia/chess/compiler.hpp"

namespace lilia::engine
{

  LILIA_ALWAYS_INLINE int lsb_i(chess::bb::Bitboard b) noexcept
  {
    return b ? chess::bb::ctz64(b) : -1;
  }
  LILIA_ALWAYS_INLINE int pop_lsb_i(chess::bb::Bitboard &b) noexcept
  {
    LILIA_ASSUME(b != 0);
    const int s = chess::bb::ctz64(b);
    b &= b - 1;
    return s;
  }

  LILIA_ALWAYS_INLINE int msb_i(chess::bb::Bitboard b) noexcept
  {
    return b ? 63 - chess::bb::clz64(b) : -1;
  }
  LILIA_ALWAYS_INLINE int clampi(int x, int lo, int hi)
  {
    return x < lo ? lo : (x > hi ? hi : x);
  }
  LILIA_ALWAYS_INLINE int king_chebyshev(int a, int b)
  {
    if (LILIA_UNLIKELY(a < 0 || b < 0))
      return 7;
    const int af = a & 7, bf = b & 7;
    const int ar = a >> 3, br = b >> 3;
    const int df = af > bf ? af - bf : bf - af;
    const int dr = ar > br ? ar - br : br - ar;
    return df > dr ? df : dr;
  }

  LILIA_ALWAYS_INLINE int sgn(int x)
  {
    return (x > 0) - (x < 0);
  }

  template <typename T>
  LILIA_ALWAYS_INLINE void prefetch_ro(const T *p) noexcept
  {
#if defined(__GNUC__) || defined(__clang__)
    __builtin_prefetch(static_cast<const void *>(p), 0 /* read */, 2 /* locality */);
#else
    (void)p;
#endif
  }

  LILIA_ALWAYS_INLINE bool only_rook_pawns(chess::bb::Bitboard pawns) noexcept
  {
    constexpr chess::bb::Bitboard ROOK_FILES = chess::bb::FILE_A | chess::bb::FILE_H;
    return pawns && ((pawns & ~ROOK_FILES) == 0ULL);
  }

  LILIA_ALWAYS_INLINE bool pawns_on_both_wings(chess::bb::Bitboard pawns) noexcept
  {
    constexpr chess::bb::Bitboard LEFT =
        chess::bb::FILE_A | chess::bb::FILE_B | chess::bb::FILE_C | chess::bb::FILE_D;
    constexpr chess::bb::Bitboard RIGHT =
        chess::bb::FILE_E | chess::bb::FILE_F | chess::bb::FILE_G | chess::bb::FILE_H;
    return (pawns & LEFT) && (pawns & RIGHT);
  }

  struct Masks
  {
    std::array<chess::bb::Bitboard, chess::SQ_NB> file{};
    std::array<chess::bb::Bitboard, chess::SQ_NB> adjFiles{};
    std::array<chess::bb::Bitboard, chess::SQ_NB> wPassed{}, bPassed{}, wFront{}, bFront{};
    std::array<chess::bb::Bitboard, chess::SQ_NB> kingRing{};
    std::array<chess::bb::Bitboard, chess::SQ_NB> frontSpanW{}, frontSpanB{};
    std::array<chess::bb::Bitboard, 8> rankLE{}, rankGE{};
    std::array<chess::bb::Bitboard, chess::SQ_NB> wFuturePawnAtt{}, bFuturePawnAtt{};
  };

  consteval Masks init_masks()
  {
    Masks m{};

    for (int r = 0; r < 8; ++r)
    {
      chess::bb::Bitboard le = 0, ge = 0;
      for (int rr = 0; rr <= r; ++rr)
        le |= (chess::bb::RANK_1 << (8 * rr));
      for (int rr = r; rr < 8; ++rr)
        ge |= (chess::bb::RANK_1 << (8 * rr));
      m.rankLE[r] = le;
      m.rankGE[r] = ge;
    }

    for (int sq = 0; sq < chess::SQ_NB; ++sq)
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

      chess::bb::Bitboard ring = 0;
      for (int dr = -KING_RING_RADIUS; dr <= KING_RING_RADIUS; ++dr)
        for (int df = -KING_RING_RADIUS; df <= KING_RING_RADIUS; ++df)
        {
          int nr = r + dr, nf = f + df;
          if (0 <= nr && nr < 8 && 0 <= nf && nf < 8)
            ring |= chess::bb::sq_bb(static_cast<chess::Square>((nr << 3) | nf));
        }
      m.kingRing[sq] = ring;

      // all squares a future white pawn from sq could ever attack
      chess::bb::Bitboard wFuture = 0;
      for (int rr = r + 1; rr < 8; ++rr)
      {
        if (f > 0)
          wFuture |= chess::bb::sq_bb(static_cast<chess::Square>((rr << 3) | (f - 1)));
        if (f < 7)
          wFuture |= chess::bb::sq_bb(static_cast<chess::Square>((rr << 3) | (f + 1)));
      }
      m.wFuturePawnAtt[sq] = wFuture;

      // all squares a future black pawn from sq could ever attack
      chess::bb::Bitboard bFuture = 0;
      for (int rr = r - 1; rr >= 0; --rr)
      {
        if (f > 0)
          bFuture |= chess::bb::sq_bb(static_cast<chess::Square>((rr << 3) | (f - 1)));
        if (f < 7)
          bFuture |= chess::bb::sq_bb(static_cast<chess::Square>((rr << 3) | (f + 1)));
      }
      m.bFuturePawnAtt[sq] = bFuture;
    }

    return m;
  }

  static constexpr Masks M = init_masks();

  consteval auto init_between_exclusive()
  {
    std::array<std::array<chess::bb::Bitboard, chess::SQ_NB>, chess::SQ_NB> table{};

    for (int a = 0; a < chess::SQ_NB; ++a)
    {
      for (int b = 0; b < chess::SQ_NB; ++b)
      {
        if (a == b)
        {
          table[a][b] = 0ULL;
          continue;
        }

        const int af = a & 7;
        const int ar = a >> 3;
        const int bf = b & 7;
        const int br = b >> 3;

        const int df0 = bf - af;
        const int dr0 = br - ar;

        const int adf = df0 < 0 ? -df0 : df0;
        const int adr = dr0 < 0 ? -dr0 : dr0;

        const bool aligned = (af == bf) || (ar == br) || (adf == adr);
        if (!aligned)
        {
          table[a][b] = 0ULL;
          continue;
        }

        const int df = (df0 > 0) - (df0 < 0);
        const int dr = (dr0 > 0) - (dr0 < 0);

        chess::bb::Bitboard m = 0ULL;
        for (int f = af + df, r = ar + dr; f != bf || r != br; f += df, r += dr)
          m |= chess::bb::sq_bb(static_cast<chess::Square>((r << 3) | f));

        m &= ~chess::bb::sq_bb(static_cast<chess::Square>(a));
        m &= ~chess::bb::sq_bb(static_cast<chess::Square>(b));
        table[a][b] = m;
      }
    }

    return table;
  }

  static constexpr auto BETWEEN_EXCLUSIVE = init_between_exclusive();

  consteval auto init_line_mask()
  {
    std::array<std::array<chess::bb::Bitboard, chess::SQ_NB>, chess::SQ_NB> table{};

    for (int a = 0; a < chess::SQ_NB; ++a)
    {
      for (int b = 0; b < chess::SQ_NB; ++b)
      {
        const int af = a & 7;
        const int ar = a >> 3;
        const int bf = b & 7;
        const int br = b >> 3;

        const int df0 = bf - af;
        const int dr0 = br - ar;

        const int adf = df0 < 0 ? -df0 : df0;
        const int adr = dr0 < 0 ? -dr0 : dr0;

        const bool aligned = (af == bf) || (ar == br) || (adf == adr);
        if (!aligned)
        {
          table[a][b] = 0ULL;
          continue;
        }

        if (a == b)
        {
          table[a][b] = chess::bb::sq_bb(static_cast<chess::Square>(a));
          continue;
        }

        const int df = (df0 > 0) - (df0 < 0);
        const int dr = (dr0 > 0) - (dr0 < 0);

        chess::bb::Bitboard m = 0ULL;

        for (int f = af, r = ar; 0 <= f && f < 8 && 0 <= r && r < 8; f -= df, r -= dr)
          m |= chess::bb::sq_bb(static_cast<chess::Square>((r << 3) | f));

        for (int f = af + df, r = ar + dr; 0 <= f && f < 8 && 0 <= r && r < 8; f += df, r += dr)
          m |= chess::bb::sq_bb(static_cast<chess::Square>((r << 3) | f));

        table[a][b] = m;
      }
    }

    return table;
  }

  static constexpr auto LINE_MASK = init_line_mask();

  constexpr chess::bb::Bitboard CENTER4 =
      chess::bb::sq_bb(chess::bb::D4) | chess::bb::sq_bb(chess::bb::E4) | chess::bb::sq_bb(chess::bb::D5) | chess::bb::sq_bb(chess::bb::E5);

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
    sc += s(mc.N[0], mc.N[1], MAT_IMB_N_PAIR_COEF, MAT_IMB_N_PAIR_COEF);
    sc += s(mc.B[0], mc.B[1], MAT_IMB_B_PAIR_COEF, MAT_IMB_B_PAIR_COEF);

    sc += (mc.R[0] * mc.N[0] * MAT_IMB_RN_SYNERGY) - (mc.R[1] * mc.N[1] * MAT_IMB_RN_SYNERGY);
    sc += (mc.R[0] * mc.B[0] * MAT_IMB_RB_SYNERGY) - (mc.R[1] * mc.B[1] * MAT_IMB_RB_SYNERGY);
    sc += (mc.Q[0] * mc.R[0] * MAT_IMB_QR_SYNERGY) - (mc.Q[1] * mc.R[1] * MAT_IMB_QR_SYNERGY);

    return sc;
  }

  static int space_term(chess::bb::Bitboard wocc, chess::bb::Bitboard bocc,
                        chess::bb::Bitboard wPA, chess::bb::Bitboard bPA,
                        int wMinorCnt, int bMinorCnt)
  {
    constexpr chess::bb::Bitboard WSPACE =
        (chess::bb::FILE_C | chess::bb::FILE_D | chess::bb::FILE_E | chess::bb::FILE_F) &
        (chess::bb::RANK_2 | chess::bb::RANK_3 | chess::bb::RANK_4);

    constexpr chess::bb::Bitboard BSPACE =
        (chess::bb::FILE_C | chess::bb::FILE_D | chess::bb::FILE_E | chess::bb::FILE_F) &
        (chess::bb::RANK_5 | chess::bb::RANK_6 | chess::bb::RANK_7);

    const chess::bb::Bitboard occ = wocc | bocc;
    const chess::bb::Bitboard empty = ~occ;

    const int wSafe = chess::bb::popcount(WSPACE & empty & ~bPA);
    const int bSafe = chess::bb::popcount(BSPACE & empty & ~wPA);

    const int wScale = SPACE_SCALE_BASE + std::min(wMinorCnt, SPACE_MINOR_SATURATION);
    const int bScale = SPACE_SCALE_BASE + std::min(bMinorCnt, SPACE_MINOR_SATURATION);

    int raw = SPACE_BASE * (wSafe * wScale - bSafe * bScale);
    return std::clamp(raw, -SPACE_CLAMP, SPACE_CLAMP);
  }

  // =============================================================================
  // Pawn structure (MG/EG SPLIT + PawnTT mg/eg)
  // =============================================================================

  inline int pawn_levers(chess::bb::Bitboard wp, chess::bb::Bitboard bp)
  {
    chess::bb::Bitboard wLever = chess::bb::white_pawn_attacks(wp) & bp;
    chess::bb::Bitboard bLever = chess::bb::black_pawn_attacks(bp) & wp;
    int centerW = chess::bb::popcount(wLever & (chess::bb::FILE_C | chess::bb::FILE_D | chess::bb::FILE_E | chess::bb::FILE_F));
    int centerB = chess::bb::popcount(bLever & (chess::bb::FILE_C | chess::bb::FILE_D | chess::bb::FILE_E | chess::bb::FILE_F));
    int wingW = chess::bb::popcount(wLever) - centerW;
    int wingB = chess::bb::popcount(bLever) - centerB;
    return (centerW - centerB) * PAWN_LEVER_CENTER + (wingW - wingB) * PAWN_LEVER_WING;
  }

  struct PawnOnly
  {
    int mg = 0, eg = 0;
    int lever = 0;

    int wLightPawns = 0, wDarkPawns = 0;
    int bLightPawns = 0, bDarkPawns = 0;
    bool wClosedCenter = false, bClosedCenter = false;

    chess::bb::Bitboard wPass = 0, bPass = 0;
    chess::bb::Bitboard wHoles = 0, bHoles = 0;
  };

  static PawnOnly pawn_structure_pawnhash_only(chess::bb::Bitboard wp, chess::bb::Bitboard bp,
                                               chess::bb::Bitboard wPA, chess::bb::Bitboard bPA)
  {
    PawnOnly out{};
    int &mg = out.mg;
    int &eg = out.eg;

    const chess::bb::Bitboard pawnOcc = wp | bp;

    auto is_light_sq = [](int sq) -> bool
    {
      return ((sq ^ (sq >> 3)) & 1) != 0;
    };

    chess::bb::Bitboard wFutureAtt = 0ULL;
    chess::bb::Bitboard bFutureAtt = 0ULL;
    int candidateDiff = 0; // white candidates - black candidates
    int backwardDiff = 0;  // white backward pawns - black backward pawns

    // Isolani & doubled
    for (int f = 0; f < 8; ++f)
    {
      const chess::bb::Bitboard F = M.file[f];
      const chess::bb::Bitboard ADJ = (f > 0 ? M.file[f - 1] : 0ULL) |
                                      (f < 7 ? M.file[f + 1] : 0ULL);

      const int wc = chess::bb::popcount(wp & F);
      const int bc = chess::bb::popcount(bp & F);

      if (wc)
      {
        if (!(wp & ADJ))
        {
          mg -= ISO_P * wc;
          eg -= (ISO_P * wc) / ISO_EG_DEN;
        }
        if (wc > 1)
        {
          mg -= DOUBLED_P * (wc - 1);
          eg -= (DOUBLED_P * (wc - 1)) / DOUBLED_EG_DEN;
        }
      }

      if (bc)
      {
        if (!(bp & ADJ))
        {
          mg += ISO_P * bc;
          eg += (ISO_P * bc) / ISO_EG_DEN;
        }
        if (bc > 1)
        {
          mg += DOUBLED_P * (bc - 1);
          eg += (DOUBLED_P * (bc - 1)) / DOUBLED_EG_DEN;
        }
      }
    }

    // Phalanx: count each adjacent pair once
    const int wPhalanx = chess::bb::popcount(((wp & ~chess::bb::FILE_A) >> 1) & wp);
    const int bPhalanx = chess::bb::popcount(((bp & ~chess::bb::FILE_A) >> 1) & bp);
    mg += PHALANX * (wPhalanx - bPhalanx);
    eg += (PHALANX * (wPhalanx - bPhalanx)) / PHALANX_EG_DEN;

    // White pawns
    chess::bb::Bitboard t = wp;
    while (t)
    {
      const int s = static_cast<int>(chess::bb::pop_lsb_unchecked(t));
      const int r = chess::bb::rank_of(static_cast<chess::Square>(s));

      if (is_light_sq(s))
        ++out.wLightPawns;
      else
        ++out.wDarkPawns;

      wFutureAtt |= M.wFuturePawnAtt[s];

      const bool passed = (M.wPassed[s] & bp) == 0ULL;

      const int stop = s + 8;
      const chess::bb::Bitboard stopBB =
          (stop >= 0 && stop < 64) ? chess::bb::sq_bb(static_cast<chess::Square>(stop)) : 0ULL;

      const bool frontClearOfEnemyPawns = (M.wFront[s] & bp) == 0ULL;
      const bool advanceFreeOfPawns = stopBB && ((pawnOcc & stopBB) == 0ULL);
      const bool supportedAdvance = stopBB && ((wPA & stopBB) != 0ULL);
      const bool enemyControlsAdvance = stopBB && ((bPA & stopBB) != 0ULL);
      const bool candidate =
          !passed &&
          frontClearOfEnemyPawns &&
          advanceFreeOfPawns &&
          supportedAdvance &&
          !enemyControlsAdvance;

      if (candidate)
      {
        mg += CANDIDATE_P;
        ++candidateDiff;
      }

      if (passed)
      {
        mg += PASSED_MG[r];
        eg += PASSED_EG[r];
        out.wPass |= chess::bb::sq_bb(static_cast<chess::Square>(s));

        const int steps = 7 - r;
        if (steps <= PASS_NEAR_PROMO_STEP2_MAX)
        {
          mg += PASS_NEAR_PROMO_STEP2_MG;
          eg += PASS_NEAR_PROMO_STEP2_EG;
        }
        else if (steps == PASS_NEAR_PROMO_STEP3_EQ)
        {
          mg += PASS_NEAR_PROMO_STEP3_MG;
          eg += PASS_NEAR_PROMO_STEP3_EG;
        }
      }

      // backward pawn
      if (r != 7 && !passed)
      {
        const int front = s + 8;
        const chess::bb::Bitboard frontBB = chess::bb::sq_bb(static_cast<chess::Square>(front));

        const bool enemyControls = (bPA & frontBB) != 0;
        const bool ownControls = (wPA & frontBB) != 0;

        if (enemyControls && !ownControls)
        {
          const chess::bb::Bitboard supportersSame = (M.adjFiles[s] & wp & M.rankLE[r]);
          if (!supportersSame)
          {
            mg -= BACKWARD_P;
            ++backwardDiff;
          }
        }
      }
    }

    // Black pawns
    t = bp;
    while (t)
    {
      const int s = static_cast<int>(chess::bb::pop_lsb_unchecked(t));
      const int r = chess::bb::rank_of(static_cast<chess::Square>(s));

      if (is_light_sq(s))
        ++out.bLightPawns;
      else
        ++out.bDarkPawns;

      bFutureAtt |= M.bFuturePawnAtt[s];

      const bool passed = (M.bPassed[s] & wp) == 0ULL;

      const int stop = s - 8;
      const chess::bb::Bitboard stopBB =
          (stop >= 0 && stop < 64) ? chess::bb::sq_bb(static_cast<chess::Square>(stop)) : 0ULL;

      const bool frontClearOfEnemyPawns = (M.bFront[s] & wp) == 0ULL;
      const bool advanceFreeOfPawns = stopBB && ((pawnOcc & stopBB) == 0ULL);
      const bool supportedAdvance = stopBB && ((bPA & stopBB) != 0ULL);
      const bool enemyControlsAdvance = stopBB && ((wPA & stopBB) != 0ULL);
      const bool candidate =
          !passed &&
          frontClearOfEnemyPawns &&
          advanceFreeOfPawns &&
          supportedAdvance &&
          !enemyControlsAdvance;

      if (candidate)
      {
        mg -= CANDIDATE_P;
        --candidateDiff;
      }

      if (passed)
      {
        mg -= PASSED_MG[7 - r];
        eg -= PASSED_EG[7 - r];
        out.bPass |= chess::bb::sq_bb(static_cast<chess::Square>(s));

        const int steps = r;
        if (steps <= PASS_NEAR_PROMO_STEP2_MAX)
        {
          mg -= PASS_NEAR_PROMO_STEP2_MG;
          eg -= PASS_NEAR_PROMO_STEP2_EG;
        }
        else if (steps == PASS_NEAR_PROMO_STEP3_EQ)
        {
          mg -= PASS_NEAR_PROMO_STEP3_MG;
          eg -= PASS_NEAR_PROMO_STEP3_EG;
        }
      }

      // backward pawn
      if (r != 0 && !passed)
      {
        const int front = s - 8;
        const chess::bb::Bitboard frontBB = chess::bb::sq_bb(static_cast<chess::Square>(front));

        const bool enemyControls = (wPA & frontBB) != 0;
        const bool ownControls = (bPA & frontBB) != 0;

        if (enemyControls && !ownControls)
        {
          const chess::bb::Bitboard supportersSame = (M.adjFiles[s] & bp & M.rankGE[r]);
          if (!supportersSame)
          {
            mg += BACKWARD_P;
            --backwardDiff;
          }
        }
      }
    }

    eg += (CANDIDATE_P * candidateDiff) / CANDIDATE_EG_DEN;
    eg -= (BACKWARD_P * backwardDiff) / BACKWARD_EG_DEN;

    // Closed center: any direct white-vs-black pawn lock on the d/e files
    const chess::bb::Bitboard centralFiles = chess::bb::FILE_D | chess::bb::FILE_E;
    const chess::bb::Bitboard wCentral = wp & centralFiles;
    const chess::bb::Bitboard bCentral = bp & centralFiles;

    const bool centerLocked = (chess::bb::north(wCentral) & bCentral) != 0ULL;
    out.wClosedCenter = centerLocked;
    out.bClosedCenter = centerLocked;

    // Connected passers
    const int wC = chess::bb::popcount(((out.wPass & ~chess::bb::FILE_H) << 1) & out.wPass);
    const int bC = chess::bb::popcount(((out.bPass & ~chess::bb::FILE_H) << 1) & out.bPass);

    mg += (CONNECTED_PASSERS * (wC - bC)) / CONNECTED_PASSERS_MG_DEN;
    eg += CONNECTED_PASSERS * (wC - bC);

    // Cached hole maps and lever score
    out.wHoles = ~(bPA | bFutureAtt);
    out.bHoles = ~(wPA | wFutureAtt);
    out.lever = pawn_levers(wp, bp);

    return out;
  }

  struct PasserDyn
  {
    int mg = 0, eg = 0;
  };

  struct AttackMap
  {
    // non-pawn / non-king attacks
    chess::bb::Bitboard wAll = 0, bAll = 0;

    // squares attacked at least twice
    chess::bb::Bitboard w2 = 0, b2 = 0;

    chess::bb::Bitboard wPA = 0, bPA = 0;
    chess::bb::Bitboard wKAtt = 0, bKAtt = 0;

    chess::bb::Bitboard wN = 0, wB = 0, wR = 0, wQ = 0;
    chess::bb::Bitboard bN = 0, bB = 0, bR = 0, bQ = 0;

    int wKingAttackers = 0;
    int bKingAttackers = 0;
    int wKingRingUnits = 0;
    int bKingRingUnits = 0;
  };

  LILIA_ALWAYS_INLINE chess::bb::Bitboard white_double_pawn_attacks(chess::bb::Bitboard wp) noexcept
  {
    return ((wp & ~chess::bb::FILE_A) << 7) &
           ((wp & ~chess::bb::FILE_H) << 9);
  }

  LILIA_ALWAYS_INLINE chess::bb::Bitboard black_double_pawn_attacks(chess::bb::Bitboard bp) noexcept
  {
    return ((bp & ~chess::bb::FILE_A) >> 9) &
           ((bp & ~chess::bb::FILE_H) >> 7);
  }

  static PasserDyn passer_dynamic_bonus(
      chess::bb::Bitboard occ,
      int wK, int bK,
      chess::bb::Bitboard wPass,
      chess::bb::Bitboard bPass)
  {
    PasserDyn d{};

    auto add_side = [&](bool white)
    {
      chess::bb::Bitboard pass = white ? wPass : bPass;
      const int ownK = white ? wK : bK;
      const int oppK = white ? bK : wK;
      const chess::bb::Bitboard oppKBB =
          (oppK >= 0) ? chess::bb::sq_bb(static_cast<chess::Square>(oppK)) : 0ULL;

      while (pass)
      {
        const int s = pop_lsb_i(pass);
        const int stop = white ? (s + 8) : (s - 8);

        const chess::bb::Bitboard frontMask = white ? M.wFront[s] : M.bFront[s];
        const chess::bb::Bitboard stopBB =
            (stop >= 0 && stop < 64) ? chess::bb::sq_bb(static_cast<chess::Square>(stop)) : 0ULL;

        int mgB = 0, egB = 0;

        if (stopBB && (occ & stopBB))
        {
          mgB -= PASS_BLOCK;
          egB -= PASS_BLOCK;
        }

        if ((frontMask & occ) == 0ULL)
        {
          mgB += PASS_FREE;
          egB += PASS_FREE;
        }

        if (ownK >= 0 && king_chebyshev(ownK, s) <= PASS_KBOOST_DIST_MAX)
        {
          mgB += PASS_KBOOST;
          egB += PASS_KBOOST;
        }

        if (oppKBB & (frontMask | stopBB))
        {
          mgB -= PASS_KBLOCK;
          egB -= PASS_KBLOCK;
        }

        if (oppK >= 0)
        {
          const int dist = king_chebyshev(oppK, s);
          const int prox = std::max(0, PASS_KPROX_DIST_MAX - dist) * PASS_KPROX;
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
    int mg = 0, eg = 0;
  };

  static AttInfo mobility(chess::bb::Bitboard occ, chess::bb::Bitboard wocc, chess::bb::Bitboard bocc,
                          const std::array<chess::bb::Bitboard, 6> &W, const std::array<chess::bb::Bitboard, 6> &B,
                          chess::bb::Bitboard wPA, chess::bb::Bitboard bPA,
                          int wK, int bK,
                          chess::bb::Bitboard wPinned,
                          chess::bb::Bitboard bPinned,
                          AttackMap &A)
  {
    AttInfo ai{};

    A.wAll = A.bAll = 0ULL;
    A.w2 = A.b2 = 0ULL;
    A.wPA = wPA;
    A.bPA = bPA;
    A.wKAtt = (wK >= 0) ? chess::bb::king_attacks_from((chess::Square)wK) : 0ULL;
    A.bKAtt = (bK >= 0) ? chess::bb::king_attacks_from((chess::Square)bK) : 0ULL;
    A.wN = A.wB = A.wR = A.wQ = 0ULL;
    A.bN = A.bB = A.bR = A.bQ = 0ULL;
    A.wKingAttackers = A.bKingAttackers = 0;
    A.wKingRingUnits = A.bKingRingUnits = 0;

    // Start attackedBy2 with king/pawn overlaps, including double pawn attacks
    A.w2 = white_double_pawn_attacks(W[(int)chess::PieceType::Pawn]) | (A.wPA & A.wKAtt);
    A.b2 = black_double_pawn_attacks(B[(int)chess::PieceType::Pawn]) | (A.bPA & A.bKAtt);

    chess::bb::Bitboard wAllAgg = A.wPA | A.wKAtt;
    chess::bb::Bitboard bAllAgg = A.bPA | A.bKAtt;

    const chess::bb::Bitboard bKingBB =
        (bK >= 0) ? chess::bb::sq_bb(static_cast<chess::Square>(bK)) : 0ULL;
    const chess::bb::Bitboard wKingBB =
        (wK >= 0) ? chess::bb::sq_bb(static_cast<chess::Square>(wK)) : 0ULL;

    const chess::bb::Bitboard safeMaskW = ~wocc & ~bPA & ~bKingBB;
    const chess::bb::Bitboard safeMaskB = ~bocc & ~wPA & ~wKingBB;

    const chess::bb::Bitboard whiteRing = (wK >= 0) ? M.kingRing[wK] : 0ULL;
    const chess::bb::Bitboard blackRing = (bK >= 0) ? M.kingRing[bK] : 0ULL;

    auto add_white_pressure = [&](chess::bb::Bitboard a, int unit)
    {
      if (bK >= 0 && (a & blackRing))
      {
        ++A.wKingAttackers;
        A.wKingRingUnits += chess::bb::popcount(a & blackRing) * unit;
      }
    };

    auto add_black_pressure = [&](chess::bb::Bitboard a, int unit)
    {
      if (wK >= 0 && (a & whiteRing))
      {
        ++A.bKingAttackers;
        A.bKingRingUnits += chess::bb::popcount(a & whiteRing) * unit;
      }
    };

    auto add_white_attacks = [&](chess::bb::Bitboard a, chess::bb::Bitboard &slot, int unit)
    {
      A.w2 |= wAllAgg & a;
      slot |= a;
      A.wAll |= a;
      wAllAgg |= a;
      add_white_pressure(a, unit);
    };

    auto add_black_attacks = [&](chess::bb::Bitboard a, chess::bb::Bitboard &slot, int unit)
    {
      A.b2 |= bAllAgg & a;
      slot |= a;
      A.bAll |= a;
      bAllAgg |= a;
      add_black_pressure(a, unit);
    };

    const chess::bb::Bitboard allQueens =
        W[(int)chess::PieceType::Queen] | B[(int)chess::PieceType::Queen];

    const chess::bb::Bitboard wBishopOcc = occ ^ allQueens;
    const chess::bb::Bitboard bBishopOcc = occ ^ allQueens;

    const chess::bb::Bitboard wRookOcc =
        occ ^ allQueens ^ W[(int)chess::PieceType::Rook];

    const chess::bb::Bitboard bRookOcc =
        occ ^ allQueens ^ B[(int)chess::PieceType::Rook];

    // --- White knights ---
    {
      chess::bb::Bitboard bb = W[(int)chess::PieceType::Knight];
      while (bb)
      {
        const int s = pop_lsb_i(bb);
        const chess::bb::Bitboard fromBB = chess::bb::sq_bb((chess::Square)s);

        chess::bb::Bitboard a = chess::bb::knight_attacks_from((chess::Square)s);

        if (wPinned & fromBB)
          a &= LINE_MASK[wK][s]; // becomes 0 for a pinned knight

        if (!a)
          continue;

        add_white_attacks(a, A.wN, KS_UNIT_N);

        int c = chess::bb::popcount(a & safeMaskW);
        if (c > 8)
          c = 8;
        ai.mg += KN_MOB_MG[c];
        ai.eg += KN_MOB_EG[c];
      }
    }

    // --- Black knights ---
    {
      chess::bb::Bitboard bb = B[(int)chess::PieceType::Knight];
      while (bb)
      {
        const int s = pop_lsb_i(bb);
        const chess::bb::Bitboard fromBB = chess::bb::sq_bb((chess::Square)s);

        chess::bb::Bitboard a = chess::bb::knight_attacks_from((chess::Square)s);

        if (bPinned & fromBB)
          a &= LINE_MASK[bK][s];

        if (!a)
          continue;

        add_black_attacks(a, A.bN, KS_UNIT_N);

        int c = chess::bb::popcount(a & safeMaskB);
        if (c > 8)
          c = 8;
        ai.mg -= KN_MOB_MG[c];
        ai.eg -= KN_MOB_EG[c];
      }
    }

    // --- White bishops ---
    {
      chess::bb::Bitboard bb = W[(int)chess::PieceType::Bishop];
      while (bb)
      {
        const int s = pop_lsb_i(bb);
        const chess::bb::Bitboard fromBB = chess::bb::sq_bb((chess::Square)s);

        chess::bb::Bitboard a =
            chess::magic::sliding_attacks(chess::magic::Slider::Bishop, (chess::Square)s, wBishopOcc);

        if (wPinned & fromBB)
          a &= LINE_MASK[wK][s];

        if (!a)
          continue;

        add_white_attacks(a, A.wB, KS_UNIT_B);

        int c = chess::bb::popcount(a & safeMaskW);
        if (c > 13)
          c = 13;
        ai.mg += BI_MOB_MG[c];
        ai.eg += BI_MOB_EG[c];
      }
    }

    // --- Black bishops ---
    {
      chess::bb::Bitboard bb = B[(int)chess::PieceType::Bishop];
      while (bb)
      {
        const int s = pop_lsb_i(bb);
        const chess::bb::Bitboard fromBB = chess::bb::sq_bb((chess::Square)s);

        chess::bb::Bitboard a =
            chess::magic::sliding_attacks(chess::magic::Slider::Bishop, (chess::Square)s, bBishopOcc);

        if (bPinned & fromBB)
          a &= LINE_MASK[bK][s];

        if (!a)
          continue;

        add_black_attacks(a, A.bB, KS_UNIT_B);

        int c = chess::bb::popcount(a & safeMaskB);
        if (c > 13)
          c = 13;
        ai.mg -= BI_MOB_MG[c];
        ai.eg -= BI_MOB_EG[c];
      }
    }

    // --- White rooks ---
    {
      chess::bb::Bitboard bb = W[(int)chess::PieceType::Rook];
      while (bb)
      {
        const int s = pop_lsb_i(bb);
        const chess::bb::Bitboard fromBB = chess::bb::sq_bb((chess::Square)s);

        chess::bb::Bitboard a =
            chess::magic::sliding_attacks(chess::magic::Slider::Rook, (chess::Square)s, wRookOcc);

        if (wPinned & fromBB)
          a &= LINE_MASK[wK][s];

        if (!a)
          continue;

        add_white_attacks(a, A.wR, KS_UNIT_R);

        int c = chess::bb::popcount(a & safeMaskW);
        if (c > 14)
          c = 14;
        ai.mg += RO_MOB_MG[c];
        ai.eg += RO_MOB_EG[c];
      }
    }

    // --- Black rooks ---
    {
      chess::bb::Bitboard bb = B[(int)chess::PieceType::Rook];
      while (bb)
      {
        const int s = pop_lsb_i(bb);
        const chess::bb::Bitboard fromBB = chess::bb::sq_bb((chess::Square)s);

        chess::bb::Bitboard a =
            chess::magic::sliding_attacks(chess::magic::Slider::Rook, (chess::Square)s, bRookOcc);

        if (bPinned & fromBB)
          a &= LINE_MASK[bK][s];

        if (!a)
          continue;

        add_black_attacks(a, A.bR, KS_UNIT_R);

        int c = chess::bb::popcount(a & safeMaskB);
        if (c > 14)
          c = 14;
        ai.mg -= RO_MOB_MG[c];
        ai.eg -= RO_MOB_EG[c];
      }
    }

    // --- White queens ---
    {
      chess::bb::Bitboard bb = W[(int)chess::PieceType::Queen];
      while (bb)
      {
        const int s = pop_lsb_i(bb);
        const chess::bb::Bitboard fromBB = chess::bb::sq_bb((chess::Square)s);

        chess::bb::Bitboard a =
            chess::magic::sliding_attacks(chess::magic::Slider::Rook, (chess::Square)s, occ) |
            chess::magic::sliding_attacks(chess::magic::Slider::Bishop, (chess::Square)s, occ);

        if (wPinned & fromBB)
          a &= LINE_MASK[wK][s];

        if (!a)
          continue;

        add_white_attacks(a, A.wQ, KS_UNIT_Q);

        int c = chess::bb::popcount(a & safeMaskW);
        if (c > 27)
          c = 27;
        ai.mg += QU_MOB_MG[c];
        ai.eg += QU_MOB_EG[c];
      }
    }

    // --- Black queens ---
    {
      chess::bb::Bitboard bb = B[(int)chess::PieceType::Queen];
      while (bb)
      {
        const int s = pop_lsb_i(bb);
        const chess::bb::Bitboard fromBB = chess::bb::sq_bb((chess::Square)s);

        chess::bb::Bitboard a =
            chess::magic::sliding_attacks(chess::magic::Slider::Rook, (chess::Square)s, occ) |
            chess::magic::sliding_attacks(chess::magic::Slider::Bishop, (chess::Square)s, occ);

        if (bPinned & fromBB)
          a &= LINE_MASK[bK][s];

        if (!a)
          continue;

        add_black_attacks(a, A.bQ, KS_UNIT_Q);

        int c = chess::bb::popcount(a & safeMaskB);
        if (c > 27)
          c = 27;
        ai.mg -= QU_MOB_MG[c];
        ai.eg -= QU_MOB_EG[c];
      }
    }

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

  static int threats(const std::array<chess::bb::Bitboard, chess::PIECE_TYPE_NB> &W,
                     const std::array<chess::bb::Bitboard, chess::PIECE_TYPE_NB> &B,
                     const AttackMap &A,
                     chess::bb::Bitboard wocc, chess::bb::Bitboard bocc)
  {
    int sc = 0;

    auto pawn_threat_score = [&](chess::bb::Bitboard pa,
                                 const std::array<chess::bb::Bitboard, chess::PIECE_TYPE_NB> &side)
    {
      int s = 0;
      s += chess::bb::popcount(pa & side[1]) * THR_PAWN_MINOR;
      s += chess::bb::popcount(pa & side[2]) * THR_PAWN_MINOR;
      s += chess::bb::popcount(pa & side[3]) * THR_PAWN_ROOK;
      s += chess::bb::popcount(pa & side[4]) * THR_PAWN_QUEEN;
      return s;
    };

    auto hang_score = [&](chess::bb::Bitboard h,
                          const std::array<chess::bb::Bitboard, chess::PIECE_TYPE_NB> &side)
    {
      int s = 0;
      s += chess::bb::popcount(h & side[1]) * HANG_MINOR;
      s += chess::bb::popcount(h & side[2]) * HANG_MINOR;
      s += chess::bb::popcount(h & side[3]) * HANG_ROOK;
      s += chess::bb::popcount(h & side[4]) * HANG_QUEEN;
      return s;
    };

    sc += pawn_threat_score(A.wPA, B);
    sc -= pawn_threat_score(A.bPA, W);

    chess::bb::Bitboard wDef = A.wAll | A.wPA | A.wKAtt;
    chess::bb::Bitboard bDef = A.bAll | A.bPA | A.bKAtt;

    chess::bb::Bitboard wHang = ((A.bAll | A.bPA) & wocc) & ~wDef;
    chess::bb::Bitboard bHang = ((A.wAll | A.wPA) & bocc) & ~bDef;

    sc += hang_score(bHang, B);
    sc -= hang_score(wHang, W);

    if ((A.wN | A.wB) & B[4])
      sc += MINOR_ON_QUEEN;
    if ((A.bN | A.bB) & W[4])
      sc -= MINOR_ON_QUEEN;

    return sc;
  }

  // ------------------------------------------------------
  //                    King Danger
  // ------------------------------------------------------

  struct KingStructureScore
  {
    int mg = 0;
    int eg = 0;
  };

  static int king_shelter_one(chess::bb::Bitboard ownPawns,
                              chess::bb::Bitboard enemyPawns,
                              int ksq,
                              bool white)
  {
    if (ksq < 0)
      return 0;

    const int kFile = chess::bb::file_of(ksq);
    const int kRank = chess::bb::rank_of(ksq);

    int total = 0;
    int stormRaw = 0;

    for (int df = -1; df <= 1; ++df)
    {
      const int ff = kFile + df;
      if (ff < 0 || ff > 7)
        continue;

      const int baseSq = (kRank << 3) | ff;
      const chess::bb::Bitboard mask = white ? M.frontSpanW[baseSq] : M.frontSpanB[baseSq];

      if (white)
      {
        const int nearOwnSq = lsb_i(mask & ownPawns);
        const int ownIdx =
            (nearOwnSq >= 0)
                ? 7 - clampi((nearOwnSq >> 3) - kRank, 0, 7)
                : 0;
        total += SHELTER[ownIdx];

        const int nearEnemySq = lsb_i(mask & enemyPawns);
        if (nearEnemySq >= 0)
        {
          const int nearEnemyR = nearEnemySq >> 3;
          const int edist = clampi(nearEnemyR - kRank, 0, 7);
          stormRaw += STORM[7 - edist];
        }
      }
      else
      {
        const int nearOwnSq = msb_i(mask & ownPawns);
        const int ownIdx =
            (nearOwnSq >= 0)
                ? 7 - clampi(kRank - (nearOwnSq >> 3), 0, 7)
                : 0;
        total += SHELTER[ownIdx];

        const int nearEnemySq = msb_i(mask & enemyPawns);
        if (nearEnemySq >= 0)
        {
          const int nearEnemyR = nearEnemySq >> 3;
          const int edist = clampi(kRank - nearEnemyR, 0, 7);
          stormRaw += STORM[7 - edist];
        }
      }
    }

    total -= stormRaw / SHELTER_STORM_DEN;
    return total / SHELTER_FINAL_DEN;
  }

  static int king_file_exposure_penalty_one(chess::bb::Bitboard ownPawns,
                                            chess::bb::Bitboard enemyPawns,
                                            int ksq,
                                            bool white)
  {
    if (ksq < 0)
      return 0;

    const chess::bb::Bitboard front = white ? M.frontSpanW[ksq] : M.frontSpanB[ksq];
    const bool ownOn = (front & ownPawns) != 0ULL;
    const bool oppOn = (front & enemyPawns) != 0ULL;

    if (!ownOn && !oppOn)
      return KS_OPEN_FILE;

    if (!ownOn && oppOn)
      return KS_OPEN_FILE / KS_OPEN_FILE_SEMI_DEN;

    return 0;
  }

  static KingStructureScore king_structure(const std::array<chess::bb::Bitboard, 6> &W,
                                           const std::array<chess::bb::Bitboard, 6> &B,
                                           int wK, int bK)
  {
    KingStructureScore out{};
    const chess::bb::Bitboard wp = W[0];
    const chess::bb::Bitboard bp = B[0];

    int wShelter = 0, bShelter = 0;
    int wStatic = 0, bStatic = 0;

    if (wK >= 0)
    {
      wShelter = king_shelter_one(wp, bp, wK, true);
      wStatic += wShelter;
      wStatic -= king_file_exposure_penalty_one(wp, bp, wK, true);
    }

    if (bK >= 0)
    {
      bShelter = king_shelter_one(bp, wp, bK, false);
      bStatic += bShelter;
      bStatic -= king_file_exposure_penalty_one(bp, wp, bK, false);
    }

    out.mg = wStatic - bStatic;
    out.eg = (wShelter - bShelter) / SHELTER_EG_DEN;
    return out;
  }

  static LILIA_ALWAYS_INLINE int king_danger_from_units(int units)
  {
    const int u = units - KS_DANGER_FREE;
    if (u <= 0)
      return 0;

    int score = KS_DANGER_LINEAR * u +
                (KS_DANGER_QUAD * u * u) / KS_DANGER_DIV;

    return std::min(score, KS_DANGER_CLAMP);
  }

  static int king_attack_units_one(const AttackMap &A,
                                   chess::bb::Bitboard ownOcc,
                                   int ksq,
                                   bool kingIsWhite,
                                   int safeCheckScore)
  {
    if (ksq < 0)
      return 0;

    const chess::bb::Bitboard ring = M.kingRing[ksq];
    const chess::bb::Bitboard adj = kingIsWhite ? A.wKAtt : A.bKAtt;

    const chess::bb::Bitboard ownDef =
        kingIsWhite ? (A.wAll | A.wPA | A.wKAtt)
                    : (A.bAll | A.bPA | A.bKAtt);

    const chess::bb::Bitboard enemyAll =
        kingIsWhite ? (A.bAll | A.bPA)
                    : (A.wAll | A.wPA);

    const chess::bb::Bitboard enemyAtkAll =
        kingIsWhite ? (A.bAll | A.bPA | A.bKAtt)
                    : (A.wAll | A.wPA | A.wKAtt);

    if (((enemyAll & ring) == 0ULL) && safeCheckScore == 0)
      return 0;

    const chess::bb::Bitboard weak = ring & enemyAll & ~ownDef;

    const int esc = chess::bb::popcount(adj & ~ownOcc & ~enemyAtkAll);
    const int noEscape = std::max(0, KS_ESCAPE_SAFE_CAP - esc);

    const int ringUnits = kingIsWhite ? A.bKingRingUnits : A.wKingRingUnits;
    const int attackers = kingIsWhite ? A.bKingAttackers : A.wKingAttackers;

    int units = 0;
    units += ringUnits;
    units += attackers * KS_UNIT_ATTACKER;
    units += chess::bb::popcount(weak) * KS_UNIT_WEAK;
    units += safeCheckScore / KS_UNIT_SAFE_CHECK_DEN;
    units += noEscape * KS_UNIT_NOESCAPE;

    return units;
  }

  // =============================================================================
  // Style terms
  // =============================================================================
  static int bishop_pair_term(const MaterialCounts &mc)
  {
    int s = 0;
    if (mc.B[0] >= 2)
      s += BISHOP_PAIR;
    if (mc.B[1] >= 2)
      s -= BISHOP_PAIR;
    return s;
  }

  static int bad_bishop(const std::array<chess::bb::Bitboard, chess::PIECE_TYPE_NB> &W,
                        const std::array<chess::bb::Bitboard, chess::PIECE_TYPE_NB> &B,
                        int wLightPawns, int wDarkPawns,
                        int bLightPawns, int bDarkPawns,
                        bool wClosedCenter, bool bClosedCenter)
  {
    auto is_light = [&](int sq)
    {
      return ((chess::bb::file_of(static_cast<chess::Square>(sq)) +
               chess::bb::rank_of(static_cast<chess::Square>(sq))) &
              1) != 0;
    };

    int sc = 0;

    chess::bb::Bitboard bishops = W[2];
    while (bishops)
    {
      const int s = pop_lsb_i(bishops);
      const int same = is_light(s) ? wLightPawns : wDarkPawns;
      const int pen = (same > BAD_BISHOP_SAME_COLOR_THRESHOLD)
                          ? (same - BAD_BISHOP_SAME_COLOR_THRESHOLD) * BAD_BISHOP_PER_PAWN
                          : 0;
      if (pen)
        sc -= wClosedCenter ? pen : (pen * BAD_BISHOP_OPEN_NUM / BAD_BISHOP_OPEN_DEN);
    }

    bishops = B[2];
    while (bishops)
    {
      const int s = pop_lsb_i(bishops);
      const int same = is_light(s) ? bLightPawns : bDarkPawns;
      const int pen = (same > BAD_BISHOP_SAME_COLOR_THRESHOLD)
                          ? (same - BAD_BISHOP_SAME_COLOR_THRESHOLD) * BAD_BISHOP_PER_PAWN
                          : 0;
      if (pen)
        sc += bClosedCenter ? pen : (pen * BAD_BISHOP_OPEN_NUM / BAD_BISHOP_OPEN_DEN);
    }

    return sc;
  }

  static int outposts_center(const std::array<chess::bb::Bitboard, chess::PIECE_TYPE_NB> &W,
                             const std::array<chess::bb::Bitboard, chess::PIECE_TYPE_NB> &B,
                             chess::bb::Bitboard wPA, chess::bb::Bitboard bPA,
                             chess::bb::Bitboard wHoles, chess::bb::Bitboard bHoles)
  {
    int s = 0;

    auto add_kn = [&](int sq, bool white)
    {
      const chess::bb::Bitboard bb = chess::bb::sq_bb((chess::Square)sq);
      const bool supportedByOwnPawn = white ? ((wPA & bb) != 0) : ((bPA & bb) != 0);
      const bool trueHole = white ? ((wHoles & bb) != 0) : ((bHoles & bb) != 0);

      const int r = chess::bb::rank_of(sq);
      const bool deepOutpost = white ? (r >= OUTPOST_DEEP_RANK_WHITE)
                                     : (r <= OUTPOST_DEEP_RANK_BLACK);

      if (!(supportedByOwnPawn && trueHole && deepOutpost))
        return 0;

      int add = OUTPOST_KN;

      if (bb & CENTER4)
        add += OUTPOST_CENTER_SQ_BONUS;

      if (chess::bb::knight_attacks_from((chess::Square)sq) & CENTER4)
        add += CENTER_CTRL;

      if ((white && r >= 5) || (!white && r <= 2))
        add += OUTPOST_DEEP_EXTRA;

      return add;
    };

    chess::bb::Bitboard t = W[1];
    while (t)
      s += add_kn(pop_lsb_i(t), true);

    t = B[1];
    while (t)
      s -= add_kn(pop_lsb_i(t), false);

    return s;
  }

  static int rook_activity(const std::array<chess::bb::Bitboard, chess::PIECE_TYPE_NB> &W,
                           const std::array<chess::bb::Bitboard, chess::PIECE_TYPE_NB> &B,
                           chess::bb::Bitboard wp,
                           chess::bb::Bitboard bp,
                           chess::bb::Bitboard wPass,
                           chess::bb::Bitboard bPass,
                           chess::bb::Bitboard occ)
  {
    int s = 0;
    const chess::bb::Bitboard wr = W[3];
    const chess::bb::Bitboard br = B[3];
    if (!wr && !br)
      return 0;

    auto rank = [&](int sq)
    { return chess::bb::rank_of(sq); };

    auto openScore = [&](int sq, bool white)
    {
      const chess::bb::Bitboard f = M.file[sq];
      const bool own = white ? ((f & wp) != 0) : ((f & bp) != 0);
      const bool opp = white ? ((f & bp) != 0) : ((f & wp) != 0);

      if (!own && !opp)
        return ROOK_OPEN;
      if (!own && opp)
        return ROOK_SEMI;
      return 0;
    };

    chess::bb::Bitboard t = wr;
    while (t)
    {
      const int sq = pop_lsb_i(t);
      s += openScore(sq, true);

      if (rank(sq) == 6)
      {
        const bool tgt = (B[5] & chess::bb::RANK_8) || (B[0] & chess::bb::RANK_7);
        if (tgt)
          s += ROOK_ON_7TH;
      }
    }

    t = br;
    while (t)
    {
      const int sq = pop_lsb_i(t);
      s -= openScore(sq, false);

      if (rank(sq) == 1)
      {
        const bool tgt = (W[5] & chess::bb::RANK_1) || (W[0] & chess::bb::RANK_2);
        if (tgt)
          s -= ROOK_ON_7TH;
      }
    }

    auto connected = [&](chess::bb::Bitboard rooks, chess::bb::Bitboard occAll)
    {
      if (chess::bb::popcount(rooks) != 2)
        return false;

      const int s1 = lsb_i(rooks);
      const chess::bb::Bitboard r2 = rooks & (rooks - 1);
      const int s2 = lsb_i(r2);

      if (chess::bb::file_of(s1) != chess::bb::file_of(s2) &&
          chess::bb::rank_of(s1) != chess::bb::rank_of(s2))
        return false;

      return (BETWEEN_EXCLUSIVE[s1][s2] & occAll) == 0ULL;
    };

    if (connected(wr, occ))
      s += CONNECTED_ROOKS;
    if (connected(br, occ))
      s -= CONNECTED_ROOKS;

    auto behind = [&](int rSq, int pSq, bool pawnWhite, int full, int half)
    {
      if (chess::bb::file_of(rSq) != chess::bb::file_of(pSq))
        return 0;

      if (BETWEEN_EXCLUSIVE[rSq][pSq] & occ)
        return 0;

      if (pawnWhite)
        return (chess::bb::rank_of(rSq) < chess::bb::rank_of(pSq) ? full : half);
      else
        return (chess::bb::rank_of(rSq) > chess::bb::rank_of(pSq) ? full : half);
    };

    t = wr;
    while (t)
    {
      const int rs = pop_lsb_i(t);

      chess::bb::Bitboard f = M.file[rs] & wPass;
      while (f)
      {
        const int ps = pop_lsb_i(f);
        s += behind(rs, ps, true, ROOK_BEHIND_PASSER, ROOK_BEHIND_PASSER_HALF);
      }

      f = M.file[rs] & bPass;
      while (f)
      {
        const int ps = pop_lsb_i(f);
        s += behind(rs, ps, false, ROOK_BEHIND_PASSER_HALF, ROOK_BEHIND_PASSER_THIRD);
      }
    }

    t = br;
    while (t)
    {
      const int rs = pop_lsb_i(t);

      chess::bb::Bitboard f = M.file[rs] & bPass;
      while (f)
      {
        const int ps = pop_lsb_i(f);
        s -= behind(rs, ps, false, ROOK_BEHIND_PASSER, ROOK_BEHIND_PASSER_HALF);
      }

      f = M.file[rs] & wPass;
      while (f)
      {
        const int ps = pop_lsb_i(f);
        s -= behind(rs, ps, true, ROOK_BEHIND_PASSER_HALF, ROOK_BEHIND_PASSER_THIRD);
      }
    }

    return s;
  }

  static int rook_endgame_extras_eg(const std::array<chess::bb::Bitboard, chess::PIECE_TYPE_NB> &W,
                                    const std::array<chess::bb::Bitboard, chess::PIECE_TYPE_NB> &B,
                                    chess::bb::Bitboard occ,
                                    const MaterialCounts &mc,
                                    int wK, int bK)
  {
    const chess::bb::Bitboard wr = W[3];
    const chess::bb::Bitboard br = B[3];

    const bool rookEnd =
        (mc.R[0] == 1 && mc.R[1] == 1 &&
         mc.N[0] == 0 && mc.N[1] == 0 &&
         mc.B[0] == 0 && mc.B[1] == 0 &&
         mc.Q[0] == 0 && mc.Q[1] == 0 &&
         (mc.P[0] | mc.P[1]) != 0);

    if (!rookEnd || wK < 0 || bK < 0)
      return 0;

    int eg = 0;

    auto cut_by = [&](chess::bb::Bitboard rook, int targetK, int sign)
    {
      if (!rook)
        return;

      const int rsq = lsb_i(rook);

      if (chess::bb::file_of(rsq) == chess::bb::file_of(targetK))
      {
        if ((BETWEEN_EXCLUSIVE[rsq][targetK] & occ) != 0ULL)
          return;

        const int diff = std::abs(chess::bb::rank_of(rsq) - chess::bb::rank_of(targetK));
        if (diff >= ROOK_CUT_MIN_SEPARATION)
          eg += sign * ROOK_CUT_BONUS;
      }
      else if (chess::bb::rank_of(rsq) == chess::bb::rank_of(targetK))
      {
        if ((BETWEEN_EXCLUSIVE[rsq][targetK] & occ) != 0ULL)
          return;

        const int diff = std::abs(chess::bb::file_of(rsq) - chess::bb::file_of(targetK));
        if (diff >= ROOK_CUT_MIN_SEPARATION)
          eg += sign * ROOK_CUT_BONUS;
      }
    };

    cut_by(wr, bK, +1);
    cut_by(br, wK, -1);

    return eg;
  }

  static inline int kdist_cheb(int a, int b)
  {
    if (a < 0 || b < 0)
      return 7;
    int df = std::abs((a & 7) - (b & 7));
    int dr = std::abs((a >> 3) - (b >> 3));
    return std::max(df, dr);
  }

  // =============================================================================
  // Endgame scalers
  // =============================================================================
  static int endgame_scale(const std::array<chess::bb::Bitboard, chess::PIECE_TYPE_NB> &W,
                           const std::array<chess::bb::Bitboard, chess::PIECE_TYPE_NB> &B,
                           const MaterialCounts &mc,
                           int wK, int bK)
  {
    const int wP = mc.P[0], bP = mc.P[1];
    const int wN = mc.N[0], bN = mc.N[1];
    const int wB = mc.B[0], bB = mc.B[1];
    const int wR = mc.R[0], bR = mc.R[1];
    const int wQ = mc.Q[0], bQ = mc.Q[1];

    auto on_fileA = [&](chess::bb::Bitboard paw)
    { return (paw & M.file[0]) != 0; };

    auto on_fileH = [&](chess::bb::Bitboard paw)
    { return (paw & M.file[7]) != 0; };

    auto is_corner_pawn = [&](chess::bb::Bitboard paw)
    { return on_fileA(paw) || on_fileH(paw); };

    auto square_is_light = [&](int sq)
    {
      return ((chess::bb::file_of(static_cast<chess::Square>(sq)) +
               chess::bb::rank_of(static_cast<chess::Square>(sq))) &
              1) != 0;
    };

    // Exact no-pawn minor endings should be handled before bishop-only heuristics.
    if (wP == 0 && bP == 0 && wR == 0 && bR == 0 && wQ == 0 && bQ == 0)
    {
      const int wMin = wN + wB;
      const int bMin = bN + bB;

      if (wMin <= NO_PAWNS_DRAW_MAX_MINORS_PER_SIDE &&
          bMin <= NO_PAWNS_DRAW_MAX_MINORS_PER_SIDE)
        return SCALE_DRAW;

      if ((wN == TWO_KNIGHTS_DRAWISH_COUNT && wB == 0 && bMin == 0) ||
          (bN == TWO_KNIGHTS_DRAWISH_COUNT && bB == 0 && wMin == 0))
        return SCALE_VERY_DRAWISH;

      if ((wMin == SAME_TYPE_MINOR_DRAWISH_COUNT &&
           bMin == SAME_TYPE_MINOR_DRAWISH_COUNT) &&
          ((wN == bN) || (wB == bB)))
        return SCALE_VERY_DRAWISH;
    }

    auto rook_pawn_only_scale = [&](bool white) -> int
    {
      const int ownP = white ? wP : bP;
      const int oppP = white ? bP : wP;
      const int atkN = white ? wN : bN;
      const int oppN = white ? bN : wN;
      const int atkB = white ? wB : bB;
      const int oppB = white ? bB : wB;
      const int atkR = white ? wR : bR;
      const int oppR = white ? bR : wR;
      const int atkQ = white ? wQ : bQ;
      const int oppQ = white ? bQ : wQ;

      if (ownP != 1 || oppP != 0)
        return FULL_SCALE;
      if ((atkN + atkB + atkR + atkQ + oppN + oppB + oppR + oppQ) != 0)
        return FULL_SCALE;

      const chess::bb::Bitboard paw = white ? W[0] : B[0];
      if (!is_corner_pawn(paw))
        return FULL_SCALE;

      const int pSq = lsb_i(paw);
      const int promoSq = white
                              ? (on_fileA(paw) ? chess::bb::A8 : chess::bb::H8)
                              : (on_fileA(paw) ? chess::bb::A1 : chess::bb::H1);

      const int ownKsq = white ? wK : bK;
      const int oppKsq = white ? bK : wK;
      const int advance = white
                              ? chess::bb::rank_of(static_cast<chess::Square>(pSq))
                              : (7 - chess::bb::rank_of(static_cast<chess::Square>(pSq)));

      if (oppKsq == promoSq && advance >= 5)
      {
        const int ownKDist = kdist_cheb(ownKsq, promoSq);
        if (ownKDist > 1)
          return SCALE_DRAW;
        if (ownKDist > 0)
          return SCALE_VERY_DRAWISH;
      }

      return FULL_SCALE;
    };

    auto wrong_bishop_scale = [&](bool white) -> int
    {
      const int atkN = white ? wN : bN;
      const int atkB = white ? wB : bB;
      const int atkR = white ? wR : bR;
      const int atkQ = white ? wQ : bQ;
      const int ownP = white ? wP : bP;

      const int oppP = white ? bP : wP;
      const int oppN = white ? bN : wN;
      const int oppB = white ? bB : wB;
      const int oppR = white ? bR : wR;
      const int oppQ = white ? bQ : wQ;

      if (atkB != 1 || ownP != 1)
        return FULL_SCALE;
      if ((atkN + atkR + atkQ) != 0)
        return FULL_SCALE;
      if ((oppP + oppN + oppB + oppR + oppQ) != 0)
        return FULL_SCALE;

      const chess::bb::Bitboard paw = white ? W[0] : B[0];
      if (!is_corner_pawn(paw))
        return FULL_SCALE;

      const int bishopSq = lsb_i(white ? W[2] : B[2]);
      const int promoSq = white
                              ? (on_fileA(paw) ? chess::bb::A8 : chess::bb::H8)
                              : (on_fileA(paw) ? chess::bb::A1 : chess::bb::H1);

      const bool rightBishop = (square_is_light(bishopSq) == square_is_light(promoSq));
      if (rightBishop)
        return FULL_SCALE;

      const int oppKsq = white ? bK : wK;
      const int d = kdist_cheb(oppKsq, promoSq);

      if (d <= 1)
        return SCALE_DRAW;
      if (d <= 2)
        return SCALE_VERY_DRAWISH;
      return SCALE_MEDIUM;
    };

    {
      const int s = rook_pawn_only_scale(true);
      if (s != FULL_SCALE)
        return s;
    }
    {
      const int s = rook_pawn_only_scale(false);
      if (s != FULL_SCALE)
        return s;
    }

    {
      const int s = wrong_bishop_scale(true);
      if (s != FULL_SCALE)
        return s;
    }
    {
      const int s = wrong_bishop_scale(false);
      if (s != FULL_SCALE)
        return s;
    }

    auto rook_pawn_corner_dist = [&](chess::bb::Bitboard pawns, int enemyK, bool white) -> int
    {
      int best = 99;
      if (pawns & chess::bb::FILE_A)
        best = std::min(best, kdist_cheb(enemyK, white ? chess::bb::A8 : chess::bb::A1));
      if (pawns & chess::bb::FILE_H)
        best = std::min(best, kdist_cheb(enemyK, white ? chess::bb::H8 : chess::bb::H1));
      return best;
    };

    if (wR == 1 && bR == 1 &&
        wN == 0 && wB == 0 && wQ == 0 &&
        bN == 0 && bB == 0 && bQ == 0 &&
        (wP <= SIDEPAWN_ROOK_MAX_PAWNS) && only_rook_pawns(W[0]) && bP == 0)
    {
      const int d = rook_pawn_corner_dist(W[0], bK, true);
      return (d <= SIDEPAWN_ROOK_DRAW_CHEB_DIST ? SCALE_VERY_DRAWISH : SCALE_REDUCED);
    }

    if (bR == 1 && wR == 1 &&
        bN == 0 && bB == 0 && bQ == 0 &&
        wN == 0 && wB == 0 && wQ == 0 &&
        (bP <= SIDEPAWN_ROOK_MAX_PAWNS) && only_rook_pawns(B[0]) && wP == 0)
    {
      const int d = rook_pawn_corner_dist(B[0], wK, false);
      return (d <= SIDEPAWN_ROOK_DRAW_CHEB_DIST ? SCALE_VERY_DRAWISH : SCALE_REDUCED);
    }

    if (wN == 1 && wB == 0 && wR == 0 && wQ == 0 &&
        wP == 1 && is_corner_pawn(W[0]) &&
        bN == 0 && bB == 0 && bR == 0 && bQ == 0 && bP == 0)
      return KN_CORNER_PAWN_SCALE;

    if (bN == 1 && bB == 0 && bR == 0 && bQ == 0 &&
        bP == 1 && is_corner_pawn(B[0]) &&
        wN == 0 && wB == 0 && wR == 0 && wQ == 0 && wP == 0)
      return KN_CORNER_PAWN_SCALE;

    return FULL_SCALE;
  }

  static int pawn_file_asymmetry(chess::bb::Bitboard wp, chess::bb::Bitboard bp)
  {
    int asym = 0;
    for (int f = 0; f < 8; ++f)
    {
      const bool w = (wp & M.file[f]) != 0;
      const bool b = (bp & M.file[f]) != 0;
      asym += (w != b);
    }
    return asym;
  }

  static int initiative_complexity(const MaterialCounts &mc,
                                   chess::bb::Bitboard wp,
                                   chess::bb::Bitboard bp,
                                   chess::bb::Bitboard wPass,
                                   chess::bb::Bitboard bPass,
                                   int eg)
  {
    if (eg == 0)
      return 0;

    int complexity = INIT_COMPLEXITY_BASE;
    complexity += (mc.P[0] + mc.P[1]) * INIT_COMPLEXITY_PAWNS;
    complexity += chess::bb::popcount(wPass | bPass) * INIT_COMPLEXITY_PASSERS;
    complexity += pawn_file_asymmetry(wp, bp) * INIT_COMPLEXITY_ASYMMETRY;
    complexity += (pawns_on_both_wings(wp | bp) ? INIT_COMPLEXITY_WINGS : 0);

    if (complexity <= 0)
      return 0;

    // Never flips the endgame sign.
    return sgn(eg) * std::min(std::abs(eg), complexity);
  }

  LILIA_ALWAYS_INLINE chess::bb::Bitboard pinned_blockers(
      chess::bb::Bitboard occ,
      chess::bb::Bitboard own,
      chess::bb::Bitboard oppBQ,
      chess::bb::Bitboard oppRQ,
      int ksq)
  {
    if (ksq < 0)
      return 0ULL;

    const chess::Square kingSq = static_cast<chess::Square>(ksq);
    chess::bb::Bitboard pinned = 0ULL;

    chess::bb::Bitboard pinners =
        chess::magic::sliding_attacks(chess::magic::Slider::Rook, kingSq, occ ^ own) & oppRQ;

    while (pinners)
    {
      const int s = pop_lsb_i(pinners);
      const chess::bb::Bitboard b = BETWEEN_EXCLUSIVE[ksq][s] & own;
      if (b && !(b & (b - 1)))
        pinned |= b;
    }

    pinners =
        chess::magic::sliding_attacks(chess::magic::Slider::Bishop, kingSq, occ ^ own) & oppBQ;

    while (pinners)
    {
      const int s = pop_lsb_i(pinners);
      const chess::bb::Bitboard b = BETWEEN_EXCLUSIVE[ksq][s] & own;
      if (b && !(b & (b - 1)))
        pinned |= b;
    }

    return pinned;
  }

  // =============================================================================
  // Eval caches
  // =============================================================================
  constexpr size_t PAWN_BITS = 15;
  constexpr size_t PAWN_SIZE = 1ULL << PAWN_BITS;

  struct PawnEntry
  {
    uint64_t key = 0;
    uint32_t generation = 0;
    uint32_t _pad0 = 0;

    int32_t mg = 0;
    int32_t eg = 0;
    int32_t lever = 0;

    uint8_t wLightPawns = 0;
    uint8_t wDarkPawns = 0;
    uint8_t bLightPawns = 0;
    uint8_t bDarkPawns = 0;
    uint8_t wClosedCenter = 0;
    uint8_t bClosedCenter = 0;
    uint16_t _pad1 = 0;

    uint64_t wPA = 0;
    uint64_t bPA = 0;
    uint64_t wPass = 0;
    uint64_t bPass = 0;
    uint64_t wHoles = 0;
    uint64_t bHoles = 0;
  };

  struct Evaluator::Impl
  {
    uint32_t pawnGeneration = 1;
    std::array<PawnEntry, PAWN_SIZE> pawn{};
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

    ++m_impl->pawnGeneration;

    if (m_impl->pawnGeneration == 0)
    {
      m_impl->pawnGeneration = 1;
      for (auto &e : m_impl->pawn)
      {
        e.key = 0;
        e.generation = 0;
      }
    }
  }

  static LILIA_ALWAYS_INLINE size_t idx_pawn(uint64_t k)
  {
    return (size_t)k & (PAWN_SIZE - 1);
  }

  LILIA_ALWAYS_INLINE chess::bb::Bitboard rook_pins_from_kingray(chess::bb::Bitboard occ,
                                                                 chess::bb::Bitboard own,
                                                                 chess::bb::Bitboard oppRQ,
                                                                 int ksq,
                                                                 chess::bb::Bitboard kingRookRay)
  {
    if (ksq < 0)
      return 0ULL;

    chess::bb::Bitboard pins = 0ULL;
    chess::bb::Bitboard blockers = kingRookRay & own;

    while (blockers)
    {
      const int b = pop_lsb_i(blockers);

      const int df = sgn(chess::bb::file_of(b) - chess::bb::file_of(ksq));
      const int dr = sgn(chess::bb::rank_of(b) - chess::bb::rank_of(ksq));

      int f = chess::bb::file_of(b);
      int r = chess::bb::rank_of(b);

      for (;;)
      {
        f += df;
        r += dr;
        if (f < 0 || f > 7 || r < 0 || r > 7)
          break;

        const int s = (r << 3) | f;
        const chess::bb::Bitboard bb = chess::bb::sq_bb((chess::Square)s);

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

  LILIA_ALWAYS_INLINE chess::bb::Bitboard bishop_pins_from_kingray(chess::bb::Bitboard occ,
                                                                   chess::bb::Bitboard own,
                                                                   chess::bb::Bitboard oppBQ,
                                                                   int ksq,
                                                                   chess::bb::Bitboard kingBishopRay)
  {
    if (ksq < 0)
      return 0ULL;

    chess::bb::Bitboard pins = 0ULL;
    chess::bb::Bitboard blockers = kingBishopRay & own;

    while (blockers)
    {
      const int b = pop_lsb_i(blockers);

      const int df = sgn(chess::bb::file_of(b) - chess::bb::file_of(ksq));
      const int dr = sgn(chess::bb::rank_of(b) - chess::bb::rank_of(ksq));

      int f = chess::bb::file_of(b);
      int r = chess::bb::rank_of(b);

      for (;;)
      {
        f += df;
        r += dr;
        if (f < 0 || f > 7 || r < 0 || r > 7)
          break;

        const int s = (r << 3) | f;
        const chess::bb::Bitboard bb = chess::bb::sq_bb((chess::Square)s);

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

  LILIA_ALWAYS_INLINE int safe_checks(bool white,
                                      chess::bb::Bitboard occ,
                                      chess::bb::Bitboard wocc,
                                      chess::bb::Bitboard bocc,
                                      chess::bb::Bitboard wQueens,
                                      chess::bb::Bitboard bQueens,
                                      const AttackMap &A,
                                      int oppK) noexcept
  {
    if (LILIA_UNLIKELY(oppK < 0))
      return 0;

    const chess::bb::Bitboard atkOcc = white ? wocc : bocc;
    const chess::bb::Bitboard defQOcc = white ? bQueens : wQueens;

    const chess::bb::Bitboard atkAll = white ? (A.wAll | A.wPA | A.wKAtt)
                                             : (A.bAll | A.bPA | A.bKAtt);
    const chess::bb::Bitboard defAll = white ? (A.bAll | A.bPA | A.bKAtt)
                                             : (A.wAll | A.wPA | A.wKAtt);

    const chess::bb::Bitboard atk2 = white ? A.w2 : A.b2;
    const chess::bb::Bitboard def2 = white ? A.b2 : A.w2;

    const chess::bb::Bitboard atkN = white ? A.wN : A.bN;
    const chess::bb::Bitboard atkB = white ? A.wB : A.bB;
    const chess::bb::Bitboard atkR = white ? A.wR : A.bR;
    const chess::bb::Bitboard atkQ = white ? A.wQ : A.bQ;

    const chess::bb::Bitboard defQAtt = white ? A.bQ : A.wQ;
    const chess::bb::Bitboard defKAtt = white ? A.bKAtt : A.wKAtt;

    const chess::Square ksq = static_cast<chess::Square>(oppK);

    // attacked by attacker, not defended twice, and either undefended or
    // only queen/king-defended.
    const chess::bb::Bitboard weak =
        atkAll & ~def2 & (~defAll | defKAtt | defQAtt);

    // Safe checking squares:
    // not occupied by attacker, and either undefended by defender or weak+attacked twice.
    chess::bb::Bitboard safe = ~atkOcc;
    safe &= (~defAll) | (weak & atk2);

    chess::bb::Bitboard rookMask = 0ULL;
    chess::bb::Bitboard bishopMask = 0ULL;

    // Remove defender queen from occupancy
    if ((atkR | atkQ) != 0ULL)
      rookMask = chess::magic::sliding_attacks(
          chess::magic::Slider::Rook, ksq, occ ^ defQOcc);

    if ((atkB | atkQ) != 0ULL)
      bishopMask = chess::magic::sliding_attacks(
          chess::magic::Slider::Bishop, ksq, occ ^ defQOcc);

    const chess::bb::Bitboard rookChecks =
        rookMask & atkR & safe;

    const chess::bb::Bitboard queenChecks =
        (rookMask | bishopMask) & atkQ & safe & ~defQAtt & ~rookChecks;

    const chess::bb::Bitboard bishopChecks =
        bishopMask & atkB & safe & ~queenChecks;

    const chess::bb::Bitboard knightChecks =
        chess::bb::knight_attacks_from(ksq) & atkN & safe;

    int sc = 0;
    sc += KS_SAFE_CHECK_R * chess::bb::popcount(rookChecks);
    sc += KS_SAFE_CHECK_QR * chess::bb::popcount(queenChecks & rookMask);
    sc += KS_SAFE_CHECK_QB * chess::bb::popcount(queenChecks & bishopMask);
    sc += KS_SAFE_CHECK_B * chess::bb::popcount(bishopChecks);
    sc += KS_SAFE_CHECK_N * chess::bb::popcount(knightChecks);
    return sc;
  }

  static int king_attack_danger(chess::bb::Bitboard occ,
                                chess::bb::Bitboard wocc,
                                chess::bb::Bitboard bocc,
                                const std::array<chess::bb::Bitboard, chess::PIECE_TYPE_NB> &W,
                                const std::array<chess::bb::Bitboard, chess::PIECE_TYPE_NB> &B,
                                const AttackMap &A,
                                int wK, int bK)
  {
    const bool whiteHasAttackPieces = (W[1] | W[2] | W[3] | W[4]) != 0ULL;
    const bool blackHasAttackPieces = (B[1] | B[2] | B[3] | B[4]) != 0ULL;

    if (!whiteHasAttackPieces && !blackHasAttackPieces)
      return 0;

    const int safeW =
        (whiteHasAttackPieces && bK >= 0)
            ? safe_checks(true, occ, wocc, bocc, W[4], B[4], A, bK)
            : 0;

    const int safeB =
        (blackHasAttackPieces && wK >= 0)
            ? safe_checks(false, occ, wocc, bocc, W[4], B[4], A, wK)
            : 0;

    const bool whiteKingUnderPressure =
        blackHasAttackPieces &&
        ((A.bKingAttackers != 0) || (A.bKingRingUnits != 0) || safeB != 0);

    const bool blackKingUnderPressure =
        whiteHasAttackPieces &&
        ((A.wKingAttackers != 0) || (A.wKingRingUnits != 0) || safeW != 0);

    const int wUnits =
        whiteKingUnderPressure ? king_attack_units_one(A, wocc, wK, true, safeB) : 0;

    const int bUnits =
        blackKingUnderPressure ? king_attack_units_one(A, bocc, bK, false, safeW) : 0;

    return king_danger_from_units(bUnits) - king_danger_from_units(wUnits);
  }

  // =============================================================================
  // evaluate() – white POV
  // =============================================================================
  int Evaluator::evaluate(const SearchPosition &pos) const
  {
    const chess::Board &b = pos.getBoard();
    uint64_t pKey = (uint64_t)pos.getState().pawnKey;

    const size_t pIdx = idx_pawn(pKey);
    prefetch_ro(&m_impl->pawn[pIdx]);

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

    const bool anyKnights = (mc.N[0] | mc.N[1]) != 0;
    const bool anyBishops = (mc.B[0] | mc.B[1]) != 0;
    const bool anyRooks = (mc.R[0] | mc.R[1]) != 0;
    const bool anyQueens = (mc.Q[0] | mc.Q[1]) != 0;

    // Exact dead draw: KR vs KR
    if (mc.P[0] == 0 && mc.P[1] == 0 &&
        mc.N[0] == 0 && mc.N[1] == 0 &&
        mc.B[0] == 0 && mc.B[1] == 0 &&
        mc.Q[0] == 0 && mc.Q[1] == 0 &&
        mc.R[0] == 1 && mc.R[1] == 1)
      return 0;

    // --- Pawn hash: pawn-only structure + cached PA / passers ---
    int pMG = 0, pEG = 0;
    int lever = 0;
    chess::bb::Bitboard wPA = 0, bPA = 0, wPass = 0, bPass = 0;
    chess::bb::Bitboard wHoles = 0, bHoles = 0;

    int wLightPawns = 0, wDarkPawns = 0;
    int bLightPawns = 0, bDarkPawns = 0;
    bool wClosedCenter = false, bClosedCenter = false;
    const bool openingish = curPhase >= OPENING_PHASE_MIN;
    const int wMinorCnt = mc.N[0] + mc.B[0];
    const int bMinorCnt = mc.N[1] + mc.B[1];

    {
      PawnEntry &ps = m_impl->pawn[pIdx];
      const uint32_t gen = m_impl->pawnGeneration;

      if (ps.key == pKey && ps.generation == gen)
      {
        pMG = ps.mg;
        pEG = ps.eg;
        lever = ps.lever;

        wPA = static_cast<chess::bb::Bitboard>(ps.wPA);
        bPA = static_cast<chess::bb::Bitboard>(ps.bPA);
        wPass = static_cast<chess::bb::Bitboard>(ps.wPass);
        bPass = static_cast<chess::bb::Bitboard>(ps.bPass);
        wHoles = static_cast<chess::bb::Bitboard>(ps.wHoles);
        bHoles = static_cast<chess::bb::Bitboard>(ps.bHoles);

        wLightPawns = ps.wLightPawns;
        wDarkPawns = ps.wDarkPawns;
        bLightPawns = ps.bLightPawns;
        bDarkPawns = ps.bDarkPawns;
        wClosedCenter = ps.wClosedCenter != 0;
        bClosedCenter = ps.bClosedCenter != 0;
      }
      else
      {
        wPA = chess::bb::white_pawn_attacks(W[0]);
        bPA = chess::bb::black_pawn_attacks(B[0]);

        const PawnOnly po = pawn_structure_pawnhash_only(W[0], B[0], wPA, bPA);

        pMG = po.mg;
        pEG = po.eg;
        lever = po.lever;
        wPass = po.wPass;
        bPass = po.bPass;
        wHoles = po.wHoles;
        bHoles = po.bHoles;

        wLightPawns = po.wLightPawns;
        wDarkPawns = po.wDarkPawns;
        bLightPawns = po.bLightPawns;
        bDarkPawns = po.bDarkPawns;
        wClosedCenter = po.wClosedCenter;
        bClosedCenter = po.bClosedCenter;

        ps.key = pKey;
        ps.generation = gen;

        ps.mg = pMG;
        ps.eg = pEG;
        ps.lever = lever;

        ps.wPA = static_cast<uint64_t>(wPA);
        ps.bPA = static_cast<uint64_t>(bPA);
        ps.wPass = static_cast<uint64_t>(wPass);
        ps.bPass = static_cast<uint64_t>(bPass);
        ps.wHoles = static_cast<uint64_t>(wHoles);
        ps.bHoles = static_cast<uint64_t>(bHoles);

        ps.wLightPawns = static_cast<uint8_t>(wLightPawns);
        ps.wDarkPawns = static_cast<uint8_t>(wDarkPawns);
        ps.bLightPawns = static_cast<uint8_t>(bLightPawns);
        ps.bDarkPawns = static_cast<uint8_t>(bDarkPawns);
        ps.wClosedCenter = static_cast<uint8_t>(wClosedCenter);
        ps.bClosedCenter = static_cast<uint8_t>(bClosedCenter);
      }
    }

    // material-dependent gates
    const bool queensOn = anyQueens;

    const bool whiteHasAttackPieces = (W[1] | W[2] | W[3] | W[4]) != 0ULL;
    const bool blackHasAttackPieces = (B[1] | B[2] | B[3] | B[4]) != 0ULL;

    // Pins must be ready before attack generation, because attack generation
    // is where legality gets enforced now.
    const bool needWhitePins =
        wK >= 0 && whiteHasAttackPieces && (B[2] | B[3] | B[4]);

    const bool needBlackPins =
        bK >= 0 && blackHasAttackPieces && (W[2] | W[3] | W[4]);

    chess::bb::Bitboard wPinned = 0ULL, bPinned = 0ULL;

    if (needWhitePins)
      wPinned = pinned_blockers(occ, wocc, (B[2] | B[4]), (B[3] | B[4]), wK);

    if (needBlackPins)
      bPinned = pinned_blockers(occ, bocc, (W[2] | W[4]), (W[3] | W[4]), bK);

    AttackMap A{};
    AttInfo att = mobility(occ, wocc, bocc, W, B, wPA, bPA, wK, bK,
                           wPinned, bPinned, A);

    // threats
    int thr = threats(W, B, A, wocc, bocc);

    const KingStructureScore kingStruct = king_structure(W, B, wK, bK);

    int kDanger = king_attack_danger(occ, wocc, bocc, W, B, A, wK, bK);

    // style & structure
    int bp = bishop_pair_term(mc);
    int badB = anyBishops
                   ? bad_bishop(W, B,
                                wLightPawns, wDarkPawns,
                                bLightPawns, bDarkPawns,
                                wClosedCenter, bClosedCenter)
                   : 0;
    int outp = anyKnights ? outposts_center(W, B, wPA, bPA, wHoles, bHoles) : 0;
    int ract = anyRooks ? rook_activity(W, B, W[0], B[0], wPass, bPass, occ) : 0;
    int spc = (openingish && (wClosedCenter || bClosedCenter) && (wMinorCnt + bMinorCnt >= 4))
                  ? space_term(wocc, bocc, wPA, bPA, wMinorCnt, bMinorCnt)
                  : 0;

    // material imbalance
    int imb = material_imbalance(mc);

    // KS mixing
    const int ksMulMG = queensOn ? KS_MIX_MG_Q_ON : KS_MIX_MG_Q_OFF;
    int ksMG = kDanger * ksMulMG / 100;
    ksMG = std::clamp(ksMG, -KS_MG_CLAMP, KS_MG_CLAMP);

    // Accumulate MG/EG
    int mg_add = 0, eg_add = 0;

    // rook activity (EG lighter)
    mg_add += ract;
    eg_add += ract / ROOK_ACTIVITY_EG_DEN;

    // space
    mg_add += spc;
    eg_add += spc / SPACE_EG_DEN;

    // outposts
    mg_add += outp;
    eg_add += outp / OUTPOST_EG_DEN;

    // pawn-only (from TT)
    mg_add += pMG;
    eg_add += pEG;

    // king structure + tactical attack
    mg_add += kingStruct.mg + ksMG;
    eg_add += kingStruct.eg;

    // mobility
    mg_add += att.mg;
    eg_add += att.eg;

    // dynamic passer adds (needs A/occ/kings)
    if (wPass | bPass)
    {
      const PasserDyn pd = passer_dynamic_bonus(occ, wK, bK, wPass, bPass);
      mg_add += pd.mg;
      eg_add += pd.eg;
    }

    // threats/tropism
    mg_add += (thr * THREATS_MG_NUM) / THREATS_MG_DEN;
    eg_add += thr / THREATS_EG_DEN;

    // bishop pair + imbalance
    mg_add += bp + imb;
    eg_add += bp / BISHOP_PAIR_EG_DEN + imb / IMBALANCE_EG_DEN;

    // bad bishop
    mg_add += badB;
    eg_add += badB / BAD_BISHOP_EG_DEN;

    // Pawn levers: mostly MG; a touch in EG
    mg_add += lever;
    eg_add += lever / LEVER_EG_DEN;

    // EG extras
    if (anyRooks)
      eg_add += rook_endgame_extras_eg(W, B, occ, mc, wK, bK);

    mg += mg_add;
    eg += eg_add;

    // scale only the EG component
    eg += initiative_complexity(mc, W[0], B[0], wPass, bPass, eg);

    const int scale = endgame_scale(W, B, mc, wK, bK);
    eg = (eg * scale) / FULL_SCALE;

    int score = taper(mg, eg, curPhase);

    // tempo (phase-aware)
    const bool wtm = (pos.getState().sideToMove == chess::Color::White);
    const int tempo = taper(TEMPO_MG, TEMPO_EG, curPhase);
    score += (wtm ? +tempo : -tempo);

    score = clampi(score, -MATE + 1, MATE - 1);
    return score;
  }

}
