#include "lilia/engine/search.hpp"

#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstring>
#include <exception>
#include <future>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <unordered_set>
#include <vector>

#include "lilia/engine/config.hpp"
#include "lilia/chess/move_buffer.hpp"
#include "lilia/engine/move_list.hpp"
#include "lilia/engine/move_order.hpp"
#include "lilia/engine/thread_pool.hpp"
#include "lilia/chess/core/bitboard.hpp"
#include "lilia/chess/core/magic.hpp"
#include "lilia/chess/compiler.hpp"

namespace lilia::engine
{

  using steady_clock = std::chrono::steady_clock;

  LILIA_ALWAYS_INLINE int16_t clamp16(int x)
  {
    if (x > 32767)
      return 32767;
    if (x < -32768)
      return -32768;
    return (int16_t)x;
  }

  LILIA_ALWAYS_INLINE int mate_in(int ply)
  {
    return MATE - ply;
  }
  LILIA_ALWAYS_INLINE int mated_in(int ply)
  {
    return -MATE + ply;
  }
  LILIA_ALWAYS_INLINE bool is_mate_score(int s)
  {
    return std::abs(s) >= MATE_THR;
  }
  LILIA_ALWAYS_INLINE int cap_ply(int ply)
  {
    return ply < 0 ? 0 : (ply >= MAX_PLY ? (MAX_PLY - 1) : ply);
  }

  LILIA_ALWAYS_INLINE int encode_tt_score(int s, int ply)
  {
    if (s >= MATE_THR)
      return s + ply;
    if (s <= -MATE_THR)
      return s - ply;
    return s;
  }
  LILIA_ALWAYS_INLINE int decode_tt_score(int s, int ply)
  {
    if (s >= MATE_THR)
      return s - ply;
    if (s <= -MATE_THR)
      return s + ply;
    return s;
  }

  namespace
  {
    // -----------------------------
    // Search tuning / pruning
    // -----------------------------

    // History / decay
    static constexpr int HIST_BONUS_BASE = 16;
    static constexpr int HIST_BONUS_LOG_STEP = 8;
    static constexpr int HISTORY_DECAY_SHIFT = 6;

    // Node batching / stop polling
    static constexpr uint32_t STOP_POLL_MASK = 63u;
    static constexpr uint32_t NODE_BATCH_TICK_STEP = 8192u;

    // Eval trend / tension classification
    static constexpr int IMPROVING_EVAL_MARGIN = 16;
    static constexpr int NARROW_WINDOW_MARGIN = 16;
    static constexpr int HIGH_TENSION_ALPHA_MARGIN = 64;
    static constexpr int TACTICAL_NODE_MAX_DEPTH = 5;
    static constexpr int NEED_NONPAWN_SHALLOW_DEPTH = 3;

    // Razoring / reverse futility / SNMP
    static constexpr int FUT_MARGIN[4] = {0, 110, 210, 300};
    static constexpr int SNMP_MARGINS[4] = {0, 140, 200, 260};
    static constexpr int RAZOR_MARGIN[3] = {0, 256, 480}; // [depth], used for depths 1..2
    static constexpr int RAZOR_IMPROVING_BONUS = 64;
    static constexpr int RFP_MARGIN_BASE = 190;
    static constexpr int RFP_IMPROVING_BONUS = 40;
    static constexpr int LOW_MVV_MARGIN = 360;
    static constexpr int LOSING_CAPTURE_SKIP_MVV = 400;
    static constexpr int DELTA_MARGIN = 112;

    // Quiet pruning / LMP / futility
    static constexpr int LMP_LIMIT[4] = {0, 5, 9, 14};
    static constexpr int BAD_HISTORY_THRESHOLD = -8000;
    static constexpr int BAD_HISTORY_PRUNE_THRESHOLD = -11000;
    static constexpr int LMP_IMPROVING_BONUS = 32;
    static constexpr int LMP_ALPHA_SLACK = 32;
    static constexpr int QUIET_LATE_PRUNE_BASE = 16;
    static constexpr int QUIET_LATE_PRUNE_DEPTH_FACTOR = 4;
    static constexpr int EXT_FUTILITY_NEG_HISTORY_BONUS = 32;
    static constexpr int EXT_FUTILITY_IMPROVING_BONUS = 48;

    // Qsearch
    static constexpr int QS_INCHECK_COUNTERMOVE_BONUS = 80'000;
    static constexpr int QS_INCHECK_CAPTURE_BONUS = 100'000;
    static constexpr int QS_INCHECK_PROMO_BONUS = 60'000;
    static constexpr int QS_QUIET_CHECK_MIN_NONPAWNS = 2;
    static constexpr int QS_QUIET_CHECK_LIMIT = 10;
    static constexpr int QS_QUIET_CHECK_MARGIN = 64;
    static constexpr int QS_QUIET_CHECK_KILLER_BONUS = 6000;

    // IID / quick quiet-check probe / null move
    static constexpr int IID_MIN_DEPTH = 5;
    static constexpr int IID_TT_DEPTH_MARGIN = 2;
    static constexpr int IID_NONPV_STATIC_MARGIN = 32;

    static constexpr int QUICK_CHECK_PROBE_MAX_DEPTH = 5;
    static constexpr int QUICK_CHECK_PROBE_MOVE_CAP = 16;
    static constexpr int QUICK_CHECK_MIN_HISTORY = 0;

    static constexpr int NULLMOVE_MIN_DEPTH = 3;
    static constexpr int NULLMOVE_SPARSE_NONPAWNS = 3;
    static constexpr int NULLMOVE_DEEP_DEPTH = 8;
    static constexpr int NULLMOVE_BASE_REDUCTION = 2;
    static constexpr int NULLMOVE_DEEP_REDUCTION_BONUS = 1;
    static constexpr int NULLMOVE_BIG_EVAL_MARGIN_1 = 200;
    static constexpr int NULLMOVE_BIG_EVAL_MARGIN_2 = 500;
    static constexpr int NULLMOVE_DENSE_NONPAWNS = 8;
    static constexpr int NULLMOVE_MARGIN_BASE = 50;
    static constexpr int NULLMOVE_MARGIN_DEPTH_MULT = 20;
    static constexpr int NULLMOVE_MARGIN_IMPROVING_BONUS = 40;
    static constexpr int NULLMOVE_VERIFY_MIN_R = 3;
    static constexpr int NULLMOVE_VERIFY_MAX_EVAL_GAP = 800;

    // Staged move ordering
    static constexpr int ORDER_BUCKET = 10'000'000;

    enum MoveOrderStage : int
    {
      ORDER_STAGE_BAD_CAP = 1,
      ORDER_STAGE_QUIET = 2,
      ORDER_STAGE_KILLER_CM_QP = 3,
      ORDER_STAGE_GOOD_CAP = 4,
      ORDER_STAGE_TT = 5
    };

    static constexpr int ORDER_TT_BONUS = 2'400'000;
    static constexpr int ORDER_GOOD_CAPTURE_BASE = 180'000;
    static constexpr int ORDER_BAD_CAPTURE_BASE = 20'000;
    static constexpr int ORDER_PROMO_BASE = 160'000;
    static constexpr int ORDER_KILLER_BASE = 120'000;
    static constexpr int ORDER_COUNTERMOVE_BASE = 140'000;
    static constexpr int ORDER_HEAVY_QUIET_MALUS = 6000;
    static constexpr int ORDER_CHECK_BONUS = 90'000;
    static constexpr int ORDER_THREAT_BONUS = 40'000;

    // Singular extension / ProbCut / LMR / check extensions
    static constexpr int SINGULAR_MIN_DEPTH = 6;
    static constexpr int SINGULAR_DEEP_DEPTH = 8;
    static constexpr int SINGULAR_REDUCTION_SHALLOW = 2;
    static constexpr int SINGULAR_REDUCTION_DEEP = 3;
    static constexpr int SINGULAR_MARGIN_BASE = 64;
    static constexpr int SINGULAR_MARGIN_DEPTH_MULT = 2;
    static constexpr int SINGULAR_MATE_FLOOR_MARGIN = 64;

    static constexpr int PROBCUT_MIN_DEPTH = 4;
    static constexpr int PROBCUT_MARGIN = 224;
    static constexpr int PROBCUT_REDUCTION = 3;
    static constexpr int PROBCUT_MIN_MVV = 500;

    static constexpr int EARLY_PROBCUT_MIN_DEPTH = 6;
    static constexpr int EARLY_PROBCUT_MARGIN = 192;
    static constexpr int EARLY_PROBCUT_MAX_SCAN = 6;
    static constexpr int EARLY_PROBCUT_REDUCTION = 3;

    static constexpr int QUIET_CHECK_EXTENSION_MAX_DEPTH = 2;
    static constexpr int QUIET_CHECK_EXTENSION_MIN_HISTORY = 0;

    static constexpr int LMR_MIN_MOVE_INDEX = 3;
    static constexpr int LMR_GOOD_HISTORY_THRESHOLD = 8000;
    static constexpr int LMR_GOOD_CONT_HISTORY_THRESHOLD = 8000;
    static constexpr int LMR_SHALLOW_PLY = 2;
    static constexpr int LMR_NARROW_WINDOW = 8;
    static constexpr int LMR_NO_REDUCE_SHALLOW_DEPTH = 2;
    static constexpr int LMR_NO_REDUCE_FIRST_MOVES = 3;
    static constexpr int LMR_DEEP_DEPTH = 5;
    static constexpr int LMR_REDUCTION_CAP_SHALLOW = 2;
    static constexpr int LMR_REDUCTION_CAP_DEEP = 3;

    // Root ordering / aspiration / root reductions
    static constexpr int ROOT_ORDER_TT_BONUS = 2'500'000;
    static constexpr int ROOT_ORDER_PROMO_BONUS = 1'200'000;
    static constexpr int ROOT_ORDER_CAPTURE_BASE = 1'050'000;
    static constexpr int ROOT_ORDER_HISTORY_CLAMP = 20'000;
    static constexpr int ROOT_ORDER_CHECK_BONUS = 3'000;
    static constexpr int ROOT_ORDER_THREAT_BONUS = 1'000;

    static constexpr int ROOT_LMR_MIN_DEPTH = 6;
    static constexpr int ROOT_LMR_DEEP_DEPTH = 10;
    static constexpr int ROOT_LMR_LATE_MOVE_INDEX = 3;
    static constexpr int ROOT_LMR_SHALLOW_DEPTH = 7;
    static constexpr int ROOT_LMR_BASE_REDUCTION = 1;

    static constexpr int ASPIRATION_INITIAL_WINDOW = 24;
    static constexpr int ASPIRATION_MIN_WINDOW = 12;
    static constexpr int ASPIRATION_WIDEN_MIN_STEP = 32;

    // Quiet signal levels
    enum QuietSignalLevel : int
    {
      QUIET_SIGNAL_NONE = 0,
      QUIET_SIGNAL_THREAT = 1,
      QUIET_SIGNAL_CHECK = 2
    };

    // Quiet / continuation history blending
    static constexpr int QUIET_HIST_BLEND_SHIFT = 1;
    static constexpr int COUNTERMOVE_HIST_BLEND_SHIFT = 1;
    static constexpr int FAIL_LOW_HISTORY_SHIFT = 1;
    static constexpr int CONTHIST_MALUS_SHIFT_L1 = 1;
    static constexpr int CONTHIST_MALUS_SHIFT_L2 = 2;
    static constexpr int CONTHIST_BONUS_SHIFT_L1 = 1;
    static constexpr int CONTHIST_BONUS_SHIFT_L2 = 2;

    // Repeated depth gates
    static constexpr int SNMP_MAX_DEPTH = 3;
    static constexpr int QUIET_PRUNE_MAX_DEPTH = 3;
    static constexpr int HISTORY_PRUNE_MAX_DEPTH = 2;
    static constexpr int ASPIRATION_MIN_DEPTH = 3;

    // Small search-policy constants
    static constexpr int PV_FROM_TT_MAX_LEN = 32;
    static constexpr int ROOT_TOP_MOVES_MAX = 5;
    static constexpr int HEURISTIC_EMA_MERGE_FACTOR = 4;

    // More Extensions and reduction
    static constexpr int SINGULAR_EXTENSION = 1;
    static constexpr int CAPTURE_CHECK_EXTENSION = 1;
    static constexpr int QUIET_CHECK_EXTENSION = 1;
    static constexpr int PASSED_PUSH_EXTENSION = 1;
    static constexpr int BAD_CAPTURE_REDUCTION = 1;
  }

  namespace
  {

    struct MoveUndoGuard
    {
      SearchPosition &pos;
      bool applied = false;
      LILIA_ALWAYS_INLINE explicit MoveUndoGuard(SearchPosition &p) : pos(p) {}
      LILIA_ALWAYS_INLINE bool doMove(const chess::Move &m)
      {
        applied = pos.doMove(m);
        return applied;
      }
      LILIA_ALWAYS_INLINE void rollback()
      {
        if (LILIA_UNLIKELY(applied))
        {
          pos.undoMove();
          applied = false;
        }
      }
      LILIA_ALWAYS_INLINE ~MoveUndoGuard()
      {
        if (LILIA_UNLIKELY(applied))
          pos.undoMove();
      }
    };

    struct NullUndoGuard
    {
      SearchPosition &pos;
      bool applied = false;
      LILIA_ALWAYS_INLINE explicit NullUndoGuard(SearchPosition &p) : pos(p) {}
      LILIA_ALWAYS_INLINE bool doNull()
      {
        applied = pos.doNullMove();
        return applied;
      }
      LILIA_ALWAYS_INLINE void rollback()
      {
        if (LILIA_UNLIKELY(applied))
        {
          pos.undoNullMove();
          applied = false;
        }
      }
      LILIA_ALWAYS_INLINE ~NullUndoGuard()
      {
        if (LILIA_UNLIKELY(applied))
          pos.undoNullMove();
      }
    };

