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
#include "lilia/engine/move_buffer.hpp"
#include "lilia/engine/move_list.hpp"
#include "lilia/engine/move_order.hpp"
#include "lilia/engine/thread_pool.hpp"
#include "lilia/model/core/bitboard.hpp"
#include "lilia/model/core/magic.hpp"

namespace lilia::engine {

using steady_clock = std::chrono::steady_clock;

inline int16_t clamp16(int x) {
  if (x > 32767) return 32767;
  if (x < -32768) return -32768;
  return (int16_t)x;
}

inline int mate_in(int ply) {
  return MATE - ply;
}
inline int mated_in(int ply) {
  return -MATE + ply;
}
inline bool is_mate_score(int s) {
  return std::abs(s) >= MATE_THR;
}
inline int cap_ply(int ply) {
  return ply < 0 ? 0 : (ply >= MAX_PLY ? (MAX_PLY - 1) : ply);
}

inline int encode_tt_score(int s, int ply) {
  if (s >= MATE_THR) return s + ply;
  if (s <= -MATE_THR) return s - ply;
  return s;
}
inline int decode_tt_score(int s, int ply) {
  if (s >= MATE_THR) return s - ply;
  if (s <= -MATE_THR) return s + ply;
  return s;
}

static constexpr int ROOT_VERIFY_MARGIN_BASE = 60;
static constexpr int FUT_MARGIN[4] = {0, 110, 210, 300};
static constexpr int SNMP_MARGINS[4] = {0, 140, 200, 260};
static constexpr int RAZOR_MARGIN_BASE = 240;
static constexpr int RFP_MARGIN_BASE = 190;
static constexpr int LMP_LIMIT[4] = {0, 5, 9, 14};  // D=1..3
static constexpr int LOW_MVV_MARGIN = 360;

namespace {

// ---- Guards ----

struct MoveUndoGuard {
  model::Position& pos;
  bool applied = false;
  explicit MoveUndoGuard(model::Position& p) : pos(p) {}
  bool doMove(const model::Move& m) {
    applied = pos.doMove(m);
    return applied;
  }
  void rollback() {
    if (applied) {
      pos.undoMove();
      applied = false;
    }
  }
  ~MoveUndoGuard() {
    if (applied) pos.undoMove();
  }
};

struct NullUndoGuard {
  model::Position& pos;
  bool applied = false;
  explicit NullUndoGuard(model::Position& p) : pos(p) {}
  bool doNull() {
    applied = pos.doNullMove();
    return applied;
  }
  void rollback() {
    if (applied) {
      pos.undoNullMove();
      applied = false;
    }
  }
  ~NullUndoGuard() {
    if (applied) pos.undoNullMove();
  }
};

inline void check_stop(const std::shared_ptr<std::atomic<bool>>& stopFlag) {
  if (stopFlag && stopFlag->load()) throw SearchStoppedException();
}

// piece index [0..5] for Pawn..King
inline int pidx(core::PieceType pt) {
  using PT = core::PieceType;
  switch (pt) {
    case PT::Pawn:
      return 0;
    case PT::Knight:
      return 1;
    case PT::Bishop:
      return 2;
    case PT::Rook:
      return 3;
    case PT::Queen:
      return 4;
    case PT::King:
      return 5;
    default:
      assert(false && "pidx: unexpected PieceType");
      return 0;  // safe fallback
  }
}

// --- kleine Int-Tools / History-Update ---

static inline int ilog2_u32(unsigned v) {
#if defined(__GNUC__) || defined(__clang__)
  return v ? 31 - __builtin_clz(v) : 0;
#else
  int r = 0;
  while (v >>= 1) ++r;
  return r;
#endif
}
static inline int iabs_int(int x) {
  return x < 0 ? -x : x;
}

static inline int hist_bonus(int depth) {
  unsigned x = (unsigned)(depth * depth) + 1u;
  int lg = ilog2_u32(x);
  return 16 + 8 * lg;  // 16,24,32,40,...
}

template <typename T>
static inline void hist_update(T& h, int bonus) {
  int x = (int)h;
  x += bonus - (x * iabs_int(bonus)) / 32768;
  if (x > 32767) x = 32767;
  if (x < -32768) x = -32768;
  h = (T)x;
}

static inline int gen_all(model::MoveGenerator& mg, model::Position& pos, model::Move* out,
                          int cap) {
  engine::MoveBuffer buf(out, cap);
  return mg.generatePseudoLegalMoves(pos.getBoard(), pos.getState(), buf);
}
static inline int gen_caps(model::MoveGenerator& mg, model::Position& pos, model::Move* out,
                           int cap) {
  engine::MoveBuffer buf(out, cap);
  return mg.generateCapturesOnly(pos.getBoard(), pos.getState(), buf);
}
static inline int gen_evasions(model::MoveGenerator& mg, model::Position& pos, model::Move* out,
                               int cap) {
  engine::MoveBuffer buf(out, cap);
  return mg.generateEvasions(pos.getBoard(), pos.getState(), buf);
}

// Is there one of our advanced pawns on or next to the capture file?
// (Fast heuristic for clearance sacs that free a passer push next.)
static inline bool advanced_pawn_adjacent_to(const model::Board& b, core::Color us, int toSq) {
  using PT = core::PieceType;
  auto paw = b.getPieces(us, PT::Pawn);
  const int toF = (int)bb::file_of(toSq);
  while (paw) {
    int s = model::bb::ctz64(paw);
    paw &= paw - 1;
    int r = (int)bb::rank_of(s);
    int f = (int)bb::file_of(s);
    // "Advanced" = already deep enough to matter
    bool advanced = (us == core::Color::White) ? (r >= 4) : (r <= 3);
    if (advanced && std::abs(f - toF) <= 1) return true;
  }
  return false;
}

static inline bool is_advanced_passed_pawn_push(const model::Board& b, const model::Move& m,
                                                core::Color us) {
  using PT = core::PieceType;
  if (m.isCapture() || m.promotion() != PT::None) return false;

  auto mover = b.getPiece(m.from());
  if (!mover || mover->type != PT::Pawn) return false;

  const int toSq = m.to();
  const int toFile = bb::file_of(static_cast<core::Square>(toSq));
  const int toRank = bb::rank_of(static_cast<core::Square>(toSq));

  if (us == core::Color::White) {
    if (toRank < 4) return false;  // only consider far-advanced pawns
  } else {
    if (toRank > 3) return false;
  }

  auto oppPawns = b.getPieces(~us, PT::Pawn);
  if (!oppPawns) return true;  // trivially passed

  const int dir = (us == core::Color::White) ? 1 : -1;
  for (int df = -1; df <= 1; ++df) {
    int file = toFile + df;
    if (file < 0 || file > 7) continue;

    for (int r = toRank + dir; r >= 0 && r < 8; r += dir) {
      int sq = (r << 3) | file;
      auto sqBB = model::bb::sq_bb(static_cast<core::Square>(sq));
      if (oppPawns & sqBB) return false;
    }
  }

  return true;
}

static inline void decay_tables(Search& S, int shift /* e.g. 6 => ~1.6% */) {
  for (int f = 0; f < SQ_NB; ++f)
    for (int t = 0; t < SQ_NB; ++t)
      S.history[f][t] = clamp16((int)S.history[f][t] - ((int)S.history[f][t] >> shift));

  for (int p = 0; p < PIECE_NB; ++p)
    for (int t = 0; t < SQ_NB; ++t)
      S.quietHist[p][t] = clamp16((int)S.quietHist[p][t] - ((int)S.quietHist[p][t] >> shift));

  for (int mp = 0; mp < PIECE_NB; ++mp)
    for (int t = 0; t < SQ_NB; ++t)
      for (int cp = 0; cp < PIECE_NB; ++cp)
        S.captureHist[mp][t][cp] =
            clamp16((int)S.captureHist[mp][t][cp] - ((int)S.captureHist[mp][t][cp] >> shift));

  for (int f = 0; f < SQ_NB; ++f)
    for (int t = 0; t < SQ_NB; ++t)
      S.counterHist[f][t] = clamp16((int)S.counterHist[f][t] - ((int)S.counterHist[f][t] >> shift));

  for (int L = 0; L < CH_LAYERS; ++L)
    for (int pp = 0; pp < PIECE_NB; ++pp)
      for (int pt = 0; pt < SQ_NB; ++pt)
        for (int mp = 0; mp < PIECE_NB; ++mp)
          for (int to = 0; to < SQ_NB; ++to) {
            int16_t& h = S.contHist[L][pp][pt][mp][to];
            h = clamp16((int)h - ((int)h >> shift));
          }
}

// 0 = no signal; 1 = attacks high-value piece; 2 = gives check
struct CheckTables {
  model::bb::Bitboard KN_FROM[64];
  model::bb::Bitboard K_FROM[64];
  model::bb::Bitboard PAWN_CHK[2][64];  // [us][kSq]
  model::bb::Bitboard LINE[64][64];
  model::bb::Bitboard BETWEEN[64][64];
  model::bb::Bitboard RAY[64][64];  // ray starting at a towards b
  int DIR[64][64];                  // -1 if not aligned, else 0..7 for N,NE,E,SE,S,SW,W,NW
} CT;

static inline void init_check_tables() {
  using namespace lilia::model::bb;
  for (int s = 0; s < 64; ++s) {
    CT.KN_FROM[s] = knight_attacks_from(static_cast<core::Square>(s));
    CT.K_FROM[s] = king_attacks_from(static_cast<core::Square>(s));
  }
  for (int k = 0; k < 64; ++k) {
    auto kB = sq_bb(static_cast<core::Square>(k));
    CT.PAWN_CHK[(int)core::Color::White][k] = sw(kB) | se(kB);
    CT.PAWN_CHK[(int)core::Color::Black][k] = nw(kB) | ne(kB);
  }

  auto on_line = [&](int a, int b) -> bool {
    int ra = rank_of(static_cast<core::Square>(a));
    int fa = file_of(static_cast<core::Square>(a));
    int rb = rank_of(static_cast<core::Square>(b));
    int fb = file_of(static_cast<core::Square>(b));
    return (ra == rb) || (fa == fb) || (std::abs(ra - rb) == std::abs(fa - fb));
  };

  auto dir_from_to = [&](int a, int b) -> int {
    int ra = rank_of(static_cast<core::Square>(a));
    int fa = file_of(static_cast<core::Square>(a));
    int rb = rank_of(static_cast<core::Square>(b));
    int fb = file_of(static_cast<core::Square>(b));
    int dr = (rb > ra) - (rb < ra);  // -1,0,1
    int df = (fb > fa) - (fb < fa);
    if (dr == 0 && df == 0) return -1;
    if (dr == 0 && df == 1) return 2;    // E
    if (dr == 0 && df == -1) return 6;   // W
    if (dr == 1 && df == 0) return 0;    // N
    if (dr == -1 && df == 0) return 4;   // S
    if (dr == 1 && df == 1) return 1;    // NE
    if (dr == 1 && df == -1) return 7;   // NW
    if (dr == -1 && df == 1) return 3;   // SE
    if (dr == -1 && df == -1) return 5;  // SW
    return -1;
  };

  auto step = [](int dir, model::bb::Bitboard b) {
    using namespace lilia::model::bb;
    switch (dir) {
      case 0:
        return north(b);
      case 1:
        return ne(b);
      case 2:
        return east(b);
      case 3:
        return se(b);
      case 4:
        return south(b);
      case 5:
        return sw(b);
      case 6:
        return west(b);
      case 7:
        return nw(b);
    }
    return (model::bb::Bitboard)0;
  };

  for (int a = 0; a < 64; ++a)
    for (int b = 0; b < 64; ++b) {
      CT.DIR[a][b] = dir_from_to(a, b);
      if (!on_line(a, b)) {
        CT.LINE[a][b] = 0;
        CT.BETWEEN[a][b] = 0;
        CT.RAY[a][b] = 0;
        continue;
      }
      auto A = lilia::model::bb::sq_bb(static_cast<core::Square>(a));
      auto B = lilia::model::bb::sq_bb(static_cast<core::Square>(b));
      int d = CT.DIR[a][b];
      // RAY[a][b]: from a toward b, exclusive
      model::bb::Bitboard ray = 0, r = step(d, A);
      while (r) {
        ray |= r;
        if (r & B) break;
        r = step(d, r);
      }
      CT.RAY[a][b] = ray;
      // BETWEEN[a][b]: squares strictly between
      CT.BETWEEN[a][b] = ray & ~B;
      // LINE[a][b]: whole line through a,b (union of both rays + endpoints)
      // Build opposite ray too:
      int dOpp = (d + 4) & 7;
      model::bb::Bitboard rayOpp = 0, r2 = step(dOpp, A);
      while (r2) {
        rayOpp |= r2;
        r2 = step(dOpp, r2);
      }
      CT.LINE[a][b] = ray | rayOpp | A | B;
    }
}

// --- pre-move "would give check" detector (EP & promotion aware) ---
struct QuietSignals {
  int pawnSignal = 0;
  int pieceSignal = 0;
  bool givesCheck = false;
};

// Does moving to m.to() create an x-ray on the enemy king such that
// removing exactly one blocker (which we already attack) would deliver check?
static inline bool xray_check_after_one_capture(const model::Position& pos, const model::Move& m,
                                                lilia::model::bb::Bitboard occ, int kingSq,
                                                core::Color us, core::PieceType moverAfter) {
  using namespace lilia::model;
  using PT = core::PieceType;

  if (!(moverAfter == PT::Bishop || moverAfter == PT::Rook || moverAfter == PT::Queen))
    return false;

  if (!CT.LINE[kingSq][m.to()]) return false;

  const auto between = CT.BETWEEN[kingSq][m.to()];
  auto blockers = occ & between;
  if (!blockers) return false;

  // Require exactly one blocker on the ray
  if (blockers & (blockers - 1)) return false;
  const int blSq = bb::ctz64(blockers);

  const auto& B = pos.getBoard();

  // Compute if we (us) already attack that blocking square from the current position.
  lilia::model::bb::Bitboard atk = 0;

  // Pawns that could capture the blocker
  atk |= (CT.PAWN_CHK[(int)us][blSq] & B.getPieces(us, PT::Pawn));

  // Knights / King
  atk |= (bb::knight_attacks_from((core::Square)blSq) & B.getPieces(us, PT::Knight));
  atk |= (bb::king_attacks_from((core::Square)blSq) & B.getPieces(us, PT::King));

  // Sliders
  atk |= (magic::sliding_attacks(magic::Slider::Bishop, (core::Square)blSq, occ) &
          (B.getPieces(us, PT::Bishop) | B.getPieces(us, PT::Queen)));
  atk |= (magic::sliding_attacks(magic::Slider::Rook, (core::Square)blSq, occ) &
          (B.getPieces(us, PT::Rook) | B.getPieces(us, PT::Queen)));

  return atk != 0;
}

static inline QuietSignals compute_quiet_signals(const model::Position& pos, const model::Move& m) {
  QuietSignals info{};
  using PT = core::PieceType;

  if (m.isNull()) return info;

  const auto& board = pos.getBoard();
  const auto us = pos.getState().sideToMove;

  const auto enemyKingBB = board.getPieces(~us, PT::King);
  if (!enemyKingBB) return info;

  const int kingSq = lilia::model::bb::ctz64(enemyKingBB);
  const auto fromBB = lilia::model::bb::sq_bb(m.from());
  const auto toBB = lilia::model::bb::sq_bb(m.to());

  auto occ = board.getAllPieces();
  occ = (occ & ~fromBB) | toBB;
  if (m.isEnPassant()) {
    const int epSq = (us == core::Color::White) ? m.to() - 8 : m.to() + 8;
    occ &= ~lilia::model::bb::sq_bb(static_cast<core::Square>(epSq));
  }

  PT moverBefore = PT::None;
  if (auto mover = board.getPiece(m.from())) moverBefore = mover->type;
  PT moverAfter = (m.promotion() != PT::None) ? m.promotion() : moverBefore;

  // Direct check detection using precomputed tables and magics
  if (moverAfter == PT::Pawn) {
    if (CT.PAWN_CHK[(int)us][kingSq] & toBB) info.givesCheck = true;
  } else if (moverAfter == PT::Knight) {
    if (CT.KN_FROM[m.to()] & enemyKingBB) info.givesCheck = true;
  } else if (moverAfter == PT::King) {
    if (CT.K_FROM[m.to()] & enemyKingBB) info.givesCheck = true;
  } else if (moverAfter == PT::Bishop || moverAfter == PT::Rook || moverAfter == PT::Queen) {
    if (CT.LINE[kingSq][m.to()] && (occ & CT.BETWEEN[kingSq][m.to()]) == 0) {
      const bool rookLine = (CT.DIR[kingSq][m.to()] % 2 == 0);
      const bool bishopLine = (CT.DIR[kingSq][m.to()] % 2 != 0);
      if ((moverAfter == PT::Rook && rookLine) || (moverAfter == PT::Bishop && bishopLine) ||
          moverAfter == PT::Queen)
        info.givesCheck = true;
    }
  }

  if (!info.givesCheck && CT.LINE[kingSq][m.from()]) {
    const int dir = CT.DIR[kingSq][m.from()];
    auto ray = CT.RAY[kingSq][m.from()];
    auto blockers = occ & ray;
    if (blockers) {
      int firstSq;
      // Use CTZ when indices grow along the ray: N(0), NE(1), E(2), NW(7)
      if (dir == 0 || dir == 1 || dir == 2 || dir == 7)
        firstSq = lilia::model::bb::ctz64(blockers);
      else
        firstSq = 63 - lilia::model::bb::clz64(blockers);

      if (auto firstPiece = board.getPiece(firstSq); firstPiece && firstPiece->color == us) {
        const bool rookLine = (dir % 2 == 0);
        const bool bishopLine = (dir % 2 != 0);
        if ((rookLine && (firstPiece->type == PT::Rook || firstPiece->type == PT::Queen)) ||
            (bishopLine && (firstPiece->type == PT::Bishop || firstPiece->type == PT::Queen))) {
          info.givesCheck = true;
        }
      }
    }
  }

  // Quiet threat signals (only for non-capturing, non-promoting moves)
  if (!m.isCapture() && m.promotion() == PT::None) {
    if (moverBefore == PT::Pawn) {
      const auto to = lilia::model::bb::sq_bb(m.to());
      const auto pawnAtk = (us == core::Color::White)
                               ? (lilia::model::bb::ne(to) | lilia::model::bb::nw(to))
                               : (lilia::model::bb::se(to) | lilia::model::bb::sw(to));

      if (pawnAtk & enemyKingBB)
        info.pawnSignal = 2;
      else {
        const auto targets = board.getPieces(~us, PT::Queen) | board.getPieces(~us, PT::Rook) |
                             board.getPieces(~us, PT::Bishop) | board.getPieces(~us, PT::Knight);
        if (pawnAtk & targets)
          info.pawnSignal = 1;
        else if (is_advanced_passed_pawn_push(board, m, us))
          info.pawnSignal = 1;
      }
    } else if (moverBefore != PT::None) {
      lilia::model::bb::Bitboard attacks = 0;
      switch (moverBefore) {
        case PT::Knight:
          attacks = lilia::model::bb::knight_attacks_from(m.to());
          break;
        case PT::Bishop:
          attacks = lilia::model::magic::sliding_attacks(lilia::model::magic::Slider::Bishop,
                                                         m.to(), occ);
          break;
        case PT::Rook:
          attacks =
              lilia::model::magic::sliding_attacks(lilia::model::magic::Slider::Rook, m.to(), occ);
          break;
        case PT::Queen:
          attacks =
              lilia::model::magic::sliding_attacks(lilia::model::magic::Slider::Bishop, m.to(),
                                                   occ) |
              lilia::model::magic::sliding_attacks(lilia::model::magic::Slider::Rook, m.to(), occ);
          break;
        case PT::King:
          attacks = lilia::model::bb::king_attacks_from(m.to());
          break;
        default:
          break;
      }

      if (attacks & enemyKingBB) {
        info.pieceSignal = 2;
      } else {
        const auto targets = board.getPieces(~us, PT::Queen) | board.getPieces(~us, PT::Rook) |
                             board.getPieces(~us, PT::Bishop) | board.getPieces(~us, PT::Knight);
        if (attacks & targets) info.pieceSignal = 1;
      }
    }
  }

  // X-ray discovered-check threat: after removing one blocker we already attack,
  // the move would *unveil* a check next ply (e.g. ...Bc5!! threatening ...Qxd4+).
  if (!info.givesCheck &&
      (moverAfter == PT::Bishop || moverAfter == PT::Rook || moverAfter == PT::Queen)) {
    if (xray_check_after_one_capture(pos, m, occ, kingSq, us, moverAfter)) {
      // Not a check yet, but a forcing tactical quiet. Use pieceSignal=1 to
      // gate LMP/futility/LMR and to boost ordering.
      info.pieceSignal = std::max(info.pieceSignal, 1);
    }
  }

  return info;
}

inline void ensure_check_tables_initialized() {
  static std::once_flag init_flag;
  std::call_once(init_flag, [] { init_check_tables(); });
}

}  // namespace

// ---------- Search ----------

Search::Search(model::TT5& tt_, std::shared_ptr<const Evaluator> eval_, const EngineConfig& cfg_)
    : tt(tt_), mg(), cfg(cfg_), eval_(std::move(eval_)) {
  for (auto& kk : killers) {
    kk[0] = model::Move{};
    kk[1] = model::Move{};
  }
  for (auto& h : history) h.fill(0);
  std::fill(&quietHist[0][0], &quietHist[0][0] + PIECE_NB * SQ_NB, 0);
  std::fill(&captureHist[0][0][0], &captureHist[0][0][0] + PIECE_NB * SQ_NB * PIECE_NB, 0);
  std::fill(&counterHist[0][0], &counterHist[0][0] + SQ_NB * SQ_NB, 0);
  std::memset(contHist, 0, sizeof(contHist));
  for (auto& row : counterMove)
    for (auto& m : row) m = model::Move{};
  for (auto& pm : prevMove) pm = model::Move{};

  stopFlag.reset();
  sharedNodes.reset();
  nodeLimit = 0;
  stats = SearchStats{};
  ensure_check_tables_initialized();
}

int Search::signed_eval(model::Position& pos) {
  int v = eval_->evaluate(pos);
  if (pos.getState().sideToMove == core::Color::Black) v = -v;
  return std::clamp(v, -MATE + 1, MATE - 1);
}

namespace {

class ThreadNodeBatch {
 public:
  void reset() { local_ = 0; }