    LILIA_ALWAYS_INLINE void check_stop(const std::shared_ptr<std::atomic<bool>> &stopFlag)
    {
      if (LILIA_UNLIKELY(stopFlag && stopFlag->load(std::memory_order_relaxed)))
        throw SearchStoppedException();
    }

    static LILIA_ALWAYS_INLINE int ilog2_u32(unsigned v)
    {
#if defined(__GNUC__) || defined(__clang__)
      return v ? 31 - __builtin_clz(v) : 0;
#else
      int r = 0;
      while (v >>= 1)
        ++r;
      return r;
#endif
    }
    static LILIA_ALWAYS_INLINE int iabs_int(int x)
    {
      return x < 0 ? -x : x;
    }

    static LILIA_ALWAYS_INLINE int hist_bonus(int depth)
    {
      unsigned x = (unsigned)(depth * depth) + 1u;
      int lg = ilog2_u32(x);
      return HIST_BONUS_BASE + HIST_BONUS_LOG_STEP * lg;
    }

    template <typename T>
    static LILIA_ALWAYS_INLINE void hist_update(T &h, int bonus)
    {
      int x = (int)h;
      x += bonus - (x * iabs_int(bonus)) / 32768;
      if (x > 32767)
        x = 32767;
      if (x < -32768)
        x = -32768;
      h = (T)x;
    }

    static LILIA_ALWAYS_INLINE int gen_all(chess::MoveGenerator &mg, SearchPosition &pos, chess::Move *out,
                                           int cap)
    {
      chess::MoveBuffer buf(out, cap);
      return mg.generatePseudoLegalMoves(pos.getBoard(), pos.getState(), buf);
    }
    static LILIA_ALWAYS_INLINE int gen_caps(chess::MoveGenerator &mg, SearchPosition &pos, chess::Move *out,
                                            int cap)
    {
      chess::MoveBuffer buf(out, cap);
      return mg.generateCapturesOnly(pos.getBoard(), pos.getState(), buf);
    }
    static LILIA_ALWAYS_INLINE int gen_evasions(chess::MoveGenerator &mg, SearchPosition &pos, chess::Move *out,
                                                int cap)
    {
      chess::MoveBuffer buf(out, cap);
      return mg.generateEvasions(pos.getBoard(), pos.getState(), buf);
    }

    // Is there one of our advanced pawns on or next to the capture file?
    // (Fast heuristic for clearance sacs that free a passer push next.)
    static inline bool advanced_pawn_adjacent_to(const chess::Board &b, chess::Color us, int toSq)
    {
      auto paw = b.getPieces(us, chess::PieceType::Pawn);
      const int toF = (int)chess::bb::file_of(toSq);
      while (paw)
      {
        int s = chess::bb::ctz64(paw);
        paw &= paw - 1;
        int r = (int)chess::bb::rank_of(s);
        int f = (int)chess::bb::file_of(s);
        // "Advanced" = already deep enough to matter
        bool advanced = (us == chess::Color::White) ? (r >= 4) : (r <= 3);
        if (advanced && std::abs(f - toF) <= 1)
          return true;
      }
      return false;
    }

    static inline bool is_advanced_passed_pawn_push(const chess::Board &b, const chess::Move &m,
                                                    chess::Color us)
    {
      if (m.isCapture() || m.promotion() != chess::PieceType::None)
        return false;

      auto mover = b.getPiece(m.from());
      if (!mover || mover->type != chess::PieceType::Pawn)
        return false;

      const int toSq = m.to();
      const int toFile = chess::bb::file_of(static_cast<chess::Square>(toSq));
      const int toRank = chess::bb::rank_of(static_cast<chess::Square>(toSq));

      if (us == chess::Color::White)
      {
        if (toRank < 4)
          return false; // only consider far-advanced pawns
      }
      else
      {
        if (toRank > 3)
          return false;
      }

      auto oppPawns = b.getPieces(~us, chess::PieceType::Pawn);
      if (!oppPawns)
        return true; // trivially passed

      const int dir = (us == chess::Color::White) ? 1 : -1;
      for (int df = -1; df <= 1; ++df)
      {
        int file = toFile + df;
        if (file < 0 || file > 7)
          continue;

        for (int r = toRank + dir; r >= 0 && r < 8; r += dir)
        {
          int sq = (r << 3) | file;
          auto sqBB = chess::bb::sq_bb(static_cast<chess::Square>(sq));
          if (oppPawns & sqBB)
            return false;
        }
      }

      return true;
    }

    static inline void decay_tables(Search &S, int shift /* e.g. 6 => ~1.6% */)
    {
      for (int f = 0; f < chess::SQ_NB; ++f)
        for (int t = 0; t < chess::SQ_NB; ++t)
          S.history[f][t] = clamp16((int)S.history[f][t] - ((int)S.history[f][t] >> shift));

      for (int p = 0; p < chess::PIECE_TYPE_NB; ++p)
        for (int t = 0; t < chess::SQ_NB; ++t)
          S.quietHist[p][t] = clamp16((int)S.quietHist[p][t] - ((int)S.quietHist[p][t] >> shift));

      for (int mp = 0; mp < chess::PIECE_TYPE_NB; ++mp)
        for (int t = 0; t < chess::SQ_NB; ++t)
          for (int cp = 0; cp < chess::PIECE_TYPE_NB; ++cp)
            S.captureHist[mp][t][cp] =
                clamp16((int)S.captureHist[mp][t][cp] - ((int)S.captureHist[mp][t][cp] >> shift));

      for (int f = 0; f < chess::SQ_NB; ++f)
        for (int t = 0; t < chess::SQ_NB; ++t)
          S.counterHist[f][t] = clamp16((int)S.counterHist[f][t] - ((int)S.counterHist[f][t] >> shift));

      for (int L = 0; L < CONTHIST_LAYERS; ++L)
        for (int pp = 0; pp < chess::PIECE_TYPE_NB; ++pp)
          for (int pt = 0; pt < chess::SQ_NB; ++pt)
            for (int mp = 0; mp < chess::PIECE_TYPE_NB; ++mp)
              for (int to = 0; to < chess::SQ_NB; ++to)
              {
                int16_t &h = S.contHist[L][pp][pt][mp][to];
                h = clamp16((int)h - ((int)h >> shift));
              }
    }

    // 0 = no signal; 1 = attacks high-value piece; 2 = gives check
    struct CheckTables
    {
      chess::bb::Bitboard KN_FROM[chess::SQ_NB];
      chess::bb::Bitboard K_FROM[chess::SQ_NB];
      chess::bb::Bitboard PAWN_CHK[2][chess::SQ_NB]; // [us][kSq]
      chess::bb::Bitboard LINE[chess::SQ_NB][chess::SQ_NB];
      chess::bb::Bitboard BETWEEN[chess::SQ_NB][chess::SQ_NB];
      chess::bb::Bitboard RAY[chess::SQ_NB][chess::SQ_NB]; // ray starting at a towards b
      int DIR[chess::SQ_NB][chess::SQ_NB];                 // -1 if not aligned, else 0..7 for N,NE,E,SE,S,SW,W,NW
    } CT;

    static inline void init_check_tables()
    {
      for (int s = 0; s < chess::SQ_NB; ++s)
      {
        CT.KN_FROM[s] = chess::bb::knight_attacks_from(static_cast<chess::Square>(s));
        CT.K_FROM[s] = chess::bb::king_attacks_from(static_cast<chess::Square>(s));
      }
      for (int k = 0; k < chess::SQ_NB; ++k)
      {
        auto kB = chess::bb::sq_bb(static_cast<chess::Square>(k));
        CT.PAWN_CHK[(int)chess::Color::White][k] = chess::bb::sw(kB) | chess::bb::se(kB);
        CT.PAWN_CHK[(int)chess::Color::Black][k] = chess::bb::nw(kB) | chess::bb::ne(kB);
      }

      auto on_line = [&](int a, int b) -> bool
      {
        int ra = chess::bb::rank_of(static_cast<chess::Square>(a));
        int fa = chess::bb::file_of(static_cast<chess::Square>(a));
        int rb = chess::bb::rank_of(static_cast<chess::Square>(b));
        int fb = chess::bb::file_of(static_cast<chess::Square>(b));
        return (ra == rb) || (fa == fb) || (std::abs(ra - rb) == std::abs(fa - fb));
      };

      auto dir_from_to = [&](int a, int b) -> int
      {
        int ra = chess::bb::rank_of(static_cast<chess::Square>(a));
        int fa = chess::bb::file_of(static_cast<chess::Square>(a));
        int rb = chess::bb::rank_of(static_cast<chess::Square>(b));
        int fb = chess::bb::file_of(static_cast<chess::Square>(b));
        int dr = (rb > ra) - (rb < ra); // -1,0,1
        int df = (fb > fa) - (fb < fa);
        if (dr == 0 && df == 0)
          return -1;
        if (dr == 0 && df == 1)
          return 2; // E
        if (dr == 0 && df == -1)
          return 6; // W
        if (dr == 1 && df == 0)
          return 0; // N
        if (dr == -1 && df == 0)
          return 4; // S
        if (dr == 1 && df == 1)
          return 1; // NE
        if (dr == 1 && df == -1)
          return 7; // NW
        if (dr == -1 && df == 1)
          return 3; // SE
        if (dr == -1 && df == -1)
          return 5; // SW
        return -1;
      };

      auto step = [](int dir, chess::bb::Bitboard b)
      {
        switch (dir)
        {
        case 0:
          return chess::bb::north(b);
        case 1:
          return chess::bb::ne(b);
        case 2:
          return chess::bb::east(b);
        case 3:
          return chess::bb::se(b);
        case 4:
          return chess::bb::south(b);
        case 5:
          return chess::bb::sw(b);
        case 6:
          return chess::bb::west(b);
        case 7:
          return chess::bb::nw(b);
        }
        return (chess::bb::Bitboard)0;
      };

      for (int a = 0; a < chess::SQ_NB; ++a)
        for (int b = 0; b < chess::SQ_NB; ++b)
        {
          CT.DIR[a][b] = dir_from_to(a, b);
          if (!on_line(a, b))
          {
            CT.LINE[a][b] = 0;
            CT.BETWEEN[a][b] = 0;
            CT.RAY[a][b] = 0;
            continue;
          }
          auto A = lilia::chess::bb::sq_bb(static_cast<chess::Square>(a));
          auto B = lilia::chess::bb::sq_bb(static_cast<chess::Square>(b));
          int d = CT.DIR[a][b];
          // RAY[a][b]: from a toward b, exclusive
          chess::bb::Bitboard ray = 0, r = step(d, A);
          while (r)
          {
            ray |= r;
            if (r & B)
              break;
            r = step(d, r);
          }
          CT.RAY[a][b] = ray;
          // BETWEEN[a][b]: squares strictly between
          CT.BETWEEN[a][b] = ray & ~B;
          // LINE[a][b]: whole line through a,b (union of both rays + endpoints)
          // Build opposite ray too:
          int dOpp = (d + 4) & 7;
          chess::bb::Bitboard rayOpp = 0, r2 = step(dOpp, A);
          while (r2)
          {
            rayOpp |= r2;
            r2 = step(dOpp, r2);
          }
          CT.LINE[a][b] = ray | rayOpp | A | B;
        }
    }

    struct QuietSignals
    {
      int pawnSignal = 0;
      int pieceSignal = 0;
      bool givesCheck = false;
    };

    // Does moving to m.to() create an x-ray on the enemy king such that
    // removing exactly one blocker (which we already attack) would deliver check?
    static inline bool xray_check_after_one_capture(const SearchPosition &pos, const chess::Move &m,
                                                    chess::bb::Bitboard occ, int kingSq,
                                                    chess::Color us, chess::PieceType moverAfter)
    {

      if (!(moverAfter == chess::PieceType::Bishop || moverAfter == chess::PieceType::Rook || moverAfter == chess::PieceType::Queen))
        return false;

      if (!CT.LINE[kingSq][m.to()])
        return false;

      const auto between = CT.BETWEEN[kingSq][m.to()];
      auto blockers = occ & between;
      if (!blockers)
        return false;

      // Require exactly one blocker on the ray
      if (blockers & (blockers - 1))
        return false;
      const int blSq = chess::bb::ctz64(blockers);

      const auto &B = pos.getBoard();

      // Compute if we (us) already attack that blocking square from the current position.
      lilia::chess::bb::Bitboard atk = 0;

      // Pawns that could capture the blocker
      atk |= (CT.PAWN_CHK[(int)us][blSq] & B.getPieces(us, chess::PieceType::Pawn));

      // Knights / King
      atk |= (chess::bb::knight_attacks_from((chess::Square)blSq) & B.getPieces(us, chess::PieceType::Knight));
      atk |= (chess::bb::king_attacks_from((chess::Square)blSq) & B.getPieces(us, chess::PieceType::King));

      // Sliders
      atk |= (chess::magic::sliding_attacks(chess::magic::Slider::Bishop, (chess::Square)blSq, occ) &
              (B.getPieces(us, chess::PieceType::Bishop) | B.getPieces(us, chess::PieceType::Queen)));
      atk |= (chess::magic::sliding_attacks(chess::magic::Slider::Rook, (chess::Square)blSq, occ) &
              (B.getPieces(us, chess::PieceType::Rook) | B.getPieces(us, chess::PieceType::Queen)));

      return atk != 0;
    }

    static inline QuietSignals compute_quiet_signals(const SearchPosition &pos, const chess::Move &m)
    {
      QuietSignals info{};

      if (m.isNull())
        return info;

      const auto &board = pos.getBoard();
      const auto us = pos.getState().sideToMove;

      const auto enemyKingBB = board.getPieces(~us, chess::PieceType::King);
      if (!enemyKingBB)
        return info;

      const int kingSq = lilia::chess::bb::ctz64(enemyKingBB);
      const auto fromBB = lilia::chess::bb::sq_bb(m.from());
      const auto toBB = lilia::chess::bb::sq_bb(m.to());

      auto occ = board.getAllPieces();
      occ = (occ & ~fromBB) | toBB;
      if (m.isEnPassant())
      {
        const int epSq = (us == chess::Color::White) ? m.to() - 8 : m.to() + 8;
        occ &= ~lilia::chess::bb::sq_bb(static_cast<chess::Square>(epSq));
      }

      chess::PieceType moverBefore = chess::PieceType::None;
      if (auto mover = board.getPiece(m.from()))
        moverBefore = mover->type;
      chess::PieceType moverAfter = (m.promotion() != chess::PieceType::None) ? m.promotion() : moverBefore;

      // Direct check detection using precomputed tables and magics
      if (moverAfter == chess::PieceType::Pawn)
      {
        if (CT.PAWN_CHK[(int)us][kingSq] & toBB)
          info.givesCheck = true;
      }
      else if (moverAfter == chess::PieceType::Knight)
      {
        if (CT.KN_FROM[m.to()] & enemyKingBB)
          info.givesCheck = true;
      }
      else if (moverAfter == chess::PieceType::King)
      {
        if (CT.K_FROM[m.to()] & enemyKingBB)
          info.givesCheck = true;
      }
      else if (moverAfter == chess::PieceType::Bishop || moverAfter == chess::PieceType::Rook || moverAfter == chess::PieceType::Queen)
      {
        if (CT.LINE[kingSq][m.to()] && (occ & CT.BETWEEN[kingSq][m.to()]) == 0)
        {
          const bool rookLine = (CT.DIR[kingSq][m.to()] % 2 == 0);
          const bool bishopLine = (CT.DIR[kingSq][m.to()] % 2 != 0);
          if ((moverAfter == chess::PieceType::Rook && rookLine) || (moverAfter == chess::PieceType::Bishop && bishopLine) ||
              moverAfter == chess::PieceType::Queen)
            info.givesCheck = true;
        }
      }

      if (!info.givesCheck && CT.LINE[kingSq][m.from()])
      {
        const int dir = CT.DIR[kingSq][m.from()];
        auto ray = CT.RAY[kingSq][m.from()];
        auto blockers = occ & ray;
        if (blockers)
        {
          int firstSq;
          // Use CTZ when indices grow along the ray: N(0), NE(1), E(2), NW(7)
          if (dir == 0 || dir == 1 || dir == 2 || dir == 7)
            firstSq = lilia::chess::bb::ctz64(blockers);
          else
            firstSq = 63 - lilia::chess::bb::clz64(blockers);

          if (auto firstPiece = board.getPiece(firstSq); firstPiece && firstPiece->color == us)
          {
            const bool rookLine = (dir % 2 == 0);
            const bool bishopLine = (dir % 2 != 0);
            if ((rookLine && (firstPiece->type == chess::PieceType::Rook || firstPiece->type == chess::PieceType::Queen)) ||
                (bishopLine && (firstPiece->type == chess::PieceType::Bishop || firstPiece->type == chess::PieceType::Queen)))
            {
              info.givesCheck = true;
            }
          }
        }
      }

      // Quiet threat signals
      if (!m.isCapture() && m.promotion() == chess::PieceType::None)
      {
        if (moverBefore == chess::PieceType::Pawn)
        {
          const auto to = lilia::chess::bb::sq_bb(m.to());
          const auto pawnAtk = (us == chess::Color::White)
                                   ? (lilia::chess::bb::ne(to) | lilia::chess::bb::nw(to))
                                   : (lilia::chess::bb::se(to) | lilia::chess::bb::sw(to));

          if (pawnAtk & enemyKingBB)
            info.pawnSignal = QUIET_SIGNAL_CHECK;
          else
          {
            const auto targets = board.getPieces(~us, chess::PieceType::Queen) | board.getPieces(~us, chess::PieceType::Rook) |
                                 board.getPieces(~us, chess::PieceType::Bishop) | board.getPieces(~us, chess::PieceType::Knight);
            if (pawnAtk & targets)
              info.pawnSignal = QUIET_SIGNAL_THREAT;
            else if (is_advanced_passed_pawn_push(board, m, us))
              info.pawnSignal = QUIET_SIGNAL_THREAT;
          }
        }
        else if (moverBefore != chess::PieceType::None)
        {
          lilia::chess::bb::Bitboard attacks = 0;
          switch (moverBefore)
          {
          case chess::PieceType::Knight:
            attacks = lilia::chess::bb::knight_attacks_from(m.to());
            break;
          case chess::PieceType::Bishop:
            attacks = lilia::chess::magic::sliding_attacks(lilia::chess::magic::Slider::Bishop,
                                                           m.to(), occ);
            break;
          case chess::PieceType::Rook:
            attacks =
                lilia::chess::magic::sliding_attacks(lilia::chess::magic::Slider::Rook, m.to(), occ);
            break;
          case chess::PieceType::Queen:
            attacks =
                lilia::chess::magic::sliding_attacks(lilia::chess::magic::Slider::Bishop, m.to(),
                                                     occ) |
                lilia::chess::magic::sliding_attacks(lilia::chess::magic::Slider::Rook, m.to(), occ);
            break;
          case chess::PieceType::King:
            attacks = lilia::chess::bb::king_attacks_from(m.to());
            break;
          default:
            break;
          }

          if (attacks & enemyKingBB)
          {
            info.pieceSignal = QUIET_SIGNAL_CHECK;
          }
          else
          {
            const auto targets = board.getPieces(~us, chess::PieceType::Queen) | board.getPieces(~us, chess::PieceType::Rook) |
                                 board.getPieces(~us, chess::PieceType::Bishop) | board.getPieces(~us, chess::PieceType::Knight);
            if (attacks & targets)
              info.pieceSignal = QUIET_SIGNAL_THREAT;
          }
        }
      }

      // X-ray discovered-check threat: after removing one blocker we already attack,
      // the move would *unveil* a check next ply (e.g. ...Bc5!! threatening ...Qxd4+).
      if (!info.givesCheck &&
          (moverAfter == chess::PieceType::Bishop || moverAfter == chess::PieceType::Rook || moverAfter == chess::PieceType::Queen))
      {
        if (xray_check_after_one_capture(pos, m, occ, kingSq, us, moverAfter))
        {
          // Not a check yet, but a forcing tactical quiet. Use pieceSignal=1 to
          // gate LMP/futility/LMR and to boost ordering.
          info.pieceSignal = std::max(info.pieceSignal, int(QUIET_SIGNAL_THREAT));
        }
      }

      return info;
    }

    inline void ensure_check_tables_initialized()
    {
      static std::once_flag init_flag;
      std::call_once(init_flag, []
                     { init_check_tables(); });
    }

  }

  Search::Search(TT5 &tt_, const EngineConfig &cfg_)
      : tt(tt_), mg(), cfg(cfg_), eval_()
  {
    for (auto &kk : killers)
    {
      kk[0] = chess::Move{};
      kk[1] = chess::Move{};
    }
    for (auto &h : history)
      h.fill(0);
    std::fill(&quietHist[0][0], &quietHist[0][0] + chess::PIECE_TYPE_NB * chess::SQ_NB, 0);
    std::fill(&captureHist[0][0][0], &captureHist[0][0][0] + chess::PIECE_TYPE_NB * chess::SQ_NB * chess::PIECE_TYPE_NB, 0);
    std::fill(&counterHist[0][0], &counterHist[0][0] + chess::SQ_NB * chess::SQ_NB, 0);
    std::memset(contHist, 0, sizeof(contHist));
    for (auto &row : counterMove)
      for (auto &m : row)
        m = chess::Move{};
    for (auto &pm : prevMove)
      pm = chess::Move{};

    stopFlag.reset();
    sharedNodes.reset();
    nodeLimit = 0;
    stats = SearchStats{};
    ensure_check_tables_initialized();
  }

  int Search::signed_eval(SearchPosition &pos)
  {
    int v = eval_.evaluate(pos);
    if (pos.getState().sideToMove == chess::Color::Black)
      v = -v;
    return std::clamp(v, -MATE + 1, MATE - 1);
  }

  namespace
  {

    class ThreadNodeBatch
    {
    public:
      LILIA_ALWAYS_INLINE void reset() { local_ = 0; }

      LILIA_ALWAYS_INLINE void bump(const std::shared_ptr<std::atomic<std::uint64_t>> &counter, std::uint64_t limit,
                                    const std::shared_ptr<std::atomic<bool>> &stopFlag)
      {
        ++local_;
        if (LILIA_UNLIKELY((local_ & STOP_POLL_MASK) == 0u))
        {
          if (stopFlag && stopFlag->load(std::memory_order_relaxed))
            throw SearchStoppedException();
        }

        if (LILIA_UNLIKELY(local_ >= NODE_BATCH_TICK_STEP))
          flush_batch(counter, limit, stopFlag);
      }

      LILIA_ALWAYS_INLINE std::uint64_t flush(const std::shared_ptr<std::atomic<std::uint64_t>> &counter)
      {
        if (!counter)
        {
          local_ = 0;
          return 0;
        }

        const uint32_t pending = local_;
        if (pending == 0u)
        {
          return counter->load(std::memory_order_relaxed);
        }

        local_ = 0;
        return counter->fetch_add(pending, std::memory_order_relaxed) + pending;
      }

    private:
      LILIA_ALWAYS_INLINE void flush_batch(const std::shared_ptr<std::atomic<std::uint64_t>> &counter, std::uint64_t limit,
                                           const std::shared_ptr<std::atomic<bool>> &stopFlag)
      {
        local_ -= NODE_BATCH_TICK_STEP;
        if (counter)
        {
          std::uint64_t cur = counter->fetch_add(NODE_BATCH_TICK_STEP, std::memory_order_relaxed) + NODE_BATCH_TICK_STEP;
          if (LILIA_UNLIKELY(limit && cur >= limit))
          {
            if (stopFlag)
              stopFlag->store(true, std::memory_order_relaxed);
            throw SearchStoppedException();
          }
        }
        if (LILIA_UNLIKELY(stopFlag && stopFlag->load(std::memory_order_relaxed)))
        {
          throw SearchStoppedException();
        }
      }

      uint32_t local_ = 0;
    };

    LILIA_ALWAYS_INLINE ThreadNodeBatch &node_batch()
    {
      static thread_local ThreadNodeBatch instance;
      return instance;
    }

    LILIA_ALWAYS_INLINE void reset_node_batch()
    {
      node_batch().reset();
    }

    LILIA_ALWAYS_INLINE std::uint64_t flush_node_batch(const std::shared_ptr<std::atomic<std::uint64_t>> &counter)
    {
      return node_batch().flush(counter);
    }

    class NodeFlushGuard
    {
    public:
      LILIA_ALWAYS_INLINE explicit NodeFlushGuard(const std::shared_ptr<std::atomic<std::uint64_t>> &counter)
          : counter_(counter)
      {
        reset_node_batch();
      }
      LILIA_ALWAYS_INLINE ~NodeFlushGuard()
      {
        (void)flush_node_batch(counter_);
      }

    private:
      std::shared_ptr<std::atomic<std::uint64_t>> counter_;
    };

  }

  LILIA_ALWAYS_INLINE void bump_node_or_stop(const std::shared_ptr<std::atomic<std::uint64_t>> &counter,
                                             std::uint64_t limit,
                                             const std::shared_ptr<std::atomic<bool>> &stopFlag)
  {
    node_batch().bump(counter, limit, stopFlag);
  }