  void bump(const std::shared_ptr<std::atomic<std::uint64_t>>& counter, std::uint64_t limit,
            const std::shared_ptr<std::atomic<bool>>& stopFlag) {
    ++local_;
    if ((local_ & 63u) == 0u) {
      if (stopFlag && stopFlag->load(std::memory_order_relaxed)) {
        throw SearchStoppedException();
      }
    }
    if (local_ >= TICK_STEP) {
      flush_batch(counter, limit, stopFlag);
    }
  }

  std::uint64_t flush(const std::shared_ptr<std::atomic<std::uint64_t>>& counter) {
    if (!counter) {
      local_ = 0;
      return 0;
    }

    const uint32_t pending = local_;
    if (pending == 0u) {
      return counter->load(std::memory_order_relaxed);
    }

    local_ = 0;
    return counter->fetch_add(pending, std::memory_order_relaxed) + pending;
  }

 private:
  void flush_batch(const std::shared_ptr<std::atomic<std::uint64_t>>& counter, std::uint64_t limit,
                   const std::shared_ptr<std::atomic<bool>>& stopFlag) {
    local_ -= TICK_STEP;
    if (counter) {
      std::uint64_t cur = counter->fetch_add(TICK_STEP, std::memory_order_relaxed) + TICK_STEP;
      if (limit && cur >= limit) {
        if (stopFlag) stopFlag->store(true, std::memory_order_relaxed);
        throw SearchStoppedException();
      }
    }
    if (stopFlag && stopFlag->load(std::memory_order_relaxed)) {
      throw SearchStoppedException();
    }
  }