  int Search::quiescence(SearchPosition &pos, int alpha, int beta, int ply)
  {
    bump_node_or_stop(sharedNodes, nodeLimit, stopFlag);

    if (ply >= MAX_PLY - 2)
      return signed_eval(pos);

    // Draw / 50-move / repetition in qsearch too
    if (pos.checkInsufficientMaterial() || pos.checkMoveRule() || pos.checkRepetition())
      return 0;

    const int kply = cap_ply(ply);
    const uint64_t parentKey = pos.hash();
    const int alphaOrig = alpha, betaOrig = beta;

    chess::Move bestMoveQ{};

    // QTT probe (depth == 0)
    {
      TTEntry5 tte{};
      if (tt.probe_into(pos.hash(), tte))
      {
        const int ttVal = decode_tt_score(tte.value, kply);
        if (tte.depth == 0)
        {
          if (tte.bound == Bound::Exact)
            return ttVal;
          if (tte.bound == Bound::Lower && ttVal >= beta)
            return ttVal;
          if (tte.bound == Bound::Upper && ttVal <= alpha)
            return ttVal;
        }
      }
    }

    const bool inCheck = pos.inCheck();
    if (inCheck)
    {
      // Evasions only
      int n = gen_evasions(mg, pos, genArr_[kply], MAX_MOVES);
      if (n <= 0)
      {
        const int ms = mated_in(ply);
        if (!(stopFlag && stopFlag->load()))
          tt.store(parentKey, encode_tt_score(ms, kply), 0, Bound::Exact, chess::Move{},
                   std::numeric_limits<int16_t>::min());
        return ms;
      }

      int *scores = ordScore_[kply];
      chess::Move *ordered = ordArr_[kply];

      const chess::Move prev = (ply > 0 ? prevMove[cap_ply(ply - 1)] : chess::Move{});
      const bool prevOk = !prev.isNull() && prev.from() != prev.to();
      const chess::Move cm = prevOk ? counterMove[prev.from()][prev.to()] : chess::Move{};

      for (int i = 0; i < n; ++i)
      {
        const auto &m = genArr_[kply][i];
        int s = 0;
        if (prevOk && m == cm)
          s += QS_INCHECK_COUNTERMOVE_BONUS;
        if (m.isCapture())
          s += QS_INCHECK_CAPTURE_BONUS + mvv_lva_fast(pos.position(), m);
        if (m.promotion() != chess::PieceType::None)
          s += QS_INCHECK_PROMO_BONUS;
        s += history[m.from()][m.to()];
        scores[i] = s;
        ordered[i] = m;
      }
      sort_by_score_desc(scores, ordered, n);

      int best = -INF;
      bool anyLegal = false;

      for (int i = 0; i < n; ++i)
      {
        if ((i & STOP_POLL_MASK) == 0)
          check_stop(stopFlag);
        const chess::Move m = ordered[i];

        MoveUndoGuard g(pos);
        if (!g.doMove(m))
          continue;
        anyLegal = true;

        prevMove[cap_ply(ply)] = m;
        tt.prefetch(pos.hash());
        int score = -quiescence(pos, -beta, -alpha, ply + 1);
        score = std::clamp(score, -MATE + 1, MATE - 1);

        if (score >= beta)
        {
          if (!(stopFlag && stopFlag->load()))
            tt.store(parentKey, encode_tt_score(beta, kply), 0, Bound::Lower, m,
                     std::numeric_limits<int16_t>::min());
          return beta;
        }
        if (score > best)
        {
          best = score;
          bestMoveQ = m;
        }
        if (score > alpha)
          alpha = score;
      }

      if (!anyLegal)
      {
        const int ms = mated_in(ply);
        if (!(stopFlag && stopFlag->load()))
          tt.store(parentKey, encode_tt_score(ms, kply), 0, Bound::Exact, chess::Move{},
                   std::numeric_limits<int16_t>::min());
        return ms;
      }

      if (!(stopFlag && stopFlag->load()))
      {
        Bound b = Bound::Exact;
        if (best <= alphaOrig)
          b = Bound::Upper;
        else if (best >= betaOrig)
          b = Bound::Lower;
        tt.store(parentKey, encode_tt_score(best, kply), 0, b, bestMoveQ,
                 std::numeric_limits<int16_t>::min());
      }
      return best;
    }

    const int stand = signed_eval(pos);
    if (stand >= beta)
    {
      if (!(stopFlag && stopFlag->load()))
      {
        constexpr int16_t SE_UNSET = std::numeric_limits<int16_t>::min();
        tt.store(parentKey, encode_tt_score(beta, kply), 0, Bound::Lower, chess::Move{},
                 inCheck ? SE_UNSET : (int16_t)stand);
      }
      return beta;
    }
    if (alpha < stand)
      alpha = stand;

    // Generate captures (+ non-capture promotions)
    int qn = gen_caps(mg, pos, capArr_[kply], MAX_MOVES);
    if (qn < MAX_MOVES)
    {
      chess::MoveBuffer buf(capArr_[kply] + qn, MAX_MOVES - qn);
      qn += mg.generateNonCapturePromotions(pos.getBoard(), pos.getState(), buf);
    }

    // Order captures/promos
    int *qs = ordScore_[kply];
    chess::Move *qord = ordArr_[kply];

    for (int i = 0; i < qn; ++i)
    {
      const auto &m = capArr_[kply][i];
      qs[i] = mvv_lva_fast(pos.position(), m);
      qord[i] = m;
    }
    sort_by_score_desc(qs, qord, qn);

    int best = stand;

    for (int i = 0; i < qn; ++i)
    {
      const chess::Move m = qord[i];
      if ((i & STOP_POLL_MASK) == 0)
        check_stop(stopFlag);

      const bool isCap = m.isCapture();
      const bool isPromo = (m.promotion() != chess::PieceType::None);
      const int mvv = (isCap || isPromo) ? mvv_lva_fast(pos.position(), m) : 0;

      // --- 3) stricter low-MVV negative-SEE prune ---
      if (isCap && !isPromo && mvv < LOW_MVV_MARGIN)
      {
        const chess::Move pm = (ply > 0 ? prevMove[cap_ply(ply - 1)] : chess::Move{});
        const bool isRecap = (!pm.isNull() && pm.to() == m.to());
        const int toFile = chess::bb::file_of(m.to());
        const bool onCenterFile = (toFile == chess::bb::D1 || toFile == chess::bb::E1);

        if (!isRecap && !onCenterFile)
        {
          if (!pos.see(m))
          {
            const auto us = pos.getState().sideToMove;
            if (!advanced_pawn_adjacent_to(pos.getBoard(), us, m.to()))
              continue;
          }
        }
      }

      // SEE once if needed
      bool seeOk = true;
      if (isCap && !isPromo)
      {
        const auto moverOptQ = pos.getBoard().getPiece(m.from());
        const chess::PieceType attackerPtQ = moverOptQ ? moverOptQ->type : chess::PieceType::Pawn;
        const int attackerValQ = base_value[(int)attackerPtQ];
        int victimValQ = 0;
        if (m.isEnPassant())
          victimValQ = base_value[(int)chess::PieceType::Pawn];
        else if (auto capQ = pos.getBoard().getPiece(m.to()))
          victimValQ = base_value[(int)capQ->type];

        if (victimValQ < attackerValQ)
        {
          seeOk = pos.see(m);
          if (!seeOk && mvv < LOSING_CAPTURE_SKIP_MVV)
            continue;
        }
      }

      const bool wouldGiveCheck = compute_quiet_signals(pos, m).givesCheck;

      // Delta pruning (skip if giving check) + discovered-check safeguard
      if (!wouldGiveCheck)
      {
        if (isCap || isPromo)
        {
          int capVal = 0;
          if (m.isEnPassant())
            capVal = base_value[(int)chess::PieceType::Pawn];
          else if (isCap)
          {
            if (auto cap = pos.getBoard().getPiece(m.to()))
              capVal = base_value[(int)cap->type];
          }
          int promoGain = 0;
          if (isPromo)
            promoGain =
                std::max(0, base_value[(int)m.promotion()] - base_value[(int)chess::PieceType::Pawn]);
          const bool quietPromo = isPromo && !isCap;

          bool shouldPrune = quietPromo ? (stand + promoGain + DELTA_MARGIN <= alpha)
                                        : (stand + capVal + promoGain + DELTA_MARGIN <= alpha);

          if (shouldPrune)
          {
            // Quick discovered-check safety: if the move actually gives check, don't prune
            MoveUndoGuard cg(pos);
            if (cg.doMove(m) && pos.lastMoveGaveCheck())
            {
              cg.rollback(); // fall through to normal search
            }
            else
            {
              // illegal or no-check -> keep pruned
              continue;
            }
          }
        }
      }

      MoveUndoGuard g(pos);
      if (!g.doMove(m))
        continue;

      prevMove[cap_ply(ply)] = m;
      tt.prefetch(pos.hash());
      int score = -quiescence(pos, -beta, -alpha, ply + 1);
      score = std::clamp(score, -MATE + 1, MATE - 1);

      if (score >= beta)
      {
        if (!(stopFlag && stopFlag->load()))
          tt.store(parentKey, encode_tt_score(beta, kply), 0, Bound::Lower, m, (int16_t)stand);
        return beta;
      }
      if (score > alpha)
        alpha = score;
      if (score > best)
      {
        best = score;
        bestMoveQ = m;
      }
    }

    // --- limited quiet checks in qsearch (not just low material) ---
    if (best < beta)
    {
      // MATERIAL gate: don't add quiet checks in bare endgames (king chases)
      auto countSideNP = [&](chess::Color c)
      {
        const auto &B = pos.getBoard();
        return chess::bb::popcount(B.getPieces(c, chess::PieceType::Knight) | B.getPieces(c, chess::PieceType::Bishop) |
                                   B.getPieces(c, chess::PieceType::Rook) | B.getPieces(c, chess::PieceType::Queen));
      };
      const int nonP = countSideNP(chess::Color::White) + countSideNP(chess::Color::Black);
      if (nonP >= QS_QUIET_CHECK_MIN_NONPAWNS)
      { // skip in K+minor vs K or K vs K situations
        if (stand + QS_QUIET_CHECK_MARGIN > alpha)
        {
          int an = gen_all(mg, pos, genArr_[kply], MAX_MOVES);

          // Reuse scratch buffers to avoid large stack allocations
          chess::Move *candM = ordArr_[kply];
          int *candS = ordScore_[kply];
          int cn = 0;

          for (int i = 0; i < an; ++i)
          {
            const chess::Move m = genArr_[kply][i];
            if (m.isCapture() || m.promotion() != chess::PieceType::None)
              continue;
            if (!compute_quiet_signals(pos, m).givesCheck)
              continue;

            int sc = history[m.from()][m.to()];
            if (m == killers[kply][0] || m == killers[kply][1])
              sc += QS_QUIET_CHECK_KILLER_BONUS;

            // NOTE: cn is bounded by MAX_MOVES
            candM[cn] = m;
            candS[cn] = sc;
            ++cn;
          }

          if (cn > 1)
            sort_by_score_desc(candS, candM, cn);

          int tried = 0;
          for (int i = 0; i < cn && tried < QS_QUIET_CHECK_LIMIT; ++i)
          {
            const chess::Move m = candM[i];

            MoveUndoGuard g(pos);
            if (!g.doMove(m))
              continue;

            prevMove[cap_ply(ply)] = m;
            int score = -quiescence(pos, -beta, -alpha, ply + 1);
            score = std::clamp(score, -MATE + 1, MATE - 1);
            ++tried;

            if (score >= beta)
            {
              if (!(stopFlag && stopFlag->load()))
                tt.store(parentKey, encode_tt_score(beta, kply), 0, Bound::Lower, m,
                         (int16_t)stand);
              return beta;
            }
            if (score > best)
              best = score;
            if (score > alpha)
              alpha = score;
          }
        }
      }
    }

    if (!(stopFlag && stopFlag->load()))
    {
      Bound b = Bound::Exact;
      if (best <= alphaOrig)
        b = Bound::Upper;
      else if (best >= betaOrig)
        b = Bound::Lower;
      tt.store(parentKey, encode_tt_score(best, kply), 0, b, bestMoveQ, (int16_t)stand);
    }
    return best;
  }