  static constexpr uint32_t TICK_STEP = 8192;
  uint32_t local_ = 0;
};

ThreadNodeBatch& node_batch() {
  static thread_local ThreadNodeBatch instance;
  return instance;
}

inline void reset_node_batch() {
  node_batch().reset();
}

inline std::uint64_t flush_node_batch(const std::shared_ptr<std::atomic<std::uint64_t>>& counter) {
  return node_batch().flush(counter);
}

class NodeFlushGuard {
 public:
  explicit NodeFlushGuard(const std::shared_ptr<std::atomic<std::uint64_t>>& counter)
      : counter_(counter) {
    reset_node_batch();
  }
  ~NodeFlushGuard() {
    // Force-flush this thread's local batch, even if we’re exiting via exception.
    (void)flush_node_batch(counter_);
  }

 private:
  std::shared_ptr<std::atomic<std::uint64_t>> counter_;
};

}  // namespace

inline void bump_node_or_stop(const std::shared_ptr<std::atomic<std::uint64_t>>& counter,
                              std::uint64_t limit,
                              const std::shared_ptr<std::atomic<bool>>& stopFlag) {
  node_batch().bump(counter, limit, stopFlag);
}

// ---------- Quiescence + QTT ----------
int Search::quiescence(model::Position& pos, int alpha, int beta, int ply) {
  bump_node_or_stop(sharedNodes, nodeLimit, stopFlag);

  if (ply >= MAX_PLY - 2) return signed_eval(pos);

  // Draw / 50-move / repetition in qsearch too
  if (pos.checkInsufficientMaterial() || pos.checkMoveRule() || pos.checkRepetition()) return 0;

  const int kply = cap_ply(ply);
  const uint64_t parentKey = pos.hash();
  const int alphaOrig = alpha, betaOrig = beta;

  model::Move bestMoveQ{};

  // QTT probe (depth == 0)
  {
    model::TTEntry5 tte{};
    if (tt.probe_into(pos.hash(), tte)) {
      const int ttVal = decode_tt_score(tte.value, kply);
      if (tte.depth == 0) {
        if (tte.bound == model::Bound::Exact) return ttVal;
        if (tte.bound == model::Bound::Lower && ttVal >= beta) return ttVal;
        if (tte.bound == model::Bound::Upper && ttVal <= alpha) return ttVal;
      }
    }
  }

  const bool inCheck = pos.inCheck();
  if (inCheck) {
    // Evasions only
    int n = gen_evasions(mg, pos, genArr_[kply], engine::MAX_MOVES);
    if (n <= 0) {
      const int ms = mated_in(ply);
      if (!(stopFlag && stopFlag->load()))
        tt.store(parentKey, encode_tt_score(ms, kply), 0, model::Bound::Exact, model::Move{},
                 std::numeric_limits<int16_t>::min());
      return ms;
    }

    constexpr int MAXM = engine::MAX_MOVES;
    int scores[MAXM];
    model::Move ordered[MAXM];

    const model::Move prev = (ply > 0 ? prevMove[cap_ply(ply - 1)] : model::Move{});
    const bool prevOk = !prev.isNull() && prev.from() != prev.to();
    const model::Move cm = prevOk ? counterMove[prev.from()][prev.to()] : model::Move{};

    for (int i = 0; i < n; ++i) {
      const auto& m = genArr_[kply][i];
      int s = 0;
      if (prevOk && m == cm) s += 80'000;
      if (m.isCapture()) s += 100'000 + mvv_lva_fast(pos, m);
      if (m.promotion() != core::PieceType::None) s += 60'000;
      s += history[m.from()][m.to()];
      scores[i] = s;
      ordered[i] = m;
    }
    sort_by_score_desc(scores, ordered, n);

    int best = -INF;
    bool anyLegal = false;

    for (int i = 0; i < n; ++i) {
      if ((i & 63) == 0) check_stop(stopFlag);
      const model::Move m = ordered[i];

      MoveUndoGuard g(pos);
      if (!g.doMove(m)) continue;
      anyLegal = true;

      prevMove[cap_ply(ply)] = m;
      tt.prefetch(pos.hash());
      int score = -quiescence(pos, -beta, -alpha, ply + 1);
      score = std::clamp(score, -MATE + 1, MATE - 1);

      if (score >= beta) {
        if (!(stopFlag && stopFlag->load()))
          tt.store(parentKey, encode_tt_score(beta, kply), 0, model::Bound::Lower, m,
                   std::numeric_limits<int16_t>::min());
        return beta;
      }
      if (score > best) {
        best = score;
        bestMoveQ = m;
      }
      if (score > alpha) alpha = score;
    }

    if (!anyLegal) {
      const int ms = mated_in(ply);
      if (!(stopFlag && stopFlag->load()))
        tt.store(parentKey, encode_tt_score(ms, kply), 0, model::Bound::Exact, model::Move{},
                 std::numeric_limits<int16_t>::min());
      return ms;
    }

    if (!(stopFlag && stopFlag->load())) {
      model::Bound b = model::Bound::Exact;
      if (best <= alphaOrig)
        b = model::Bound::Upper;
      else if (best >= betaOrig)
        b = model::Bound::Lower;
      tt.store(parentKey, encode_tt_score(best, kply), 0, b, bestMoveQ,
               std::numeric_limits<int16_t>::min());
    }
    return best;
  }

  // Not in check: stand pat
  const int stand = signed_eval(pos);
  if (stand >= beta) {
    if (!(stopFlag && stopFlag->load())) {
      constexpr int16_t SE_UNSET = std::numeric_limits<int16_t>::min();
      tt.store(parentKey, encode_tt_score(beta, kply), 0, model::Bound::Lower, model::Move{},
               inCheck ? SE_UNSET : (int16_t)stand);
    }
    return beta;
  }
  if (alpha < stand) alpha = stand;

  // Generate captures (+ non-capture promotions)
  int qn = gen_caps(mg, pos, capArr_[kply], engine::MAX_MOVES);
  if (qn < engine::MAX_MOVES) {
    engine::MoveBuffer buf(capArr_[kply] + qn, engine::MAX_MOVES - qn);
    qn += mg.generateNonCapturePromotions(pos.getBoard(), pos.getState(), buf);
  }

  // Order captures/promos
  constexpr int MAXM = engine::MAX_MOVES;
  int qs[MAXM];
  model::Move qord[MAXM];
  for (int i = 0; i < qn; ++i) {
    const auto& m = capArr_[kply][i];
    qs[i] = mvv_lva_fast(pos, m);
    qord[i] = m;
  }
  sort_by_score_desc(qs, qord, qn);

  constexpr int DELTA_MARGIN = 112;
  int best = stand;

  for (int i = 0; i < qn; ++i) {
    const model::Move m = qord[i];
    if ((i & 63) == 0) check_stop(stopFlag);

    const bool isCap = m.isCapture();
    const bool isPromo = (m.promotion() != core::PieceType::None);
    const int mvv = (isCap || isPromo) ? mvv_lva_fast(pos, m) : 0;

    // --- 3) stricter low-MVV negative-SEE prune ---
    if (isCap && !isPromo && mvv < LOW_MVV_MARGIN) {
      const model::Move pm = (ply > 0 ? prevMove[cap_ply(ply - 1)] : model::Move{});
      const bool isRecap = (!pm.isNull() && pm.to() == m.to());
      const int toFile = bb::file_of(m.to());
      const bool onCenterFile = (toFile == 3 || toFile == 4);  // d or e

      if (!isRecap && !onCenterFile) {
        if (!pos.see(m)) {
          // EXCEPTION: likely a clearance sac for an advanced passer
          const auto us = pos.getState().sideToMove;
          if (!advanced_pawn_adjacent_to(pos.getBoard(), us, m.to())) continue;
        }
      }
    }

    // SEE once if needed
    bool seeOk = true;
    if (isCap && !isPromo) {
      const auto moverOptQ = pos.getBoard().getPiece(m.from());
      const core::PieceType attackerPtQ = moverOptQ ? moverOptQ->type : core::PieceType::Pawn;
      const int attackerValQ = base_value[(int)attackerPtQ];
      int victimValQ = 0;
      if (m.isEnPassant())
        victimValQ = base_value[(int)core::PieceType::Pawn];
      else if (auto capQ = pos.getBoard().getPiece(m.to()))
        victimValQ = base_value[(int)capQ->type];

      if (victimValQ < attackerValQ) {
        seeOk = pos.see(m);
        if (!seeOk && mvv < 400) continue;
      }
    }

    const bool wouldGiveCheck = compute_quiet_signals(pos, m).givesCheck;

    // Delta pruning (skip if giving check) + discovered-check safeguard
    if (!wouldGiveCheck) {
      if (isCap || isPromo) {
        int capVal = 0;
        if (m.isEnPassant())
          capVal = base_value[(int)core::PieceType::Pawn];
        else if (isCap) {
          if (auto cap = pos.getBoard().getPiece(m.to())) capVal = base_value[(int)cap->type];
        }
        int promoGain = 0;
        if (isPromo)
          promoGain =
              std::max(0, base_value[(int)m.promotion()] - base_value[(int)core::PieceType::Pawn]);
        const bool quietPromo = isPromo && !isCap;

        bool shouldPrune = quietPromo ? (stand + promoGain + DELTA_MARGIN <= alpha)
                                      : (stand + capVal + promoGain + DELTA_MARGIN <= alpha);

        if (shouldPrune) {
          // Quick discovered-check safety: if the move actually gives check, don't prune
          MoveUndoGuard cg(pos);
          if (cg.doMove(m) && pos.lastMoveGaveCheck()) {
            cg.rollback();  // fall through to normal search
          } else {
            // illegal or no-check -> keep pruned
            continue;
          }
        }
      }
    }

    MoveUndoGuard g(pos);
    if (!g.doMove(m)) continue;

    prevMove[cap_ply(ply)] = m;
    tt.prefetch(pos.hash());
    int score = -quiescence(pos, -beta, -alpha, ply + 1);
    score = std::clamp(score, -MATE + 1, MATE - 1);

    if (score >= beta) {
      if (!(stopFlag && stopFlag->load()))
        tt.store(parentKey, encode_tt_score(beta, kply), 0, model::Bound::Lower, m, (int16_t)stand);
      return beta;
    }
    if (score > alpha) alpha = score;
    if (score > best) {
      best = score;
      bestMoveQ = m;
    }
  }

  // --- limited quiet checks in qsearch (not just low material) ---
  if (best < beta) {
    // MATERIAL gate: don't add quiet checks in bare endgames (king chases)
    auto countSideNP = [&](core::Color c) {
      using PT = core::PieceType;
      const auto& B = pos.getBoard();
      return model::bb::popcount(B.getPieces(c, PT::Knight) | B.getPieces(c, PT::Bishop) |
                                 B.getPieces(c, PT::Rook) | B.getPieces(c, PT::Queen));
    };
    const int nonP = countSideNP(core::Color::White) + countSideNP(core::Color::Black);
    if (nonP >= 2) {          // skip in K+minor vs K or K vs K situations
      const int LIMIT = 10;   // keep small
      const int MARGIN = 64;  // only try if position isn't already hopeless for side-to-move

      if (stand + MARGIN > alpha) {
        int an = gen_all(mg, pos, genArr_[kply], engine::MAX_MOVES);

        struct QS {
          model::Move m;
          int s;
        };
        QS cand[engine::MAX_MOVES];
        int cn = 0;

        for (int i = 0; i < an; ++i) {
          const model::Move m = genArr_[kply][i];
          if (m.isCapture() || m.promotion() != core::PieceType::None) continue;
          if (!compute_quiet_signals(pos, m).givesCheck) continue;

          int sc = history[m.from()][m.to()];
          if (m == killers[kply][0] || m == killers[kply][1]) sc += 6000;
          cand[cn++] = {m, sc};
        }

        if (cn > 1) std::sort(cand, cand + cn, [](const QS& a, const QS& b) { return a.s > b.s; });

        int tried = 0;
        for (int i = 0; i < cn && tried < LIMIT; ++i) {
          const model::Move m = cand[i].m;

          MoveUndoGuard g(pos);
          if (!g.doMove(m)) continue;

          prevMove[cap_ply(ply)] = m;
          int score = -quiescence(pos, -beta, -alpha, ply + 1);
          score = std::clamp(score, -MATE + 1, MATE - 1);
          ++tried;

          if (score >= beta) {
            if (!(stopFlag && stopFlag->load()))
              tt.store(parentKey, encode_tt_score(beta, kply), 0, model::Bound::Lower, m,
                       (int16_t)stand);
            return beta;
          }
          if (score > best) best = score;
          if (score > alpha) alpha = score;
        }
      }
    }
  }

  if (!(stopFlag && stopFlag->load())) {
    model::Bound b = model::Bound::Exact;
    if (best <= alphaOrig)
      b = model::Bound::Upper;
    else if (best >= betaOrig)
      b = model::Bound::Lower;
    tt.store(parentKey, encode_tt_score(best, kply), 0, b, bestMoveQ, (int16_t)stand);
  }
  return best;
}

// ---------- Negamax ----------

int Search::negamax(model::Position& pos, int depth, int alpha, int beta, int ply,
                    model::Move& refBest, int parentStaticEval, const model::Move* excludedMove) {
  bump_node_or_stop(sharedNodes, nodeLimit, stopFlag);

  if (ply >= MAX_PLY - 2) return signed_eval(pos);
  if (pos.checkInsufficientMaterial() || pos.checkMoveRule() || pos.checkRepetition()) return 0;
  if (depth <= 0) return quiescence(pos, alpha, beta, ply);

  // Mate distance pruning
  alpha = std::max(alpha, mated_in(ply));
  beta = std::min(beta, mate_in(ply));
  if (alpha >= beta) return alpha;

  const int origAlpha = alpha;
  const int origBeta = beta;
  const bool isPV = (beta - alpha > 1);

  const bool inCheck = pos.inCheck();

  int best = -INF;
  model::Move bestLocal{};

  // ----- TT probe (also harvest cached staticEval) -----
  model::Move ttMove{};
  bool haveTT = false;
  int ttVal = 0;
  model::Bound ttBound = model::Bound::Upper;  // for SE trust
  int ttStoredDepth = -1;
  int16_t ttSE = std::numeric_limits<int16_t>::min();

  if (model::TTEntry5 tte{}; tt.probe_into(pos.hash(), tte)) {
    haveTT = true;
    ttMove = tte.best;
    ttVal = decode_tt_score(tte.value, cap_ply(ply));
    ttBound = tte.bound;
    ttStoredDepth = (int)tte.depth;
    ttSE = tte.staticEval;

    if (tte.depth >= depth) {
      if (tte.bound == model::Bound::Exact) return std::clamp(ttVal, -MATE + 1, MATE - 1);
      if (tte.bound == model::Bound::Lower) alpha = std::max(alpha, ttVal);
      if (tte.bound == model::Bound::Upper) beta = std::min(beta, ttVal);
      if (alpha >= beta) return std::clamp(ttVal, -MATE + 1, MATE - 1);
    }
  }

  // Compute staticEval (prefer TT's cached one when not in check)
  constexpr int16_t SE_UNSET = std::numeric_limits<int16_t>::min();
  const int staticEval = inCheck ? 0 : (ttSE != SE_UNSET ? (int)ttSE : signed_eval(pos));

  // ---- "improving" ----
  const bool improving =
      !inCheck && (parentStaticEval == INF || staticEval >= parentStaticEval - 16);

  // Count non-pawn material once (for SNMP & Nullmove)
  const auto& b = pos.getBoard();

  bool queensOn = (b.getPieces(core::Color::White, core::PieceType::Queen) |
                   b.getPieces(core::Color::Black, core::PieceType::Queen)) != 0;
  bool nearWindow = (beta - alpha) <= 16;
  bool highTension = (!inCheck && depth <= 5 && nearWindow && staticEval + 64 >= alpha);
  bool tacticalNode = queensOn && highTension;

  int nonP = 0;
  const bool needNonP = (!inCheck && !isPV && (depth <= 3 || (cfg.useNullMove && depth >= 3)));
  if (needNonP) {
    auto countSide = [&](core::Color c) {
      using PT = core::PieceType;
      return model::bb::popcount(b.getPieces(c, PT::Knight) | b.getPieces(c, PT::Bishop) |
                                 b.getPieces(c, PT::Rook) | b.getPieces(c, PT::Queen));
    };
    nonP = countSide(core::Color::White) + countSide(core::Color::Black);
  }

  // --- Stronger Razoring (D1 + D2), non-PV, not in check ---
  if (!inCheck && !isPV && depth <= 2) {
    // tuned-safe margins (stronger than previous)
    const int RAZOR_D1 = 256 + (improving ? 64 : 0);
    const int RAZOR_D2 = 480 + (improving ? 64 : 0);

    if (depth == 1) {
      if (staticEval + RAZOR_D1 <= alpha) {
        int q = quiescence(pos, alpha - 1, alpha, ply);
        if (q <= alpha) return q;
      }
    } else {  // depth == 2
      if (staticEval + RAZOR_D2 <= alpha) {
        int q = quiescence(pos, alpha - 1, alpha, ply);
        if (q <= alpha) return q;
      }
    }
  }

  // Reverse futility D1 (keep)
  if (!inCheck && !isPV && depth == 1) {
    int margin = RFP_MARGIN_BASE + (improving ? 40 : 0);
    if (staticEval - margin >= beta) return staticEval;
  }

  // --- Static Null-Move Pruning (non-PV, not in check, shallow) ---
  if (!tacticalNode && !inCheck && !isPV && depth <= 3) {
    const int d = std::max(1, std::min(3, depth));
    const int margin = SNMP_MARGINS[d];
    if (staticEval - margin >= beta) {
      // Cheap cutoff – this saves a lot of leaf work with basically no tactical risk.
      if (!(stopFlag && stopFlag->load())) {
        constexpr int16_t SE_UNSET = std::numeric_limits<int16_t>::min();
        tt.store(pos.hash(), encode_tt_score(staticEval, cap_ply(ply)),
                 /*depth*/ 0, model::Bound::Lower, /*best*/ model::Move{},
                 inCheck ? SE_UNSET : (int16_t)staticEval);
      }
      return staticEval;
    }
  }

  // Internal Iterative Deepening (IID) for better ordering
  // Trigger when no good TT move at this depth, not in check, depth is big enough.
  if (!inCheck && depth >= 5 && (!haveTT || ttStoredDepth < depth - 2)) {
    // shallower probe; a touch deeper in PV
    int iidDepth = depth - 2 - (isPV ? 0 : 1);
    if (iidDepth < 1) iidDepth = 1;

    model::Move iidBest{};
    // narrow-ish window in non-PV to cut; full in PV
    int iidAlpha = isPV ? alpha : std::max(alpha, staticEval - 32);
    int iidBeta = isPV ? beta : (iidAlpha + 1);

    (void)negamax(pos, iidDepth, iidAlpha, iidBeta, ply, iidBest, staticEval);
    // re-probe TT to harvest best for ordering
    if (model::TTEntry5 tte2{}; tt.probe_into(pos.hash(), tte2)) {
      ttMove = tte2.best;
      haveTT = true;
      ttVal = decode_tt_score(tte2.value, cap_ply(ply));
      ttBound = tte2.bound;
      ttStoredDepth = (int)tte2.depth;
    }
  }

  // --- light "quick quiet check" probe to avoid suicidal null-move
  bool hasQuickQuietCheck = false;
  if (!inCheck && !isPV && depth <= 5) {
    int probeCap = std::min(engine::MAX_MOVES, 16);
    int probeN = gen_all(mg, pos, genArr_[cap_ply(ply)], probeCap);
    for (int i = 0; i < probeN && i < probeCap; ++i) {
      const auto& mm = genArr_[cap_ply(ply)][i];
      if (mm.isCapture() || mm.promotion() != core::PieceType::None) continue;

      const auto signals = compute_quiet_signals(pos, mm);
      if (!signals.givesCheck) continue;

      // require either a “threat” (attacks a piece) or decent history
      int ps = signals.pieceSignal;
      int h = history[mm.from()][mm.to()];
      if (ps >= 1 || h > 0) {  // only then disable null move
        hasQuickQuietCheck = true;
        break;
      }
    }
  }

  // Null move pruning (adaptive)
  const bool sparse = (nonP <= 3);
  const bool prevWasCapture = (ply > 0 && prevMove[cap_ply(ply - 1)].isCapture());

  if (cfg.useNullMove && depth >= 3 && !inCheck && !isPV && !sparse && !prevWasCapture &&
      !tacticalNode && !hasQuickQuietCheck) {
    const int evalGap = staticEval - beta;
    int rBase = 2 + (depth >= 8 ? 1 : 0);
    if (evalGap > 200) rBase++;
    if (evalGap > 500) rBase++;
    if (!improving) rBase++;
    if (nonP >= 8) rBase++;

    int R = std::min(rBase, depth - 2);

    int margin = 50 + 20 * depth + (improving ? 40 : 0);
    if (staticEval >= beta + margin) {
      NullUndoGuard ng(pos);
      if (ng.doNull()) {
        model::Move tmpNM{};
        int nullScore = -negamax(pos, depth - 1 - R, -beta, -beta + 1, ply + 1, tmpNM, -staticEval);
        ng.rollback();
        if (nullScore >= beta) {
          const bool needVerify = (depth >= 8 && R >= 3 && evalGap < 800);
          if (needVerify) {
            model::Move tmpVerify{};
            int verify =
                -negamax(pos, depth - 1, -beta, -beta + 1, ply + 1, tmpVerify, -staticEval);
            if (verify >= beta) return beta;
          } else {
            return beta;
          }
        }
      }
    }
  }

  // Move generation
  const int kply = cap_ply(ply);
  int n = 0;
  if (inCheck) {
    n = gen_evasions(mg, pos, genArr_[kply], engine::MAX_MOVES);
    if (n <= 0) return mated_in(ply);
  } else {
    n = gen_all(mg, pos, genArr_[kply], engine::MAX_MOVES);
    if (n <= 0) return 0;
  }

  // prev for CounterMove
  const model::Move prev = (ply > 0 ? prevMove[cap_ply(ply - 1)] : model::Move{});
  const bool prevOk = !prev.isNull() && prev.from() != prev.to();
  const model::Move cm = prevOk ? counterMove[prev.from()][prev.to()] : model::Move{};

  // --------- Staged move ordering ---------
  constexpr int MAX_MOVES = engine::MAX_MOVES;
  int scores[MAX_MOVES];
  model::Move ordered[MAX_MOVES];

  // Large bucket to enforce stages (descending sort => higher bucket first)
  constexpr int BUCKET = 10'000'000;

  // stage weights (higher = earlier)
  enum : int {
    ST_TT = 5,
    ST_GOOD_CAP = 4,
    ST_KILLER_CM_QP = 3,  // killers, countermove, quiet promotions
    ST_QUIET = 2,
    ST_BAD_CAP = 1
  };

  constexpr int TT_BONUS = 2'400'000;
  constexpr int CAP_BASE_GOOD = 180'000;
  constexpr int CAP_BASE_BAD = 20'000;  // tiny base for losing caps (still searched)
  constexpr int PROMO_BASE = 160'000;
  constexpr int KILLER_BASE = 120'000;
  constexpr int CM_BASE = 140'000;

  const auto& board = pos.getBoard();

  for (int i = 0; i < n; ++i) {
    const auto& m = genArr_[kply][i];

    // Skip duplicates of TT later by just giving TT the best bucket;
    // search loop will naturally attempt TT first anyway.
    int stage = ST_QUIET;  // default
    int base = 0;

    const bool isCap = m.isCapture();
    const bool isPromo = (m.promotion() != core::PieceType::None);
    const bool isQuiet = !isCap && !isPromo;

    // recapture detection (useful to treat as "good cap")
    const model::Move pm = (ply > 0 ? prevMove[cap_ply(ply - 1)] : model::Move{});
    const bool isRecap = (!pm.isNull() && pm.to() == m.to());

    // classify captures now: good vs bad (seeGood computed later per-move too,
    // but we also need a quick pre-pass; we can recompute here cheaply)
    bool seeGoodLocal = true;
    core::PieceType capPt = core::PieceType::Pawn;
    if (m.isEnPassant()) {
      capPt = core::PieceType::Pawn;
    } else if (isCap) {
      if (auto cap = board.getPiece(m.to())) capPt = cap->type;
    }
    if (isCap && m.promotion() == core::PieceType::None) {
      seeGoodLocal = pos.see(m);
    }

    // TT move first
    if (haveTT && m == ttMove) {
      stage = ST_TT;
      base = TT_BONUS;
    }
    // Captures next (good first)
    else if (isCap) {
      const int mvv = mvv_lva_fast(pos, m);
      // treat recaptures or big victims as "good", even if SEE<0
      const bool bigVictim = (capPt == core::PieceType::Rook || capPt == core::PieceType::Queen);
      const bool good = seeGoodLocal || isRecap || bigVictim || isPromo;

      if (good) {
        stage = ST_GOOD_CAP;
        // lightweight capture history used here is optional; keep MVV as core
        base = CAP_BASE_GOOD + mvv;
      } else {
        stage = ST_BAD_CAP;
        base = CAP_BASE_BAD + mvv;  // still sorted among themselves
      }
    }
    // Quiet promotions are high-priority quiets (like Stockfish)
    else if (isPromo) {
      stage = ST_KILLER_CM_QP;
      base = PROMO_BASE;
    }
    // Killers & countermove
    else if (m == killers[kply][0] || m == killers[kply][1]) {
      stage = ST_KILLER_CM_QP;
      base = KILLER_BASE;
    } else {
      const model::Move cm = (prevOk ? counterMove[prev.from()][prev.to()] : model::Move{});
      if (prevOk && m == cm) {
        stage = ST_KILLER_CM_QP;
        base = CM_BASE + (counterHist[prev.from()][prev.to()] >> 1);
      } else {
        // regular quiet: history + quietHist + a tiny malus for heavy non-tactical shuffles
        auto moverOpt = board.getPiece(m.from());
        const core::PieceType moverPt = moverOpt ? moverOpt->type : core::PieceType::Pawn;
        base = history[m.from()][m.to()] + (quietHist[pidx(moverPt)][m.to()] >> 1);

        if (moverPt == core::PieceType::Queen || moverPt == core::PieceType::Rook) {
          base -= 6000;  // small malus; your Search will still favor tactical quiets
        }
        stage = ST_QUIET;
      }
    }

    {
      const auto sig = compute_quiet_signals(pos, m);
      if (sig.givesCheck) {
        if (stage < ST_KILLER_CM_QP) stage = ST_KILLER_CM_QP;
        base += 90'000;
      } else if (sig.pawnSignal > 0 || sig.pieceSignal > 0) {
        if (stage < ST_KILLER_CM_QP) stage = ST_KILLER_CM_QP;
        base += 40'000;
      }
    }

    scores[i] = stage * BUCKET + base;
    ordered[i] = m;
  }

  // Staged order enforced by bucket; within each stage, your existing heuristics sort
  sort_by_score_desc(scores, ordered, n);

  const bool allowFutility = !inCheck && !isPV;
  int moveCount = 0;
  bool searchedAny = false;

  for (int idx = 0; idx < n; ++idx) {
    if ((idx & 63) == 0) check_stop(stopFlag);

    const model::Move m = ordered[idx];
    if (excludedMove && m == *excludedMove) {
      continue;  // don’t skew LMR/LMP with a non-searched move
    }

    const bool isQuiet = !m.isCapture() && (m.promotion() == core::PieceType::None);
    const auto us = pos.getState().sideToMove;

    bool doThreatSignals = cfg.useThreatSignals && depth <= cfg.threatSignalsDepthMax &&
                           moveCount < cfg.threatSignalsQuietCap;

    if (isQuiet && doThreatSignals) {
      if (history[m.from()][m.to()] < cfg.threatSignalsHistMin) doThreatSignals = false;
    }

    bool passed_push = false;
    if (isQuiet) {
      if (auto mover = board.getPiece(m.from()); mover && mover->type == core::PieceType::Pawn) {
        passed_push = is_advanced_passed_pawn_push(board, m, us);
      }
    }

    // --- Always detect true checks for quiet moves (even if threat signals are gated) ---
    int pawn_sig = 0, piece_sig = 0;
    bool wouldCheck = false;
    if (isQuiet) {
      const auto signals = compute_quiet_signals(pos, m);
      piece_sig = signals.pieceSignal;  // detects direct checks (==2)

      const bool allowPawnThreats = doThreatSignals && piece_sig < 2;

      if (piece_sig < 2) {
        if (allowPawnThreats) pawn_sig = signals.pawnSignal;
        if (is_advanced_passed_pawn_push(board, m, us)) passed_push = true;
      }

      wouldCheck = signals.givesCheck;
      if (wouldCheck) {
        piece_sig = std::max(piece_sig, 2);
        if (auto mover = board.getPiece(m.from()); mover && mover->type == core::PieceType::Pawn) {
          pawn_sig = std::max(pawn_sig, 2);
        }
      }
    }
    if (passed_push) pawn_sig = std::max(pawn_sig, 1);
    const int qp_sig = pawn_sig;
    const int qpc_sig = piece_sig;
    const bool tacticalQuiet = (qp_sig > 0) || (qpc_sig > 0);

    // pre info
    auto moverOpt = board.getPiece(m.from());
    const core::PieceType moverPt = moverOpt ? moverOpt->type : core::PieceType::Pawn;
    core::PieceType capPt = core::PieceType::Pawn;

    const bool isQuietHeavy =
        isQuiet && (moverPt == core::PieceType::Queen || moverPt == core::PieceType::Rook);

    if (m.isEnPassant())
      capPt = core::PieceType::Pawn;
    else if (m.isCapture()) {
      if (auto cap = board.getPiece(m.to())) capPt = cap->type;
    }
    const int capValPre = m.isCapture() ? (m.isEnPassant() ? base_value[(int)core::PieceType::Pawn]
                                                           : base_value[(int)capPt])
                                        : 0;

    // LMP (contHist-aware) --- don't LMP quiet checks
    if (!tacticalNode && !inCheck && !isPV && isQuiet && depth <= 3 && !tacticalQuiet &&
        !isQuietHeavy && !wouldCheck) {
      int hist = history[m.from()][m.to()] + (quietHist[pidx(moverPt)][m.to()] >> 1);

      int ch = 0;
      if (ply >= 1) {
        const auto pm1 = prevMove[cap_ply(ply - 1)];
        if (pm1.from() >= 0 && pm1.to() >= 0 && pm1.to() < 64) {
          if (auto po1 = board.getPiece(pm1.to()))
            ch = contHist[0][pidx(po1->type)][pm1.to()][pidx(moverPt)][m.to()];
        }
      }

      int limit = LMP_LIMIT[depth];
      if (hist < -8000) limit -= 1;
      if (ch < -8000) limit -= 1;
      if (limit < 1) limit = 1;

      int futMarg = FUT_MARGIN[depth] + (improving ? 32 : 0);
      if (staticEval + futMarg <= alpha + 32 && moveCount >= limit) {
        ++moveCount;
        continue;
      }
    }

    // Extra move-count-based pruning for very late quiets
    if (!tacticalNode && !inCheck && !isPV && isQuiet && depth <= 3 && !tacticalQuiet) {
      if (moveCount >= 16 + 4 * depth) {  // after many tries, bail
        ++moveCount;
        continue;
      }
    }

    // Extended futility (depth<=3, quiets) --- don't prune quiet checks
    if (allowFutility && isQuiet && depth <= 3 && !tacticalQuiet && !isQuietHeavy &&
        !tacticalNode && !wouldCheck) {
      int fut = FUT_MARGIN[depth] + (history[m.from()][m.to()] < -8000 ? 32 : 0);
      if (improving) fut += 48;
      if (staticEval + fut <= alpha) {
        ++moveCount;
        continue;
      }
    }

    // History pruning — gate on improving --- don't prune quiet checks
    if (!tacticalNode && !inCheck && !isPV && isQuiet && depth <= 2 && !tacticalQuiet &&
        !isQuietHeavy && !improving && !wouldCheck) {
      int histScore = history[m.from()][m.to()] + (quietHist[pidx(moverPt)][m.to()] >> 1);
      if (histScore < -11000 && m != killers[kply][0] && m != killers[kply][1] &&
          (!prevOk || m != cm)) {
        ++moveCount;
        continue;
      }
    }

    // Futility (D1) — gate on improving --- don't prune quiet checks
    if (!inCheck && !isPV && isQuiet && depth == 1 && !tacticalQuiet && !isQuietHeavy &&
        !improving && !wouldCheck) {
      if (staticEval + 110 <= alpha) {
        ++moveCount;
        continue;
      }
    }
    // SEE once if needed (do not prune losing captures; just mark them "bad")
    bool seeGood = true;
    if (m.isCapture() && m.promotion() == core::PieceType::None) {
      auto moverOptQ = board.getPiece(m.from());
      const core::PieceType attackerPtQ = moverOptQ ? moverOptQ->type : core::PieceType::Pawn;

      (void)attackerPtQ;  // attacker value no longer used for pruning here
      // classify capture quality once for later (probcut / reductions / staging)
      seeGood = pos.see(m);
    }

    const int mvvBefore =
        (m.isCapture() || m.promotion() != core::PieceType::None) ? mvv_lva_fast(pos, m) : 0;
    int newDepth = depth - 1;

    // ----- Singular Extension -----
    int seExt = 0;
    if (cfg.useSingularExt && haveTT && m == ttMove && !inCheck && depth >= 6) {
      // Only trust a LOWER bound close to current depth; this mirrors SF and avoids bogus SE.
      const bool ttGood =
          (ttBound == model::Bound::Lower) && (ttStoredDepth >= depth - 1) && !is_mate_score(ttVal);
      if (ttGood) {
        const int R = (depth >= 8 ? 3 : 2);
        const int margin = 64 + 2 * depth;
        const int singBeta = ttVal - margin;

        if (singBeta > -MATE + 64) {
          model::Move dummy{};
          const int sDepth = std::max(1, depth - 1 - R);
          int s = negamax(pos, sDepth, singBeta - 1, singBeta, ply, dummy, staticEval, &m);
          if (s < singBeta) seExt = 1;
        }
      }
    }
    newDepth += seExt;

    // --- Snapshot parent CH anchors BEFORE making the move ---
    int pm1_to = -1, pm2_to = -1, pm3_to = -1;
    int pm1_pt = -1, pm2_pt = -1, pm3_pt = -1;
    if (ply >= 1) {
      const model::Move pm1 = prevMove[cap_ply(ply - 1)];
      if (pm1.from() >= 0 && pm1.to() >= 0 && pm1.from() < 64 && pm1.to() < 64) {
        if (auto p = board.getPiece(pm1.to())) {
          pm1_to = pm1.to();
          pm1_pt = pidx(p->type);
        }
      }
    }
    if (ply >= 2) {
      const model::Move pm2 = prevMove[cap_ply(ply - 2)];
      if (pm2.from() >= 0 && pm2.to() >= 0 && pm2.from() < 64 && pm2.to() < 64) {
        if (auto p = board.getPiece(pm2.to())) {
          pm2_to = pm2.to();
          pm2_pt = pidx(p->type);
        }
      }
    }
    if (ply >= 3) {
      const model::Move pm3 = prevMove[cap_ply(ply - 3)];
      if (pm3.from() >= 0 && pm3.to() >= 0 && pm3.from() < 64 && pm3.to() < 64) {
        if (auto p = board.getPiece(pm3.to())) {
          pm3_to = pm3.to();
          pm3_pt = pidx(p->type);
        }
      }
    }

    MoveUndoGuard g(pos);
    if (!g.doMove(m)) {
      ++moveCount;
      continue;
    }

    prevMove[cap_ply(ply)] = m;
    tt.prefetch(pos.hash());

    int value;
    model::Move childBest{};

    // ProbCut (capture-only)
    if (!isPV && !inCheck && newDepth >= 4 && m.isCapture() && seeGood && mvvBefore >= 500) {
      constexpr int PROBCUT_MARGIN = 224;
      if (staticEval + capValPre + PROBCUT_MARGIN >= beta) {
        const int red = 3;
        const int pcDepth = std::max(1, newDepth - red);
        const int probe =
            -negamax(pos, pcDepth, -beta, -(beta - 1), ply + 1, childBest, -staticEval);
        if (probe >= beta) return beta;
      }
    }

    // Check extension (light)
    const bool givesCheck = pos.lastMoveGaveCheck();
    if (givesCheck) {
      if (!isQuiet) {
        // CAPTURING CHECK: extend only if SEE is >= 0, or it's a recapture, or a big victim
        bool allowCaptureExt = false;
        const int attackerVal = base_value[(int)moverPt];
        const int victimVal = capValPre;

        const model::Move pm = (ply > 0 ? prevMove[cap_ply(ply - 1)] : model::Move{});
        const bool isRecap = (!pm.isNull() && pm.to() == m.to());

        if (pos.see(m))
          allowCaptureExt = true;
        else if (isRecap)
          allowCaptureExt = true;
        else if (victimVal >= base_value[(int)core::PieceType::Rook])
          allowCaptureExt = true;  // RxQ/R

        if (allowCaptureExt) newDepth += 1;
      } else {
        // QUIET CHECK: only very lightly extend and only in PV nodes (and not queen shuffles)
        bool okQuietExt = isPV && depth <= 2 && (history[m.from()][m.to()] > 0) &&
                          (moverPt != core::PieceType::Queen);
        if (okQuietExt) newDepth += 1;
      }
    }
    if (passed_push && isQuiet) newDepth += 1;

    // Bad capture reduction
    int reduction = 0;
    if (!seeGood && m.isCapture() && newDepth >= 2) reduction = std::min(1, newDepth - 1);

    // PVS / LMR
    if (moveCount == 0) {
      value = -negamax(pos, newDepth, -beta, -alpha, ply + 1, childBest, -staticEval);
    } else {
      if (cfg.useLMR && isQuiet && !tacticalQuiet && !inCheck && !givesCheck && newDepth >= 2 &&
          moveCount >= 3) {
        const int ld = ilog2_u32((unsigned)depth);
        const int lm = ilog2_u32((unsigned)(moveCount + 1));
        int r = (ld * (lm + 1)) / 2;
        if (tacticalNode) r = std::max(0, r - 1);
        if (isQuietHeavy) r = std::max(0, r - 1);
        const int h = history[m.from()][m.to()] + (quietHist[pidx(moverPt)][m.to()] >> 1);
        int ch = 0;
        if (ply >= 1) {
          const auto pm1 = prevMove[cap_ply(ply - 1)];
          if (pm1.from() >= 0 && pm1.to() >= 0 && pm1.to() < 64) {
            if (auto po1 = board.getPiece(pm1.to()))
              ch = contHist[0][pidx(po1->type)][pm1.to()][pidx(moverPt)][m.to()];
          }
        }

        if (h > 8000) r -= 1;
        if (ch > 8000) r -= 1;
        if (m == killers[kply][0] || m == killers[kply][1]) r -= 1;
        if (haveTT && m == ttMove) r -= 1;
        if (ply <= 2) r -= 1;
        if (beta - alpha <= 8) r -= 1;
        if (!improving) r += 1;
        if (qpc_sig == 2 /* quiet check */) r = std::max(0, r - 1);
        if (qpc_sig == 1 /* tactical quiet via signals */) r = std::max(0, r - 1);

        // extra safety: avoid reducing the first 3 quiets at shallow depth
        if (newDepth <= 2 && moveCount < 3) r = 0;

        if (r < 0) r = 0;
        int rCap = (newDepth >= 5 ? 3 : 2);
        if (r > rCap) r = rCap;
        reduction = std::min(r, newDepth - 1);
      }

      value =
          -negamax(pos, newDepth - reduction, -alpha - 1, -alpha, ply + 1, childBest, -staticEval);
      if (value > alpha && value < beta) {
        value = -negamax(pos, newDepth, -beta, -alpha, ply + 1, childBest, -staticEval);
      }
    }

    value = std::clamp(value, -MATE + 1, MATE - 1);
    searchedAny = true;

    // History updates
    if (isQuiet && value <= origAlpha) {
      const int M = hist_bonus(depth) / 2;

      hist_update(history[m.from()][m.to()], -M);
      hist_update(quietHist[pidx(moverPt)][m.to()], -M);

      if (pm1_to >= 0 && pm1_pt >= 0)
        hist_update(contHist[0][pm1_pt][pm1_to][pidx(moverPt)][m.to()], -M);
      if (pm2_to >= 0 && pm2_pt >= 0)
        hist_update(contHist[1][pm2_pt][pm2_to][pidx(moverPt)][m.to()], -(M >> 1));
      if (pm3_to >= 0 && pm3_pt >= 0)
        hist_update(contHist[2][pm3_pt][pm3_to][pidx(moverPt)][m.to()], -(M >> 2));
    }

    if (value > best) {
      best = value;
      bestLocal = m;
    }
    if (value > alpha) alpha = value;

    if (alpha >= beta) {
      if (isQuiet) {
        killers[kply][1] = killers[kply][0];
        killers[kply][0] = m;

        const int B = hist_bonus(depth);

        hist_update(history[m.from()][m.to()], +B);
        hist_update(quietHist[pidx(moverPt)][m.to()], +B);

        if (pm1_to >= 0 && pm1_pt >= 0)
          hist_update(contHist[0][pm1_pt][pm1_to][pidx(moverPt)][m.to()], +B);
        if (pm2_to >= 0 && pm2_pt >= 0)
          hist_update(contHist[1][pm2_pt][pm2_to][pidx(moverPt)][m.to()], +(B >> 1));
        if (pm3_to >= 0 && pm3_pt >= 0)
          hist_update(contHist[2][pm3_pt][pm3_to][pidx(moverPt)][m.to()], +(B >> 2));

        if (prevOk) {
          counterMove[prev.from()][prev.to()] = m;
          hist_update(counterHist[prev.from()][prev.to()], +B);
        }
      } else {
        hist_update(captureHist[pidx(moverPt)][m.to()][pidx(capPt)], +hist_bonus(depth));
      }
      break;
    }
    ++moveCount;
  }

  // --- 5) Early ProbCut pass (cheap capture-only skim) ---
  if (!isPV && !inCheck && depth >= 6) {
    constexpr int PC_MARGIN = 192;        // a bit lighter than in-node 224
    const int MAX_SCAN = std::min(n, 6);  // don't scan too many

    for (int idx = 0; idx < MAX_SCAN; ++idx) {
      const model::Move m = ordered[idx];
      if (!m.isCapture()) continue;
      if (mvv_lva_fast(pos, m) < 500) continue;  // need a meaningful tactical swing

      MoveUndoGuard pcg(pos);
      if (!pcg.doMove(m)) continue;

      const int childSE = signed_eval(pos);  // opponent POV
      if (-childSE + PC_MARGIN >= beta) {    // flip the sign
        model::Move tmp{};
        const int probe = -negamax(pos, depth - 3, -beta, -(beta - 1), ply + 1, tmp, INF);
        pcg.rollback();
        if (probe >= beta) return beta;
      } else {
        pcg.rollback();
      }
    }
  }

  // If this was a singular-extension verification search that excluded a move and
  // we ended up searching nothing (e.g., the excluded TT move is the only legal move),
  // return a clear fail-low instead of 0/stalemate. This lets SE trigger correctly.
  if (excludedMove && !searchedAny) {
    return -INF + 1;
  }

  // safety: never leave node without searching at least one move (non-check)
  if (!searchedAny) {
    for (int idx = 0; idx < n; ++idx) {
      const model::Move m = ordered[idx];
      if (excludedMove && m == *excludedMove) continue;
      MoveUndoGuard g(pos);
      if (!g.doMove(m)) continue;

      model::Move childBest{};
      int value = -negamax(pos, depth - 1, -beta, -alpha, ply + 1, childBest, -staticEval);
      value = std::clamp(value, -MATE + 1, MATE - 1);

      best = value;
      bestLocal = m;
      if (value > alpha) alpha = value;
      break;
    }
  }

  if (best == -INF) {
    if (inCheck) return mated_in(ply);
    return 0;
  }

  if (!(stopFlag && stopFlag->load())) {
    model::Bound bnd;
    if (best <= origAlpha)
      bnd = model::Bound::Upper;
    else if (best >= origBeta)
      bnd = model::Bound::Lower;
    else
      bnd = model::Bound::Exact;

    int16_t storeSE = inCheck ? SE_UNSET : (int16_t)staticEval;

    tt.store(pos.hash(), encode_tt_score(best, cap_ply(ply)), static_cast<int16_t>(depth), bnd,
             bestLocal, storeSE);
  }

  refBest = bestLocal;
  return best;
}

// ---------- PV aus TT ----------

std::vector<model::Move> Search::build_pv_from_tt(model::Position pos, int max_len) {
  std::vector<model::Move> pv;
  std::unordered_set<uint64_t> seen;
  pv.reserve(max_len);
  seen.reserve(max_len);

  for (int i = 0; i < max_len; ++i) {
    model::TTEntry5 tte{};
    if (!tt.probe_into(pos.hash(), tte)) break;

    model::Move m = tte.best;
    if (m.from() == m.to()) break;

    if (!pos.doMove(m)) break;
    pv.push_back(m);

    uint64_t h = pos.hash();
    if (!seen.insert(h).second) break;  // loop guard
  }
  return pv;
}
int Search::search_root_single(model::Position& pos, int maxDepth,
                               std::shared_ptr<std::atomic<bool>> stop, std::uint64_t maxNodes) {
  NodeFlushGuard node_guard(sharedNodes);  // <- ensures the final <TICK_STEP> gets counted
  // --- init shared stop/nodes ---
  this->stopFlag = stop;
  if (!this->sharedNodes) this->sharedNodes = std::make_shared<std::atomic<std::uint64_t>>(0);
  if (maxNodes) this->nodeLimit = maxNodes;

  reset_node_batch();

  stats = SearchStats{};
  auto t0 = steady_clock::now();
  auto update_time_stats = [&] {
    auto now = steady_clock::now();
    std::uint64_t ms =
        (std::uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(now - t0).count();
    stats.elapsedMs = ms;
    stats.nps = (ms ? (double)stats.nodes / (ms / 1000.0) : (double)stats.nodes);
  };

  try {
    // --- legalize root moves once ---
    std::vector<model::Move> rootMoves;
    mg.generatePseudoLegalMoves(pos.getBoard(), pos.getState(), rootMoves);
    if (!rootMoves.empty()) {
      std::vector<model::Move> legalRoot;
      legalRoot.reserve(rootMoves.size());
      for (const auto& m : rootMoves) {
        MoveUndoGuard guard(pos);
        if (guard.doMove(m)) {
          legalRoot.push_back(m);
          guard.rollback();
        }
      }
      rootMoves.swap(legalRoot);
    }
    if (rootMoves.empty()) {
      stats.nodes = flush_node_batch(sharedNodes);
      update_time_stats();
      this->stopFlag.reset();
      const int score = pos.inCheck() ? mated_in(0) : 0;
      stats.bestScore = score;
      stats.bestMove = model::Move{};
      stats.bestPV.clear();
      stats.topMoves.clear();
      return score;
    }

    auto bound_rank = [](model::Bound b) {
      switch (b) {
        case model::Bound::Exact:
          return 2;
        case model::Bound::Lower:
          return 1;
        case model::Bound::Upper:
          return 0;
      }
      return 0;
    };

    auto score_root_move = [&](const model::Move& m, const model::Move& ttMove, bool haveTT,
                               int curDepth) {
      int s = 0;

      if (haveTT && m == ttMove) s += 2'500'000;

      if (m.promotion() != core::PieceType::None) {
        s += 1'200'000;
      } else if (m.isCapture()) {
        s += 1'050'000 + mvv_lva_fast(pos, m);
      } else {
        // quiet move
        const auto& board = pos.getBoard();
        int h = history[m.from()][m.to()];
        h = std::clamp(h, -20'000, 20'000);
        s += h;

        // Threat-Signale / checks: always detect checks; other signals gated
        auto mover = board.getPiece(m.from());
        if (mover) {
          const auto signals = compute_quiet_signals(pos, m);
          int piece_sig = signals.pieceSignal;  // detects direct checks (==2)
          int pawn_sig = 0;

          bool doThreat = cfg.useThreatSignals && curDepth <= cfg.threatSignalsDepthMax &&
                          h >= cfg.threatSignalsHistMin;

          const bool allowPawnThreats = doThreat && piece_sig < 2;
          if (allowPawnThreats) pawn_sig = signals.pawnSignal;

          if (signals.givesCheck) {
            piece_sig = std::max(piece_sig, 2);
            if (mover->type == core::PieceType::Pawn) pawn_sig = std::max(pawn_sig, 2);
          }

          const int sig = std::max(pawn_sig, piece_sig);
          if (sig == 2)
            s += 3'000;
          else if (sig == 1)
            s += 1'000;
        }
      }
      return s;
    };

    struct RootLine {
      model::Move m{};
      int score = -INF;  // exact if full-rescored, else bound
      model::Bound bound = model::Bound::Upper;
      int ordIdx = 0;  // stable order index
      bool exactFull = false;
    };

    // aspiration seed
    int lastScore = 0;
    if (cfg.useAspiration) {
      model::TTEntry5 tte{};
      if (tt.probe_into(pos.hash(), tte)) lastScore = decode_tt_score(tte.value, /*ply=*/0);
    }

    model::Move prevBest{};
    const int maxD = std::max(1, maxDepth);

    for (int depth = 1; depth <= maxD; ++depth) {
      if (stop && stop->load(std::memory_order_relaxed)) break;

      if (depth > 1) decay_tables(*this, /*shift=*/6);

      // TT move only as soft hint
      model::Move ttMove{};
      bool haveTT = false;
      if (model::TTEntry5 tte{}; tt.probe_into(pos.hash(), tte)) {
        haveTT = true;
        ttMove = tte.best;
      }

      // order root moves (stable)
      struct Scored {
        model::Move m;
        int s;
      };
      std::vector<Scored> scored;
      scored.reserve(rootMoves.size());
      for (const auto& m : rootMoves)
        scored.push_back({m, score_root_move(m, ttMove, haveTT, depth)});
      std::stable_sort(scored.begin(), scored.end(), [](const Scored& a, const Scored& b) {
        if (a.s != b.s) return a.s > b.s;
        if (a.m.from() != b.m.from()) return a.m.from() < b.m.from();
        return a.m.to() < b.m.to();
      });
      for (std::size_t i = 0; i < scored.size(); ++i) rootMoves[i] = scored[i].m;

      // push previous best to front for stability
      if (prevBest.from() != prevBest.to()) {
        auto it = std::find(rootMoves.begin(), rootMoves.end(), prevBest);
        if (it != rootMoves.end()) std::rotate(rootMoves.begin(), it, it + 1);
      }

      // aspiration window
      int alphaTarget = -INF + 1, betaTarget = INF - 1;
      int window = 24;
      if (cfg.useAspiration && depth >= 3 && !is_mate_score(lastScore)) {
        window = std::max(12, cfg.aspirationWindow);
        alphaTarget = lastScore - window;
        betaTarget = lastScore + window;
      }

      int bestScore = -INF;
      model::Move bestMove{};

      while (true) {
        if (stop && stop->load(std::memory_order_relaxed)) break;

        int alpha = alphaTarget, beta = betaTarget;
        std::vector<RootLine> lines;
        lines.reserve(rootMoves.size());

        int moveIdx = 0;
        for (const auto& m : rootMoves) {
          if (stop && stop->load(std::memory_order_relaxed)) break;

          const bool isQuietRoot = !m.isCapture() && (m.promotion() == core::PieceType::None);
          const QuietSignals rootSignals =
              isQuietRoot ? compute_quiet_signals(pos, m) : QuietSignals{};
          const bool quietCheckRoot = isQuietRoot && rootSignals.givesCheck;
          bool pawnQuietCheckRoot = false;
          if (quietCheckRoot) {
            if (auto mover = pos.getBoard().getPiece(m.from());
                mover && mover->type == core::PieceType::Pawn) {
              pawnQuietCheckRoot = true;
            }
          }

          MoveUndoGuard rg(pos);
          if (!rg.doMove(m)) {
            ++moveIdx;
            continue;
          }
          tt.prefetch(pos.hash());

          model::Move childBest{};
          int s;

          if (moveIdx == 0) {
            // full window for first (PVS root)
            s = -negamax(pos, depth - 1, -beta, -alpha, 1, childBest, INF);
          } else {
            // Root Move Reductions (light) + PVS
            int r = 0;
            const bool rootIsCapture = m.isCapture();
            const bool rootIsPromo = (m.promotion() != core::PieceType::None);
            if (rootIsCapture || rootIsPromo)
              r = 0;  // never reduce tactical roots
            else if (depth >= 6) {
              int hist = history[m.from()][m.to()];
              bool isQuietRoot = !m.isCapture() && (m.promotion() == core::PieceType::None);

              // Base reduction for later root moves
              if (isQuietRoot) r = 1;
              if (depth >= 10) r++;
              if (moveIdx >= 3) r++;
              if (hist < 0) r++;

              // Slight preference for quiet checks: reduce one step less, but never to zero just
              // because it checks
              if (isQuietRoot && quietCheckRoot) r = std::max(0, r - 1);

              if (depth <= 7) r = std::max(0, r - 1);
              r = std::clamp(r, 0, depth - 2);
            }

            if (r > 0) {
              s = -negamax(pos, (depth - 1) - r, -(alpha + 1), -alpha, 1, childBest, INF);
              if (s > alpha) {
                s = -negamax(pos, depth - 1, -(alpha + 1), -alpha, 1, childBest, INF);
                if (s > alpha && s < beta)
                  s = -negamax(pos, depth - 1, -beta, -alpha, 1, childBest, INF);
              }
            } else {
              s = -negamax(pos, depth - 1, -(alpha + 1), -alpha, 1, childBest, INF);
              if (s > alpha && s < beta)
                s = -negamax(pos, depth - 1, -beta, -alpha, 1, childBest, INF);
            }
          }

          s = std::clamp(s, -MATE + 1, MATE - 1);
          model::Bound b = model::Bound::Exact;
          if (s <= alpha)
            b = model::Bound::Upper;
          else if (s >= beta)
            b = model::Bound::Lower;

          lines.push_back(RootLine{m, s, b, moveIdx, /*exactFull*/ false});

          if (s > bestScore) {
            bestScore = s;
            bestMove = m;
          }
          if (s > alpha) alpha = s;

          rg.rollback();
          ++moveIdx;
          if (alpha >= beta) break;
        }

        // success if inside window
        if (bestScore > alphaTarget && bestScore < betaTarget) {
          auto full_rescore = [&](RootLine& rl) {
            MoveUndoGuard rg(pos);
            if (!rg.doMove(rl.m)) return;
            model::Move dummy{};
            int exact = -negamax(pos, depth - 1, -INF + 1, INF - 1, 1, dummy, INF);
            rl.score = std::clamp(exact, -MATE + 1, MATE - 1);
            rl.bound = model::Bound::Exact;
            rl.exactFull = true;
          };

          for (auto& rl : lines)
            if (rl.m == bestMove) {
              full_rescore(rl);
              break;
            }

          // Only rescore other moves if cfg.fullRescoreTopK > 1
          if (cfg.fullRescoreTopK > 1) {
            std::stable_sort(lines.begin(), lines.end(), [](const RootLine& a, const RootLine& b) {
              if (a.score != b.score) return a.score > b.score;
              return a.ordIdx < b.ordIdx;
            });
            int rescored = 1;
            for (auto& rl : lines) {
              if (rescored >= cfg.fullRescoreTopK) break;
              if (rl.m == bestMove) continue;
              full_rescore(rl);
              ++rescored;
            }
          }

          // pick final best (exact first, then score, then ordIdx)
          auto rank_bound = [](model::Bound b) {
            switch (b) {
              case model::Bound::Exact:
              case model::Bound::Lower:
                return 2;
              case model::Bound::Upper:
              default:
                return 1;
            }
          };
          std::stable_sort(lines.begin(), lines.end(), [&](const RootLine& a, const RootLine& b) {
            const int ra = rank_bound(a.bound), rb = rank_bound(b.bound);
            if (ra != rb) return ra > rb;
            if (a.score != b.score) return a.score > b.score;
            return a.ordIdx < b.ordIdx;
          });

          if (!lines.empty() && lines.front().bound != model::Bound::Exact) {
            full_rescore(lines.front());
            std::stable_sort(lines.begin(), lines.end(), [&](const RootLine& a, const RootLine& b) {
              const int ra = rank_bound(a.bound), rb = rank_bound(b.bound);
              if (ra != rb) return ra > rb;
              if (a.score != b.score) return a.score > b.score;
              return a.ordIdx < b.ordIdx;
            });
          }

          const model::Move finalBest = lines.front().m;
          const int finalScore = lines.front().score;

          // stats & PV
          stats.nodes = flush_node_batch(sharedNodes);
          update_time_stats();

          stats.bestScore = finalScore;
          stats.bestMove = finalBest;
          prevBest = finalBest;

          stats.bestPV.clear();
          {
            model::Position tmp = pos;
            if (tmp.doMove(finalBest)) {
              stats.bestPV.push_back(finalBest);
              auto rest = build_pv_from_tt(tmp, 32);
              for (auto& mv : rest) stats.bestPV.push_back(mv);
            }
          }

          // build exact-only topMoves (best first)
          stats.topMoves.clear();
          stats.topMoves.push_back({finalBest, finalScore});
          for (const auto& rl : lines) {
            if ((int)stats.topMoves.size() >= 5) break;
            if (rl.m == finalBest) continue;
            if (rl.bound == model::Bound::Exact) stats.topMoves.push_back({rl.m, rl.score});
          }
          if (stats.topMoves.size() > 1) {
            std::stable_sort(stats.topMoves.begin() + 1, stats.topMoves.end(),
                             [](const auto& a, const auto& b) { return a.second > b.second; });
          }

          break;  // depth done
        }

        // widen window
        if (bestScore <= alphaTarget) {
          int step = std::max(32, window);
          alphaTarget = std::max(-INF + 1, alphaTarget - step);
          window += step / 2;
        } else if (bestScore >= betaTarget) {
          int step = std::max(32, window);
          betaTarget = std::min(INF - 1, betaTarget + step);
          window += step / 2;
        } else {
          break;  // shouldn't happen
        }
      }  // aspiration loop

      if (is_mate_score(stats.bestScore)) break;
      lastScore = stats.bestScore;
    }  // depth loop

    stats.nodes = flush_node_batch(sharedNodes);
    update_time_stats();
    this->stopFlag.reset();
    return stats.bestScore;
  } catch (const SearchStoppedException&) {
    // Ensure final stats are coherent on timeout/stop:
    stats.nodes = sharedNodes ? sharedNodes->load(std::memory_order_relaxed) : 0;
    auto now = steady_clock::now();
    std::uint64_t ms =
        (std::uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(now - t0).count();
    stats.elapsedMs = ms;
    stats.nps = (ms ? (double)stats.nodes / (ms / 1000.0) : (double)stats.nodes);
    this->stopFlag.reset();
    return stats.bestScore;  // return the last known best score
  }
}

int Search::search_root_lazy_smp(model::Position& pos, int maxDepth,
                                 std::shared_ptr<std::atomic<bool>> stop, int maxThreads,
                                 std::uint64_t maxNodes) {
  const int threads = std::max(1, maxThreads > 0 ? std::min(maxThreads, cfg.threads) : cfg.threads);
  // Eine gemeinsame TT-Generation
  try {
    tt.new_generation();
  } catch (...) {
  }

  if (threads <= 1) return search_root_single(pos, maxDepth, stop, maxNodes);

  auto& pool = ThreadPool::instance();
  auto sharedCounter = std::make_shared<std::atomic<std::uint64_t>>(0);
  sharedCounter->store(0, std::memory_order_relaxed);  // reset once, up front
  const auto smpStart = steady_clock::now();

  std::vector<std::unique_ptr<Search>> workers;
  workers.reserve(threads);
  for (int t = 0; t < threads; ++t) {
    auto w = std::make_unique<Search>(tt, eval_, cfg);
    w->set_thread_id(t);
    w->stopFlag = stop;
    w->set_node_limit(sharedCounter, maxNodes);
    w->copy_heuristics_from(*this);
    workers.emplace_back(std::move(w));
  }
  for (auto& w : workers) w->stopFlag = stop;
  this->set_node_limit(sharedCounter, maxNodes);

  int mainScore = 0;

  // Snapshot
  const model::Position rootSnapshot = pos;

  // Helfer starten
  std::vector<std::future<int>> futs;
  futs.reserve(threads - 1);
  for (int t = 1; t < threads; ++t) {
    futs.emplace_back(pool.submit([rootSnapshot, &workers, maxDepth, stop, tid = t] {
      model::Position local = rootSnapshot;
      return workers[tid]->search_root_single(local, maxDepth, stop, /*maxNodes*/ 0);
    }));
  }

  // Main search
  mainScore = this->search_root_single(pos, maxDepth, stop, /*maxNodes*/ 0);

  // Main is done stop helper
  if (stop) stop->store(true, std::memory_order_relaxed);

  // wait
  for (auto& f : futs) {
    try {
      (void)f.get();
    } catch (...) {
    }
  }

  // fold worker heuristics back into main
  for (int t = 1; t < threads; ++t) {
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

void Search::clearSearchState() {
  for (auto& kk : killers) {
    kk[0] = model::Move{};
    kk[1] = model::Move{};
  }
  for (auto& h : history) h.fill(0);
  std::fill(&quietHist[0][0], &quietHist[0][0] + PIECE_NB * SQ_NB, 0);
  std::fill(&captureHist[0][0][0], &captureHist[0][0][0] + PIECE_NB * SQ_NB * PIECE_NB, 0);
  std::fill(&counterHist[0][0], &counterHist[0][0] + SQ_NB * SQ_NB, 0);
  std::memset(contHist, 0, sizeof(contHist));
  for (auto& row : counterMove)
    for (auto& m : row) m = model::Move{};
  for (auto& pm : prevMove) pm = model::Move{};
  stats = SearchStats{};
}

void Search::copy_heuristics_from(const Search& src) {
  // History-like tables
  history = src.history;

  std::memcpy(quietHist, src.quietHist, sizeof(quietHist));
  std::memcpy(captureHist, src.captureHist, sizeof(captureHist));
  std::memcpy(counterHist, src.counterHist, sizeof(counterHist));
  std::memcpy(counterMove, src.counterMove, sizeof(counterMove));

  // Killers nicht kopieren
  for (auto& kk : killers) {
    kk[0] = model::Move{};
    kk[1] = model::Move{};
  }

  // prevMove buffers sind lokaler Pfad-State
  for (auto& pm : prevMove) pm = model::Move{};
}

// EMA merge toward the worker's values: G += (L - G) / K
static inline int16_t ema_merge(int16_t G, int16_t L, int K) {
  int d = (int)L - (int)G;
  return clamp16((int)G + d / K);
}

void Search::merge_from(const Search& o) {
  // kleiner K -> schnelleres Lernen
  constexpr int K = 4;

  // Base history
  for (int f = 0; f < SQ_NB; ++f)
    for (int t = 0; t < SQ_NB; ++t) history[f][t] = ema_merge(history[f][t], o.history[f][t], K);

  // Quiet history
  for (int p = 0; p < PIECE_NB; ++p)
    for (int t = 0; t < SQ_NB; ++t)
      quietHist[p][t] = ema_merge(quietHist[p][t], o.quietHist[p][t], K);

  // Capture history
  for (int mp = 0; mp < PIECE_NB; ++mp)
    for (int t = 0; t < SQ_NB; ++t)
      for (int cp = 0; cp < PIECE_NB; ++cp)
        captureHist[mp][t][cp] = ema_merge(captureHist[mp][t][cp], o.captureHist[mp][t][cp], K);

  // Counter history + best countermove choice
  for (int f = 0; f < SQ_NB; ++f) {
    for (int t = 0; t < SQ_NB; ++t) {
      counterHist[f][t] = ema_merge(counterHist[f][t], o.counterHist[f][t], K);
      if (o.counterHist[f][t] > counterHist[f][t]) {
        counterMove[f][t] = o.counterMove[f][t];
      }
    }
  }

  // Continuation History (EMA)
  for (int L = 0; L < CH_LAYERS; ++L)
    for (int pp = 0; pp < PIECE_NB; ++pp)
      for (int pt = 0; pt < SQ_NB; ++pt)
        for (int mp = 0; mp < PIECE_NB; ++mp)
          for (int to = 0; to < SQ_NB; ++to)
            contHist[L][pp][pt][mp][to] =
                ema_merge(contHist[L][pp][pt][mp][to], o.contHist[L][pp][pt][mp][to], K);
}

}  // namespace lilia::engine