  int Search::negamax(SearchPosition &pos, int depth, int alpha, int beta, int ply,
                      chess::Move &refBest, int parentStaticEval, const chess::Move *excludedMove)
  {
    bump_node_or_stop(sharedNodes, nodeLimit, stopFlag);

    if (ply >= MAX_PLY - 2)
      return signed_eval(pos);
    if (pos.checkInsufficientMaterial() || pos.checkMoveRule() || pos.checkRepetition())
      return 0;
    if (depth <= 0)
      return quiescence(pos, alpha, beta, ply);

    // Mate distance pruning
    alpha = std::max(alpha, mated_in(ply));
    beta = std::min(beta, mate_in(ply));
    if (alpha >= beta)
      return alpha;

    const int origAlpha = alpha;
    const int origBeta = beta;
    const bool isPV = (beta - alpha > 1);

    const bool inCheck = pos.inCheck();

    int best = -INF;
    chess::Move bestLocal{};

    // ----- TT probe (also harvest cached staticEval) -----
    chess::Move ttMove{};
    bool haveTT = false;
    int ttVal = 0;
    Bound ttBound = Bound::Upper; // for SE trust
    int ttStoredDepth = -1;
    int16_t ttSE = std::numeric_limits<int16_t>::min();

    if (TTEntry5 tte{}; tt.probe_into(pos.hash(), tte))
    {
      haveTT = true;
      ttMove = tte.best;
      ttVal = decode_tt_score(tte.value, cap_ply(ply));
      ttBound = tte.bound;
      ttStoredDepth = (int)tte.depth;
      ttSE = tte.staticEval;

      if (tte.depth >= depth)
      {
        if (tte.bound == Bound::Exact)
          return std::clamp(ttVal, -MATE + 1, MATE - 1);
        if (tte.bound == Bound::Lower)
          alpha = std::max(alpha, ttVal);
        if (tte.bound == Bound::Upper)
          beta = std::min(beta, ttVal);
        if (alpha >= beta)
          return std::clamp(ttVal, -MATE + 1, MATE - 1);
      }
    }

    // Compute staticEval (prefer TT's cached one when not in check)
    constexpr int16_t SE_UNSET = std::numeric_limits<int16_t>::min();
    const int staticEval = inCheck ? 0 : (ttSE != SE_UNSET ? (int)ttSE : signed_eval(pos));

    const bool improving =
        !inCheck && (parentStaticEval == INF || staticEval >= parentStaticEval - IMPROVING_EVAL_MARGIN);

    // Count non-pawn material once (for SNMP & Nullmove)
    const auto &b = pos.getBoard();

    bool queensOn = (b.getPieces(chess::Color::White, chess::PieceType::Queen) |
                     b.getPieces(chess::Color::Black, chess::PieceType::Queen)) != 0;
    bool nearWindow = (beta - alpha) <= NARROW_WINDOW_MARGIN;
    bool highTension = (!inCheck && depth <= TACTICAL_NODE_MAX_DEPTH && nearWindow && staticEval + HIGH_TENSION_ALPHA_MARGIN >= alpha);
    bool tacticalNode = queensOn && highTension;

    int nonP = 0;
    const bool needNonP =
        (!inCheck && !isPV &&
         (depth <= NEED_NONPAWN_SHALLOW_DEPTH || (cfg.useNullMove && depth >= NULLMOVE_MIN_DEPTH)));
    if (needNonP)
    {
      auto countSide = [&](chess::Color c)
      {
        return chess::bb::popcount(b.getPieces(c, chess::PieceType::Knight) | b.getPieces(c, chess::PieceType::Bishop) |
                                   b.getPieces(c, chess::PieceType::Rook) | b.getPieces(c, chess::PieceType::Queen));
      };
      nonP = countSide(chess::Color::White) + countSide(chess::Color::Black);
    }

    // Razoring (D1 + D2), non-PV, not in check
    if (!inCheck && !isPV && depth <= 2)
    {
      const int razorMargin = RAZOR_MARGIN[depth] + (improving ? RAZOR_IMPROVING_BONUS : 0);
      if (staticEval + razorMargin <= alpha)
      {
        int q = quiescence(pos, alpha - 1, alpha, ply);
        if (q <= alpha)
          return q;
      }
    }

    // Reverse futility D1
    if (!inCheck && !isPV && depth == 1)
    {
      int margin = RFP_MARGIN_BASE + (improving ? RFP_IMPROVING_BONUS : 0);
      if (staticEval - margin >= beta)
        return staticEval;
    }

    // --- Static Null-Move Pruning (non-PV, not in check, shallow) ---
    if (!tacticalNode && !inCheck && !isPV && depth <= SNMP_MAX_DEPTH)
    {
      const int d = std::max(1, std::min(SNMP_MAX_DEPTH, depth));
      const int margin = SNMP_MARGINS[d];
      if (staticEval - margin >= beta)
      {
        if (!(stopFlag && stopFlag->load()))
        {
          constexpr int16_t SE_UNSET = std::numeric_limits<int16_t>::min();
          tt.store(pos.hash(), encode_tt_score(staticEval, cap_ply(ply)),
                   /*depth*/ 0, Bound::Lower, /*best*/ chess::Move{},
                   inCheck ? SE_UNSET : (int16_t)staticEval);
        }
        return staticEval;
      }
    }

    // Internal Iterative Deepening (IID) for better ordering
    // Trigger when no good TT move at this depth, not in check, depth is big enough.
    if (!inCheck && depth >= IID_MIN_DEPTH && (!haveTT || ttStoredDepth < depth - IID_TT_DEPTH_MARGIN))
    {
      // shallower probe; a touch deeper in PV
      int iidDepth = depth - IID_TT_DEPTH_MARGIN - (isPV ? 0 : 1);
      if (iidDepth < 1)
        iidDepth = 1;

      chess::Move iidBest{};
      // narrow-ish window in non-PV to cut; full in PV
      int iidAlpha = isPV ? alpha : std::max(alpha, staticEval - IID_NONPV_STATIC_MARGIN);
      int iidBeta = isPV ? beta : (iidAlpha + 1);

      (void)negamax(pos, iidDepth, iidAlpha, iidBeta, ply, iidBest, staticEval);
      // re-probe TT to harvest best for ordering
      if (TTEntry5 tte2{}; tt.probe_into(pos.hash(), tte2))
      {
        ttMove = tte2.best;
        haveTT = true;
        ttVal = decode_tt_score(tte2.value, cap_ply(ply));
        ttBound = tte2.bound;
        ttStoredDepth = (int)tte2.depth;
      }
    }

    // --- light "quick quiet check" probe to avoid suicidal null-move
    bool hasQuickQuietCheck = false;
    if (!inCheck && !isPV && depth <= QUICK_CHECK_PROBE_MAX_DEPTH)
    {
      int probeCap = std::min(MAX_MOVES, QUICK_CHECK_PROBE_MOVE_CAP);
      int probeN = gen_all(mg, pos, genArr_[cap_ply(ply)], probeCap);
      for (int i = 0; i < probeN && i < probeCap; ++i)
      {
        const auto &mm = genArr_[cap_ply(ply)][i];
        if (mm.isCapture() || mm.promotion() != chess::PieceType::None)
          continue;

        const auto signals = compute_quiet_signals(pos, mm);
        if (!signals.givesCheck)
          continue;

        // require either a “threat” (attacks a piece) or decent history
        int ps = signals.pieceSignal;
        int h = history[mm.from()][mm.to()];
        if (ps >= QUIET_SIGNAL_THREAT || h > QUICK_CHECK_MIN_HISTORY)
        { // only then disable null move
          hasQuickQuietCheck = true;
          break;
        }
      }
    }

    // Null move pruning (adaptive)
    const bool sparse = (nonP <= NULLMOVE_SPARSE_NONPAWNS);
    const bool prevWasCapture = (ply > 0 && prevMove[cap_ply(ply - 1)].isCapture());

    if (cfg.useNullMove && depth >= NULLMOVE_MIN_DEPTH && !inCheck && !isPV && !sparse && !prevWasCapture &&
        !tacticalNode && !hasQuickQuietCheck)
    {
      const int evalGap = staticEval - beta;
      int rBase = NULLMOVE_BASE_REDUCTION + (depth >= NULLMOVE_DEEP_DEPTH ? NULLMOVE_DEEP_REDUCTION_BONUS : 0);
      if (evalGap > NULLMOVE_BIG_EVAL_MARGIN_1)
        rBase++;
      if (evalGap > NULLMOVE_BIG_EVAL_MARGIN_2)
        rBase++;
      if (!improving)
        rBase++;
      if (nonP >= NULLMOVE_DENSE_NONPAWNS)
        rBase++;

      int R = std::min(rBase, depth - 2);

      int margin = NULLMOVE_MARGIN_BASE + NULLMOVE_MARGIN_DEPTH_MULT * depth +
                   (improving ? NULLMOVE_MARGIN_IMPROVING_BONUS : 0);
      if (staticEval >= beta + margin)
      {
        NullUndoGuard ng(pos);
        if (ng.doNull())
        {
          chess::Move tmpNM{};
          int nullScore = -negamax(pos, depth - 1 - R, -beta, -beta + 1, ply + 1, tmpNM, -staticEval);
          ng.rollback();
          if (nullScore >= beta)
          {
            const bool needVerify =
                (depth >= NULLMOVE_DEEP_DEPTH && R >= NULLMOVE_VERIFY_MIN_R &&
                 evalGap < NULLMOVE_VERIFY_MAX_EVAL_GAP);
            if (needVerify)
            {
              chess::Move tmpVerify{};
              int verify =
                  -negamax(pos, depth - 1, -beta, -beta + 1, ply + 1, tmpVerify, -staticEval);
              if (verify >= beta)
                return beta;
            }
            else
            {
              return beta;
            }
          }
        }
      }
    }

    const int kply = cap_ply(ply);
    int n = 0;
    if (inCheck)
    {
      n = gen_evasions(mg, pos, genArr_[kply], MAX_MOVES);
      if (n <= 0)
        return mated_in(ply);
    }
    else
    {
      n = gen_all(mg, pos, genArr_[kply], MAX_MOVES);
      if (n <= 0)
        return 0;
    }

    const chess::Move prev = (ply > 0 ? prevMove[cap_ply(ply - 1)] : chess::Move{});
    const bool prevOk = !prev.isNull() && prev.from() != prev.to();
    const chess::Move cm = prevOk ? counterMove[prev.from()][prev.to()] : chess::Move{};

    // --------- Staged move ordering ---------
    int *scores = ordScore_[kply];
    chess::Move *ordered = ordArr_[kply];

    const auto &board = pos.getBoard();

    for (int i = 0; i < n; ++i)
    {
      const auto &m = genArr_[kply][i];

      // Skip duplicates of TT later by just giving TT the best bucket;
      // search loop will naturally attempt TT first anyway.
      int stage = ORDER_STAGE_QUIET; // default
      int base = 0;

      const bool isCap = m.isCapture();
      const bool isPromo = (m.promotion() != chess::PieceType::None);
      const bool isQuiet = !isCap && !isPromo;

      // recapture detection (useful to treat as "good cap")
      const chess::Move pm = (ply > 0 ? prevMove[cap_ply(ply - 1)] : chess::Move{});
      const bool isRecap = (!pm.isNull() && pm.to() == m.to());

      // classify captures now: good vs bad (seeGood computed later per-move too,
      // but we also need a quick pre-pass; we can recompute here cheaply)
      bool seeGoodLocal = true;
      chess::PieceType capPt = chess::PieceType::Pawn;
      if (m.isEnPassant())
      {
        capPt = chess::PieceType::Pawn;
      }
      else if (isCap)
      {
        if (auto cap = board.getPiece(m.to()))
          capPt = cap->type;
      }
      if (isCap && m.promotion() == chess::PieceType::None)
      {
        seeGoodLocal = pos.see(m);
      }

      // TT move first
      if (haveTT && m == ttMove)
      {
        stage = ORDER_STAGE_TT;
        base = ORDER_TT_BONUS;
      }
      // Captures next (good first)
      else if (isCap)
      {
        const int mvv = mvv_lva_fast(pos.position(), m);
        // treat recaptures or big victims as "good", even if SEE<0
        const bool bigVictim = (capPt == chess::PieceType::Rook || capPt == chess::PieceType::Queen);
        const bool good = seeGoodLocal || isRecap || bigVictim || isPromo;

        if (good)
        {
          stage = ORDER_STAGE_GOOD_CAP;
          // lightweight capture history used here is optional; keep MVV as core
          base = ORDER_GOOD_CAPTURE_BASE + mvv;
        }
        else
        {
          stage = ORDER_STAGE_BAD_CAP;
          base = ORDER_BAD_CAPTURE_BASE + mvv;
        }
      }
      // Quiet promotions are high-priority quiets
      else if (isPromo)
      {
        stage = ORDER_STAGE_KILLER_CM_QP;
        base = ORDER_PROMO_BASE;
      }
      // Killers & countermove
      else if (m == killers[kply][0] || m == killers[kply][1])
      {
        stage = ORDER_STAGE_KILLER_CM_QP;
        base = ORDER_KILLER_BASE;
      }
      else
      {
        const chess::Move cm = (prevOk ? counterMove[prev.from()][prev.to()] : chess::Move{});
        if (prevOk && m == cm)
        {
          stage = ORDER_STAGE_KILLER_CM_QP;
          base = ORDER_COUNTERMOVE_BASE + (counterHist[prev.from()][prev.to()] >> COUNTERMOVE_HIST_BLEND_SHIFT);
        }
        else
        {
          // regular quiet: history + quietHist + a tiny malus for heavy non-tactical shuffles
          auto moverOpt = board.getPiece(m.from());
          const chess::PieceType moverPt = moverOpt ? moverOpt->type : chess::PieceType::Pawn;
          base = history[m.from()][m.to()] + (quietHist[chess::bb::type_index(moverPt)][m.to()] >> QUIET_HIST_BLEND_SHIFT);

          if (moverPt == chess::PieceType::Queen || moverPt == chess::PieceType::Rook)
          {
            base -= ORDER_HEAVY_QUIET_MALUS;
          }
          stage = ORDER_STAGE_QUIET;
        }
      }

      {
        const auto sig = compute_quiet_signals(pos, m);
        if (sig.givesCheck)
        {
          if (stage < ORDER_STAGE_KILLER_CM_QP)
            stage = ORDER_STAGE_KILLER_CM_QP;
          base += ORDER_CHECK_BONUS;
        }
        else if (sig.pawnSignal > 0 || sig.pieceSignal > 0)
        {
          if (stage < ORDER_STAGE_KILLER_CM_QP)
            stage = ORDER_STAGE_KILLER_CM_QP;
          base += ORDER_THREAT_BONUS;
        }
      }

      scores[i] = stage * ORDER_BUCKET + base;
      ordered[i] = m;
    }

    sort_by_score_desc(scores, ordered, n);

    const bool allowFutility = !inCheck && !isPV;
    int moveCount = 0;
    bool searchedAny = false;

    for (int idx = 0; idx < n; ++idx)
    {
      if ((idx & STOP_POLL_MASK) == 0)
        check_stop(stopFlag);

      const chess::Move m = ordered[idx];
      if (excludedMove && m == *excludedMove)
      {
        continue; // don’t skew LMR/LMP with a non-searched move
      }

      const bool isQuiet = !m.isCapture() && (m.promotion() == chess::PieceType::None);
      const auto us = pos.getState().sideToMove;

      bool doThreatSignals = cfg.useThreatSignals && depth <= cfg.threatSignalsDepthMax &&
                             moveCount < cfg.threatSignalsQuietCap;

      if (isQuiet && doThreatSignals)
      {
        if (history[m.from()][m.to()] < cfg.threatSignalsHistMin)
          doThreatSignals = false;
      }

      bool passed_push = false;
      if (isQuiet)
      {
        if (auto mover = board.getPiece(m.from()); mover && mover->type == chess::PieceType::Pawn)
        {
          passed_push = is_advanced_passed_pawn_push(board, m, us);
        }
      }

      // Always detect true checks for quiet moves (even if threat signals are gated)
      int pawn_sig = 0, piece_sig = 0;
      bool wouldCheck = false;
      if (isQuiet)
      {
        const auto signals = compute_quiet_signals(pos, m);
        piece_sig = signals.pieceSignal; // detects direct checks (==2)

        const bool allowPawnThreats = doThreatSignals && piece_sig < QUIET_SIGNAL_CHECK;

        if (piece_sig < QUIET_SIGNAL_CHECK)
        {
          if (allowPawnThreats)
            pawn_sig = signals.pawnSignal;
          if (is_advanced_passed_pawn_push(board, m, us))
            passed_push = true;
        }

        wouldCheck = signals.givesCheck;
        if (wouldCheck)
        {
          piece_sig = std::max(piece_sig, int(QUIET_SIGNAL_CHECK));
          if (auto mover = board.getPiece(m.from()); mover && mover->type == chess::PieceType::Pawn)
          {
            pawn_sig = std::max(pawn_sig, int(QUIET_SIGNAL_CHECK));
          }
        }
      }
      if (passed_push)
        pawn_sig = std::max(pawn_sig, int(QUIET_SIGNAL_THREAT));
      const int qp_sig = pawn_sig;
      const int qpc_sig = piece_sig;
      const bool tacticalQuiet = (qp_sig > 0) || (qpc_sig > 0);

      // pre info
      auto moverOpt = board.getPiece(m.from());
      const chess::PieceType moverPt = moverOpt ? moverOpt->type : chess::PieceType::Pawn;
      chess::PieceType capPt = chess::PieceType::Pawn;

      const bool isQuietHeavy =
          isQuiet && (moverPt == chess::PieceType::Queen || moverPt == chess::PieceType::Rook);

      if (m.isEnPassant())
        capPt = chess::PieceType::Pawn;
      else if (m.isCapture())
      {
        if (auto cap = board.getPiece(m.to()))
          capPt = cap->type;
      }
      const int capValPre = m.isCapture() ? (m.isEnPassant() ? base_value[(int)chess::PieceType::Pawn]
                                                             : base_value[(int)capPt])
                                          : 0;

      // LMP (contHist-aware) - don't LMP quiet checks
      if (!tacticalNode && !inCheck && !isPV && isQuiet && depth <= QUIET_PRUNE_MAX_DEPTH && !tacticalQuiet &&
          !isQuietHeavy && !wouldCheck)
      {
        int hist = history[m.from()][m.to()] + (quietHist[chess::bb::type_index(moverPt)][m.to()] >> QUIET_HIST_BLEND_SHIFT);

        int ch = 0;
        if (ply >= 1)
        {
          const auto pm1 = prevMove[cap_ply(ply - 1)];
          if (pm1.from() >= 0 && pm1.to() >= 0 && pm1.to() < 64)
          {
            if (auto po1 = board.getPiece(pm1.to()))
              ch = contHist[0][chess::bb::type_index(po1->type)][pm1.to()][chess::bb::type_index(moverPt)][m.to()];
          }
        }

        int limit = LMP_LIMIT[depth];
        if (hist < BAD_HISTORY_THRESHOLD)
          limit -= 1;
        if (ch < BAD_HISTORY_THRESHOLD)
          limit -= 1;
        if (limit < 1)
          limit = 1;

        int futMarg = FUT_MARGIN[depth] + (improving ? LMP_IMPROVING_BONUS : 0);
        if (staticEval + futMarg <= alpha + LMP_ALPHA_SLACK && moveCount >= limit)
        {
          ++moveCount;
          continue;
        }
      }

      // Extra move-count-based pruning for very late quiets
      if (!tacticalNode && !inCheck && !isPV && isQuiet && depth <= QUIET_PRUNE_MAX_DEPTH && !tacticalQuiet)
      {
        if (moveCount >= QUIET_LATE_PRUNE_BASE + QUIET_LATE_PRUNE_DEPTH_FACTOR * depth)
        { // after many tries, bail
          ++moveCount;
          continue;
        }
      }

      // Extended futility (depth<=3, quiets) - don't prune quiet checks
      if (allowFutility && isQuiet && depth <= QUIET_PRUNE_MAX_DEPTH && !tacticalQuiet && !isQuietHeavy &&
          !tacticalNode && !wouldCheck)
      {
        int fut = FUT_MARGIN[depth] +
                  (history[m.from()][m.to()] < BAD_HISTORY_THRESHOLD ? EXT_FUTILITY_NEG_HISTORY_BONUS : 0);
        if (improving)
          fut += EXT_FUTILITY_IMPROVING_BONUS;
        if (staticEval + fut <= alpha)
        {
          ++moveCount;
          continue;
        }
      }

      // History pruning - don't prune quiet checks
      if (!tacticalNode && !inCheck && !isPV && isQuiet && depth <= HISTORY_PRUNE_MAX_DEPTH && !tacticalQuiet &&
          !isQuietHeavy && !improving && !wouldCheck)
      {
        int histScore = history[m.from()][m.to()] + (quietHist[chess::bb::type_index(moverPt)][m.to()] >> QUIET_HIST_BLEND_SHIFT);
        if (histScore < BAD_HISTORY_PRUNE_THRESHOLD && m != killers[kply][0] && m != killers[kply][1] &&
            (!prevOk || m != cm))
        {
          ++moveCount;
          continue;
        }
      }

      // Futility (D1) - don't prune quiet checks
      if (!inCheck && !isPV && isQuiet && depth == 1 && !tacticalQuiet && !isQuietHeavy &&
          !improving && !wouldCheck)
      {
        if (staticEval + FUT_MARGIN[1] <= alpha)
        {
          ++moveCount;
          continue;
        }
      }
      // SEE once if needed (do not prune losing captures; just mark them "bad")
      bool seeGood = true;
      if (m.isCapture() && m.promotion() == chess::PieceType::None)
      {
        auto moverOptQ = board.getPiece(m.from());
        const chess::PieceType attackerPtQ = moverOptQ ? moverOptQ->type : chess::PieceType::Pawn;

        (void)attackerPtQ;
        // classify capture quality once for later (probcut / reductions / staging)
        seeGood = pos.see(m);
      }

      const int mvvBefore =
          (m.isCapture() || m.promotion() != chess::PieceType::None) ? mvv_lva_fast(pos.position(), m) : 0;
      int newDepth = depth - 1;

      // ----- Singular Extension -----
      int seExt = 0;
      if (cfg.useSingularExt && haveTT && m == ttMove && !inCheck && depth >= SINGULAR_MIN_DEPTH)
      {
        // Only trust a LOWER bound close to current depth
        const bool ttGood =
            (ttBound == Bound::Lower) && (ttStoredDepth >= depth - 1) && !is_mate_score(ttVal);
        if (ttGood)
        {
          const int R = (depth >= SINGULAR_DEEP_DEPTH ? SINGULAR_REDUCTION_DEEP : SINGULAR_REDUCTION_SHALLOW);
          const int margin = SINGULAR_MARGIN_BASE + SINGULAR_MARGIN_DEPTH_MULT * depth;
          const int singBeta = ttVal - margin;

          if (singBeta > -MATE + SINGULAR_MATE_FLOOR_MARGIN)
          {
            chess::Move dummy{};
            const int sDepth = std::max(1, depth - 1 - R);
            int s = negamax(pos, sDepth, singBeta - 1, singBeta, ply, dummy, staticEval, &m);
            if (s < singBeta)
              seExt = SINGULAR_EXTENSION;
          }
        }
      }
      newDepth += seExt;

      int pm1_to = -1, pm2_to = -1, pm3_to = -1;
      int pm1_pt = -1, pm2_pt = -1, pm3_pt = -1;
      if (ply >= 1)
      {
        const chess::Move pm1 = prevMove[cap_ply(ply - 1)];
        if (pm1.from() >= 0 && pm1.to() >= 0 && pm1.from() < 64 && pm1.to() < 64)
        {
          if (auto p = board.getPiece(pm1.to()))
          {
            pm1_to = pm1.to();
            pm1_pt = chess::bb::type_index(p->type);
          }
        }
      }
      if (ply >= 2)
      {
        const chess::Move pm2 = prevMove[cap_ply(ply - 2)];
        if (pm2.from() >= 0 && pm2.to() >= 0 && pm2.from() < 64 && pm2.to() < 64)
        {
          if (auto p = board.getPiece(pm2.to()))
          {
            pm2_to = pm2.to();
            pm2_pt = chess::bb::type_index(p->type);
          }
        }
      }
      if (ply >= 3)
      {
        const chess::Move pm3 = prevMove[cap_ply(ply - 3)];
        if (pm3.from() >= 0 && pm3.to() >= 0 && pm3.from() < 64 && pm3.to() < 64)
        {
          if (auto p = board.getPiece(pm3.to()))
          {
            pm3_to = pm3.to();
            pm3_pt = chess::bb::type_index(p->type);
          }
        }
      }

      MoveUndoGuard g(pos);
      if (!g.doMove(m))
      {
        ++moveCount;
        continue;
      }

      prevMove[cap_ply(ply)] = m;
      tt.prefetch(pos.hash());

      int value;
      chess::Move childBest{};

      // ProbCut (capture-only)
      if (!isPV && !inCheck && newDepth >= PROBCUT_MIN_DEPTH && m.isCapture() &&
          seeGood && mvvBefore >= PROBCUT_MIN_MVV)
      {
        if (staticEval + capValPre + PROBCUT_MARGIN >= beta)
        {
          const int pcDepth = std::max(1, newDepth - PROBCUT_REDUCTION);
          const int probe =
              -negamax(pos, pcDepth, -beta, -(beta - 1), ply + 1, childBest, -staticEval);
          if (probe >= beta)
            return beta;
        }
      }

      // Check extension (light)
      const bool givesCheck = pos.lastMoveGaveCheck();
      if (givesCheck)
      {
        if (!isQuiet)
        {
          // CAPTURING CHECK: extend only if SEE is >= 0, or it's a recapture, or a big victim
          bool allowCaptureExt = false;
          const int attackerVal = base_value[(int)moverPt];
          const int victimVal = capValPre;

          const chess::Move pm = (ply > 0 ? prevMove[cap_ply(ply - 1)] : chess::Move{});
          const bool isRecap = (!pm.isNull() && pm.to() == m.to());

          if (pos.see(m))
            allowCaptureExt = true;
          else if (isRecap)
            allowCaptureExt = true;
          else if (victimVal >= base_value[(int)chess::PieceType::Rook])
            allowCaptureExt = true;

          if (allowCaptureExt)
            newDepth += CAPTURE_CHECK_EXTENSION;
        }
        else
        {
          // QUIET CHECK: only very lightly extend and only in PV nodes (and not queen shuffles)
          bool okQuietExt =
              isPV && depth <= QUIET_CHECK_EXTENSION_MAX_DEPTH &&
              (history[m.from()][m.to()] > QUIET_CHECK_EXTENSION_MIN_HISTORY) &&
              (moverPt != chess::PieceType::Queen);
          if (okQuietExt)
            newDepth += QUIET_CHECK_EXTENSION;
        }
      }
      if (passed_push && isQuiet)
        newDepth += PASSED_PUSH_EXTENSION;

      // Bad capture reduction
      int reduction = 0;
      if (!seeGood && m.isCapture() && newDepth >= 2)
        reduction = std::min(1, newDepth - BAD_CAPTURE_REDUCTION);

      // PVS / LMR
      if (moveCount == 0)
      {
        value = -negamax(pos, newDepth, -beta, -alpha, ply + 1, childBest, -staticEval);
      }
      else
      {
        if (cfg.useLMR && isQuiet && !tacticalQuiet && !inCheck && !givesCheck && newDepth >= 2 &&
            moveCount >= LMR_MIN_MOVE_INDEX)
        {
          const int ld = ilog2_u32((unsigned)depth);
          const int lm = ilog2_u32((unsigned)(moveCount + 1));
          int r = (ld * (lm + 1)) / 2;
          if (tacticalNode)
            r = std::max(0, r - 1);
          if (isQuietHeavy)
            r = std::max(0, r - 1);
          const int h = history[m.from()][m.to()] + (quietHist[chess::bb::type_index(moverPt)][m.to()] >> QUIET_HIST_BLEND_SHIFT);
          int ch = 0;
          if (ply >= 1)
          {
            const auto pm1 = prevMove[cap_ply(ply - 1)];
            if (pm1.from() >= 0 && pm1.to() >= 0 && pm1.to() < 64)
            {
              if (auto po1 = board.getPiece(pm1.to()))
                ch = contHist[0][chess::bb::type_index(po1->type)][pm1.to()][chess::bb::type_index(moverPt)][m.to()];
            }
          }

          if (h > LMR_GOOD_HISTORY_THRESHOLD)
            r -= 1;
          if (ch > LMR_GOOD_CONT_HISTORY_THRESHOLD)
            r -= 1;
          if (m == killers[kply][0] || m == killers[kply][1])
            r -= 1;
          if (haveTT && m == ttMove)
            r -= 1;
          if (ply <= LMR_SHALLOW_PLY)
            r -= 1;
          if (beta - alpha <= LMR_NARROW_WINDOW)
            r -= 1;
          if (!improving)
            r += 1;
          if (qpc_sig == QUIET_SIGNAL_CHECK /* quiet check */)
            r = std::max(0, r - 1);
          if (qpc_sig == QUIET_SIGNAL_THREAT /* tactical quiet via signals */)
            r = std::max(0, r - 1);

          // avoid reducing the first 3 quiets at shallow depth
          if (newDepth <= LMR_NO_REDUCE_SHALLOW_DEPTH && moveCount < LMR_NO_REDUCE_FIRST_MOVES)
            r = 0;

          if (r < 0)
            r = 0;
          int rCap = (newDepth >= LMR_DEEP_DEPTH ? LMR_REDUCTION_CAP_DEEP : LMR_REDUCTION_CAP_SHALLOW);
          if (r > rCap)
            r = rCap;
          reduction = std::min(r, newDepth - 1);
        }

        value =
            -negamax(pos, newDepth - reduction, -alpha - 1, -alpha, ply + 1, childBest, -staticEval);
        if (value > alpha && value < beta)
        {
          value = -negamax(pos, newDepth, -beta, -alpha, ply + 1, childBest, -staticEval);
        }
      }

      value = std::clamp(value, -MATE + 1, MATE - 1);
      searchedAny = true;

      // History updates
      if (isQuiet && value <= origAlpha)
      {
        const int M = hist_bonus(depth) >> FAIL_LOW_HISTORY_SHIFT;

        hist_update(history[m.from()][m.to()], -M);
        hist_update(quietHist[chess::bb::type_index(moverPt)][m.to()], -M);

        if (pm1_to >= 0 && pm1_pt >= 0)
          hist_update(contHist[0][pm1_pt][pm1_to][chess::bb::type_index(moverPt)][m.to()], -M);
        if (pm2_to >= 0 && pm2_pt >= 0)
          hist_update(contHist[1][pm2_pt][pm2_to][chess::bb::type_index(moverPt)][m.to()], -(M >> CONTHIST_MALUS_SHIFT_L1));
        if (pm3_to >= 0 && pm3_pt >= 0)
          hist_update(contHist[2][pm3_pt][pm3_to][chess::bb::type_index(moverPt)][m.to()], -(M >> CONTHIST_MALUS_SHIFT_L2));
      }

      if (value > best)
      {
        best = value;
        bestLocal = m;
      }
      if (value > alpha)
        alpha = value;

      if (alpha >= beta)
      {
        if (isQuiet)
        {
          killers[kply][1] = killers[kply][0];
          killers[kply][0] = m;

          const int B = hist_bonus(depth);

          hist_update(history[m.from()][m.to()], +B);
          hist_update(quietHist[chess::bb::type_index(moverPt)][m.to()], +B);

          if (pm1_to >= 0 && pm1_pt >= 0)
            hist_update(contHist[0][pm1_pt][pm1_to][chess::bb::type_index(moverPt)][m.to()], +B);
          if (pm2_to >= 0 && pm2_pt >= 0)
            hist_update(contHist[1][pm2_pt][pm2_to][chess::bb::type_index(moverPt)][m.to()], +(B >> CONTHIST_BONUS_SHIFT_L1));
          if (pm3_to >= 0 && pm3_pt >= 0)
            hist_update(contHist[2][pm3_pt][pm3_to][chess::bb::type_index(moverPt)][m.to()], +(B >> CONTHIST_BONUS_SHIFT_L2));

          if (prevOk)
          {
            counterMove[prev.from()][prev.to()] = m;
            hist_update(counterHist[prev.from()][prev.to()], +B);
          }
        }
        else
        {
          hist_update(captureHist[chess::bb::type_index(moverPt)][m.to()][chess::bb::type_index(capPt)], +hist_bonus(depth));
        }
        break;
      }
      ++moveCount;
    }

    // --- 5) Early ProbCut pass (cheap capture-only skim) ---
    if (!isPV && !inCheck && depth >= EARLY_PROBCUT_MIN_DEPTH)
    {
      const int MAX_SCAN = std::min(n, EARLY_PROBCUT_MAX_SCAN);

      for (int idx = 0; idx < MAX_SCAN; ++idx)
      {
        const chess::Move m = ordered[idx];
        if (!m.isCapture())
          continue;
        if (mvv_lva_fast(pos.position(), m) < PROBCUT_MIN_MVV)
          continue;

        MoveUndoGuard pcg(pos);
        if (!pcg.doMove(m))
          continue;

        const int childSE = signed_eval(pos); // opponent POV
        if (-childSE + EARLY_PROBCUT_MARGIN >= beta)
        { // flip the sign
          chess::Move tmp{};
          const int probe = -negamax(pos, depth - EARLY_PROBCUT_REDUCTION, -beta, -(beta - 1), ply + 1, tmp, INF);
          pcg.rollback();
          if (probe >= beta)
            return beta;
        }
        else
        {
          pcg.rollback();
        }
      }
    }

    // If this was a singular-extension verification search that excluded a move and
    // we ended up searching nothing (e.g., the excluded TT move is the only legal move),
    // return a clear fail-low instead of 0/stalemate. This lets SE trigger correctly.
    if (excludedMove && !searchedAny)
    {
      return -INF + 1;
    }

    // safety: never leave node without searching at least one move (non-check)
    if (!searchedAny)
    {
      for (int idx = 0; idx < n; ++idx)
      {
        const chess::Move m = ordered[idx];
        if (excludedMove && m == *excludedMove)
          continue;
        MoveUndoGuard g(pos);
        if (!g.doMove(m))
          continue;

        chess::Move childBest{};
        int value = -negamax(pos, depth - 1, -beta, -alpha, ply + 1, childBest, -staticEval);
        value = std::clamp(value, -MATE + 1, MATE - 1);

        best = value;
        bestLocal = m;
        if (value > alpha)
          alpha = value;
        break;
      }
    }

    if (best == -INF)
    {
      if (inCheck)
        return mated_in(ply);
      return 0;
    }

    if (!(stopFlag && stopFlag->load()))
    {
      Bound bnd;
      if (best <= origAlpha)
        bnd = Bound::Upper;
      else if (best >= origBeta)
        bnd = Bound::Lower;
      else
        bnd = Bound::Exact;

      int16_t storeSE = inCheck ? SE_UNSET : (int16_t)staticEval;

      tt.store(pos.hash(), encode_tt_score(best, cap_ply(ply)), static_cast<int16_t>(depth), bnd,
               bestLocal, storeSE);
    }

    refBest = bestLocal;
    return best;
  }

  std::vector<chess::Move> Search::build_pv_from_tt(SearchPosition pos, int max_len)
  {
    std::vector<chess::Move> pv;
    std::unordered_set<uint64_t> seen;
    pv.reserve(max_len);
    seen.reserve(max_len);

    for (int i = 0; i < max_len; ++i)
    {
      TTEntry5 tte{};
      if (!tt.probe_into(pos.hash(), tte))
        break;

      chess::Move m = tte.best;
      if (m.from() == m.to())
        break;

      if (!pos.doMove(m))
        break;
      pv.push_back(m);

      uint64_t h = pos.hash();
      if (!seen.insert(h).second)
        break; // loop guard
    }
    return pv;
  }
  int Search::search_root_single(SearchPosition &pos, int maxDepth,
                                 std::shared_ptr<std::atomic<bool>> stop, std::uint64_t maxNodes)
  {
    NodeFlushGuard node_guard(sharedNodes);
    this->stopFlag = stop;
    if (!this->sharedNodes)
      this->sharedNodes = std::make_shared<std::atomic<std::uint64_t>>(0);
    if (maxNodes)
      this->nodeLimit = maxNodes;

    reset_node_batch();

    stats = SearchStats{};
    auto t0 = steady_clock::now();
    auto update_time_stats = [&]
    {
      auto now = steady_clock::now();
      std::uint64_t ms =
          (std::uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(now - t0).count();
      stats.elapsedMs = ms;
      stats.nps = (ms ? (double)stats.nodes / (ms / 1000.0) : (double)stats.nodes);
    };

    try
    {
      std::vector<chess::Move> rootMoves;
      mg.generatePseudoLegalMoves(pos.getBoard(), pos.getState(), rootMoves);
      if (!rootMoves.empty())
      {
        std::vector<chess::Move> legalRoot;
        legalRoot.reserve(rootMoves.size());
        for (const auto &m : rootMoves)
        {
          MoveUndoGuard guard(pos);
          if (guard.doMove(m))
          {
            legalRoot.push_back(m);
            guard.rollback();
          }
        }
        rootMoves.swap(legalRoot);
      }
      if (rootMoves.empty())
      {
        stats.nodes = flush_node_batch(sharedNodes);
        update_time_stats();
        this->stopFlag.reset();
        const int score = pos.inCheck() ? mated_in(0) : 0;
        stats.bestScore = score;
        stats.bestMove = chess::Move{};
        stats.bestPV.clear();
        stats.topMoves.clear();
        return score;
      }

      auto score_root_move = [&](const chess::Move &m, const chess::Move &ttMove, bool haveTT,
                                 int curDepth)
      {
        int s = 0;

        if (haveTT && m == ttMove)
          s += ROOT_ORDER_TT_BONUS;

        if (m.promotion() != chess::PieceType::None)
        {
          s += ROOT_ORDER_PROMO_BONUS;
        }
        else if (m.isCapture())
        {
          s += ROOT_ORDER_CAPTURE_BASE + mvv_lva_fast(pos.position(), m);
        }
        else
        {
          // quiet move
          const auto &board = pos.getBoard();
          int h = history[m.from()][m.to()];
          h = std::clamp(h, -ROOT_ORDER_HISTORY_CLAMP, ROOT_ORDER_HISTORY_CLAMP);
          s += h;

          // Threat-signals / checks: always detect checks; other signals gated
          auto mover = board.getPiece(m.from());
          if (mover)
          {
            const auto signals = compute_quiet_signals(pos, m);
            int piece_sig = signals.pieceSignal; // detects direct checks (==2)
            int pawn_sig = 0;

            bool doThreat = cfg.useThreatSignals && curDepth <= cfg.threatSignalsDepthMax &&
                            h >= cfg.threatSignalsHistMin;

            const bool allowPawnThreats = doThreat && piece_sig < QUIET_SIGNAL_CHECK;
            if (allowPawnThreats)
              pawn_sig = signals.pawnSignal;

            if (signals.givesCheck)
            {
              piece_sig = std::max(piece_sig, int(QUIET_SIGNAL_CHECK));
              if (mover->type == chess::PieceType::Pawn)
                pawn_sig = std::max(pawn_sig, int(QUIET_SIGNAL_CHECK));
            }

            const int sig = std::max(pawn_sig, piece_sig);
            if (sig == QUIET_SIGNAL_CHECK)
              s += ROOT_ORDER_CHECK_BONUS;
            else if (sig == QUIET_SIGNAL_THREAT)
              s += ROOT_ORDER_THREAT_BONUS;
          }
        }
        return s;
      };

      struct RootLine
      {
        chess::Move m{};
        int score = -INF; // exact if full-rescored, else bound
        Bound bound = Bound::Upper;
        int ordIdx = 0; // stable order index
        bool exactFull = false;
      };

      // aspiration seed
      int lastScore = 0;
      if (cfg.useAspiration)
      {
        TTEntry5 tte{};
        if (tt.probe_into(pos.hash(), tte))
          lastScore = decode_tt_score(tte.value, /*ply=*/0);
      }

      chess::Move prevBest{};
      const int maxD = std::max(1, maxDepth);

      for (int depth = 1; depth <= maxD; ++depth)
      {
        if (stop && stop->load(std::memory_order_relaxed))
          break;

        if (depth > 1)
          decay_tables(*this, /*shift=*/HISTORY_DECAY_SHIFT);

        // TT move only as soft hint
        chess::Move ttMove{};
        bool haveTT = false;
        if (TTEntry5 tte{}; tt.probe_into(pos.hash(), tte))
        {
          haveTT = true;
          ttMove = tte.best;
        }

        // order root moves (stable)
        struct Scored
        {
          chess::Move m;
          int s;
        };
        std::vector<Scored> scored;
        scored.reserve(rootMoves.size());
        for (const auto &m : rootMoves)
          scored.push_back({m, score_root_move(m, ttMove, haveTT, depth)});
        std::stable_sort(scored.begin(), scored.end(), [](const Scored &a, const Scored &b)
                         {
        if (a.s != b.s) return a.s > b.s;
        if (a.m.from() != b.m.from()) return a.m.from() < b.m.from();
        return a.m.to() < b.m.to(); });
        for (std::size_t i = 0; i < scored.size(); ++i)
          rootMoves[i] = scored[i].m;

        // push previous best to front for stability
        if (prevBest.from() != prevBest.to())
        {
          auto it = std::find(rootMoves.begin(), rootMoves.end(), prevBest);
          if (it != rootMoves.end())
            std::rotate(rootMoves.begin(), it, it + 1);
        }

        // aspiration window
        int alphaTarget = -INF + 1, betaTarget = INF - 1;
        int window = ASPIRATION_INITIAL_WINDOW;
        if (cfg.useAspiration && depth >= ASPIRATION_MIN_DEPTH && !is_mate_score(lastScore))
        {
          window = std::max(ASPIRATION_MIN_WINDOW, cfg.aspirationWindow);
          alphaTarget = lastScore - window;
          betaTarget = lastScore + window;
        }

        int bestScore = -INF;
        chess::Move bestMove{};

        while (true)
        {
          if (stop && stop->load(std::memory_order_relaxed))
            break;

          int alpha = alphaTarget, beta = betaTarget;
          std::vector<RootLine> lines;
          lines.reserve(rootMoves.size());

          int moveIdx = 0;
          for (const auto &m : rootMoves)
          {
            if (stop && stop->load(std::memory_order_relaxed))
              break;

            const bool isQuietRoot = !m.isCapture() && (m.promotion() == chess::PieceType::None);
            const QuietSignals rootSignals =
                isQuietRoot ? compute_quiet_signals(pos, m) : QuietSignals{};
            const bool quietCheckRoot = isQuietRoot && rootSignals.givesCheck;

            MoveUndoGuard rg(pos);
            if (!rg.doMove(m))
            {
              ++moveIdx;
              continue;
            }
            tt.prefetch(pos.hash());

            chess::Move childBest{};
            int s;

            if (moveIdx == 0)
            {
              // full window for first (PVS root)
              s = -negamax(pos, depth - 1, -beta, -alpha, 1, childBest, INF);
            }
            else
            {
              // Root Move Reductions (light) + PVS
              int r = 0;
              const bool rootIsCapture = m.isCapture();
              const bool rootIsPromo = (m.promotion() != chess::PieceType::None);
              if (rootIsCapture || rootIsPromo)
                r = 0; // never reduce tactical roots
              else if (depth >= ROOT_LMR_MIN_DEPTH)
              {
                int hist = history[m.from()][m.to()];
                bool isQuietRoot = !m.isCapture() && (m.promotion() == chess::PieceType::None);

                // Base reduction for later root moves
                if (isQuietRoot)
                  r = ROOT_LMR_BASE_REDUCTION;
                if (depth >= ROOT_LMR_DEEP_DEPTH)
                  r++;
                if (moveIdx >= ROOT_LMR_LATE_MOVE_INDEX)
                  r++;
                if (hist < 0)
                  r++;

                // Slight preference for quiet checks: reduce one step less, but never to zero just
                // because it checks
                if (isQuietRoot && quietCheckRoot)
                  r = std::max(0, r - 1);

                if (depth <= ROOT_LMR_SHALLOW_DEPTH)
                  r = std::max(0, r - 1);
                r = std::clamp(r, 0, depth - 2);
              }

              if (r > 0)
              {
                s = -negamax(pos, (depth - 1) - r, -(alpha + 1), -alpha, 1, childBest, INF);
                if (s > alpha)
                {
                  s = -negamax(pos, depth - 1, -(alpha + 1), -alpha, 1, childBest, INF);
                  if (s > alpha && s < beta)
                    s = -negamax(pos, depth - 1, -beta, -alpha, 1, childBest, INF);
                }
              }
              else
              {
                s = -negamax(pos, depth - 1, -(alpha + 1), -alpha, 1, childBest, INF);
                if (s > alpha && s < beta)
                  s = -negamax(pos, depth - 1, -beta, -alpha, 1, childBest, INF);
              }
            }

            s = std::clamp(s, -MATE + 1, MATE - 1);
            Bound b = Bound::Exact;
            if (s <= alpha)
              b = Bound::Upper;
            else if (s >= beta)
              b = Bound::Lower;

            lines.push_back(RootLine{m, s, b, moveIdx, /*exactFull*/ false});

            if (s > bestScore)
            {
              bestScore = s;
              bestMove = m;
            }
            if (s > alpha)
              alpha = s;

            rg.rollback();
            ++moveIdx;
            if (alpha >= beta)
              break;
          }

          // success if inside window
          if (bestScore > alphaTarget && bestScore < betaTarget)
          {
            auto full_rescore = [&](RootLine &rl)
            {
              MoveUndoGuard rg(pos);
              if (!rg.doMove(rl.m))
                return;
              chess::Move dummy{};
              int exact = -negamax(pos, depth - 1, -INF + 1, INF - 1, 1, dummy, INF);
              rl.score = std::clamp(exact, -MATE + 1, MATE - 1);
              rl.bound = Bound::Exact;
              rl.exactFull = true;
            };

            for (auto &rl : lines)
              if (rl.m == bestMove)
              {
                full_rescore(rl);
                break;
              }

            // Only rescore other moves if cfg.fullRescoreTopK > 1
            if (cfg.fullRescoreTopK > 1)
            {
              std::stable_sort(lines.begin(), lines.end(), [](const RootLine &a, const RootLine &b)
                               {
              if (a.score != b.score) return a.score > b.score;
              return a.ordIdx < b.ordIdx; });
              int rescored = 1;
              for (auto &rl : lines)
              {
                if (rescored >= cfg.fullRescoreTopK)
                  break;
                if (rl.m == bestMove)
                  continue;
                full_rescore(rl);
                ++rescored;
              }
            }

            // pick final best (exact first, then score, then ordIdx)
            auto rank_bound = [](Bound b)
            {
              switch (b)
              {
              case Bound::Exact:
              case Bound::Lower:
                return 2;
              case Bound::Upper:
              default:
                return 1;
              }
            };
            std::stable_sort(lines.begin(), lines.end(), [&](const RootLine &a, const RootLine &b)
                             {
            const int ra = rank_bound(a.bound), rb = rank_bound(b.bound);
            if (ra != rb) return ra > rb;
            if (a.score != b.score) return a.score > b.score;
            return a.ordIdx < b.ordIdx; });

            if (!lines.empty() && lines.front().bound != Bound::Exact)
            {
              full_rescore(lines.front());
              std::stable_sort(lines.begin(), lines.end(), [&](const RootLine &a, const RootLine &b)
                               {
              const int ra = rank_bound(a.bound), rb = rank_bound(b.bound);
              if (ra != rb) return ra > rb;
              if (a.score != b.score) return a.score > b.score;
              return a.ordIdx < b.ordIdx; });
            }

            const chess::Move finalBest = lines.front().m;
            const int finalScore = lines.front().score;

            // stats & PV
            stats.nodes = flush_node_batch(sharedNodes);
            update_time_stats();

            stats.bestScore = finalScore;
            stats.bestMove = finalBest;
            prevBest = finalBest;

            stats.bestPV.clear();
            {
              SearchPosition tmp = pos;
              if (tmp.doMove(finalBest))
              {
                stats.bestPV.push_back(finalBest);
                auto rest = build_pv_from_tt(tmp, PV_FROM_TT_MAX_LEN);
                for (auto &mv : rest)
                  stats.bestPV.push_back(mv);
              }
            }

            // build exact-only topMoves (best first)
            stats.topMoves.clear();
            stats.topMoves.push_back({finalBest, finalScore});
            for (const auto &rl : lines)
            {
              if ((int)stats.topMoves.size() >= ROOT_TOP_MOVES_MAX)
                break;
              if (rl.m == finalBest)
                continue;
              if (rl.bound == Bound::Exact)
                stats.topMoves.push_back({rl.m, rl.score});
            }
            if (stats.topMoves.size() > 1)
            {
              std::stable_sort(stats.topMoves.begin() + 1, stats.topMoves.end(),
                               [](const auto &a, const auto &b)
                               { return a.second > b.second; });
            }

            break; // depth done
          }

          // widen window
          if (bestScore <= alphaTarget)
          {
            int step = std::max(ASPIRATION_WIDEN_MIN_STEP, window);
            alphaTarget = std::max(-INF + 1, alphaTarget - step);
            window += step / 2;
          }
          else if (bestScore >= betaTarget)
          {
            int step = std::max(ASPIRATION_WIDEN_MIN_STEP, window);
            betaTarget = std::min(INF - 1, betaTarget + step);
            window += step / 2;
          }
          else
          {
            break; // shouldn't happen
          }
        } // aspiration loop

        if (is_mate_score(stats.bestScore))
          break;
        lastScore = stats.bestScore;
      } // depth loop

      stats.nodes = flush_node_batch(sharedNodes);
      update_time_stats();
      this->stopFlag.reset();
      return stats.bestScore;
    }
    catch (const SearchStoppedException &)
    {
      // Ensure final stats are coherent on timeout/stop:
      stats.nodes = sharedNodes ? sharedNodes->load(std::memory_order_relaxed) : 0;
      auto now = steady_clock::now();
      std::uint64_t ms =
          (std::uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(now - t0).count();
      stats.elapsedMs = ms;
      stats.nps = (ms ? (double)stats.nodes / (ms / 1000.0) : (double)stats.nodes);
      this->stopFlag.reset();
      return stats.bestScore; // return the last known best score
    }
  }

  int Search::search_root_lazy_smp(SearchPosition &pos, int maxDepth,
                                   std::shared_ptr<std::atomic<bool>> stop, int maxThreads,
                                   std::uint64_t maxNodes)
  {
    const int threads = std::max(1, maxThreads > 0 ? std::min(maxThreads, cfg.threads) : cfg.threads);
    try
    {
      tt.new_generation();
    }
    catch (...)
    {
    }

    if (threads <= 1)
      return search_root_single(pos, maxDepth, stop, maxNodes);

    auto &pool = ThreadPool::instance();
    auto sharedCounter = std::make_shared<std::atomic<std::uint64_t>>(0);
    sharedCounter->store(0, std::memory_order_relaxed);
    const auto smpStart = steady_clock::now();

    std::vector<std::unique_ptr<Search>> workers;
    workers.reserve(threads);
    for (int t = 0; t < threads; ++t)
    {
      auto w = std::make_unique<Search>(tt, cfg);
      w->set_thread_id(t);
      w->stopFlag = stop;
      w->set_node_limit(sharedCounter, maxNodes);
      w->copy_heuristics_from(*this);
      workers.emplace_back(std::move(w));
    }
    for (auto &w : workers)
      w->stopFlag = stop;
    this->set_node_limit(sharedCounter, maxNodes);

    int mainScore = 0;

    const SearchPosition rootSnapshot = pos;

    // start helper
    std::vector<std::future<int>> futs;
    futs.reserve(threads - 1);
    for (int t = 1; t < threads; ++t)
    {
      futs.emplace_back(pool.submit([rootSnapshot, &workers, maxDepth, stop, tid = t]
                                    {
      SearchPosition local = rootSnapshot;
      return workers[tid]->search_root_single(local, maxDepth, stop, /*maxNodes*/ 0); }));
    }

    // Main search
    mainScore = this->search_root_single(pos, maxDepth, stop, /*maxNodes*/ 0);

    // Main is done stop helper
    if (stop)
      stop->store(true, std::memory_order_relaxed);

    // wait
    for (auto &f : futs)
    {
      try
      {
        (void)f.get();
      }
      catch (...)
      {
      }
    }

    // fold worker heuristics back into main
    for (int t = 1; t < threads; ++t)
    {
      this->merge_from(*workers[t]);
    }

    // Finalize stats from all threads
    this->stats.nodes = sharedCounter->load(std::memory_order_relaxed);
    const auto ms_total = (std::uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
                              steady_clock::now() - smpStart)
                              .count();
    this->stats.elapsedMs = ms_total;
    this->stats.nps =
        (ms_total ? (double)this->stats.nodes / (ms_total / 1000.0) : (double)this->stats.nodes);
    return mainScore;
  }

  void Search::clearSearchState()
  {
    for (auto &kk : killers)
    {
      kk[0] = chess::Move{};
      kk[1] = chess::Move{};
    }
    for (auto &h : history)
      h.fill(0);
    std::fill(&quietHist[0][0], &quietHist[0][0] + chess::PIECE_TYPE_NB * chess::SQ_NB, 0);
    std::fill(&captureHist[0][0][0], &captureHist[0][0][0] + chess::PIECE_TYPE_NB * chess::SQ_NB * chess::PIECE_TYPE_NB, 0);
    std::fill(&counterHist[0][0], &counterHist[0][0] + chess::SQ_NB * chess::SQ_NB, 0);
    std::memset(contHist, 0, sizeof(contHist));
    for (auto &row : counterMove)
      for (auto &m : row)
        m = chess::Move{};
    for (auto &pm : prevMove)
      pm = chess::Move{};
    stats = SearchStats{};
  }

  void Search::copy_heuristics_from(const Search &src)
  {
    // History-like tables
    history = src.history;

    std::memcpy(quietHist, src.quietHist, sizeof(quietHist));
    std::memcpy(captureHist, src.captureHist, sizeof(captureHist));
    std::memcpy(counterHist, src.counterHist, sizeof(counterHist));
    std::memcpy(counterMove, src.counterMove, sizeof(counterMove));
    for (auto &kk : killers)
    {
      kk[0] = chess::Move{};
      kk[1] = chess::Move{};
    }

    for (auto &pm : prevMove)
      pm = chess::Move{};
  }

  // EMA merge toward the worker's values: G += (L - G) / K
  static LILIA_ALWAYS_INLINE int16_t ema_merge(int16_t G, int16_t L, int K)
  {
    int d = (int)L - (int)G;
    return clamp16((int)G + d / K);
  }

  void Search::merge_from(const Search &o)
  {
    constexpr int K = HEURISTIC_EMA_MERGE_FACTOR;

    // Base history
    for (int f = 0; f < chess::SQ_NB; ++f)
      for (int t = 0; t < chess::SQ_NB; ++t)
        history[f][t] = ema_merge(history[f][t], o.history[f][t], K);

    // Quiet history
    for (int p = 0; p < chess::PIECE_TYPE_NB; ++p)
      for (int t = 0; t < chess::SQ_NB; ++t)
        quietHist[p][t] = ema_merge(quietHist[p][t], o.quietHist[p][t], K);

    // Capture history
    for (int mp = 0; mp < chess::PIECE_TYPE_NB; ++mp)
      for (int t = 0; t < chess::SQ_NB; ++t)
        for (int cp = 0; cp < chess::PIECE_TYPE_NB; ++cp)
          captureHist[mp][t][cp] = ema_merge(captureHist[mp][t][cp], o.captureHist[mp][t][cp], K);

    // Counter history + best countermove choice
    for (int f = 0; f < chess::SQ_NB; ++f)
    {
      for (int t = 0; t < chess::SQ_NB; ++t)
      {
        counterHist[f][t] = ema_merge(counterHist[f][t], o.counterHist[f][t], K);
        if (o.counterHist[f][t] > counterHist[f][t])
        {
          counterMove[f][t] = o.counterMove[f][t];
        }
      }
    }

    // Continuation History (EMA)
    for (int L = 0; L < CONTHIST_LAYERS; ++L)
      for (int pp = 0; pp < chess::PIECE_TYPE_NB; ++pp)
        for (int pt = 0; pt < chess::SQ_NB; ++pt)
          for (int mp = 0; mp < chess::PIECE_TYPE_NB; ++mp)
            for (int to = 0; to < chess::SQ_NB; ++to)
              contHist[L][pp][pt][mp][to] =
                  ema_merge(contHist[L][pp][pt][mp][to], o.contHist[L][pp][pt][mp][to], K);
  }

}
