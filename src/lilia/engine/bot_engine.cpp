#include "lilia/engine/bot_engine.hpp"

#define LOG 1

#include <chrono>
#include <condition_variable>
#include <iostream>
#include <memory>
#include <mutex>
#include <thread>

#include "lilia/model/chess_game.hpp"
#include "lilia/uci/uci_helper.hpp"

namespace lilia::engine {

BotEngine::BotEngine(const EngineConfig& cfg) : m_engine(cfg) {}
BotEngine::~BotEngine() = default;

static inline std::string format_top_moves(const std::vector<std::pair<model::Move, int>>& top) {
  std::string out;
  bool first = true;
  for (auto& p : top) {
    if (!first) out += ", ";
    first = false;
    out += move_to_uci(p.first) + "(" + std::to_string(p.second) + ")";
  }
  if (out.empty()) out = "<none>";
  return out;
}
SearchResult BotEngine::findBestMove(model::ChessGame& gameState, int maxDepth, int thinkMillis,
                                     std::atomic<bool>* externalCancel) {
  SearchResult res;
  auto pos = gameState.getPositionRefForBot();

  auto stopFlag = std::make_shared<std::atomic<bool>>(false);

  std::mutex m;
  std::condition_variable cv;
  bool timerStop = false;

  std::thread timer([&]() {
    auto checkCancel = [&]() {
      if (externalCancel && externalCancel->load()) {
        stopFlag->store(true);
        return true;
      }
      return false;
    };

    if (thinkMillis <= 0) {
      while (true) {
        std::unique_lock<std::mutex> lk(m);
        cv.wait_for(lk, std::chrono::milliseconds(50), [&] { return timerStop; });
        if (timerStop) return;
        if (checkCancel()) return;
      }
    }

    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(thinkMillis);
    while (true) {
      std::unique_lock<std::mutex> lk(m);
      cv.wait_for(lk, std::chrono::milliseconds(50), [&] { return timerStop; });
      if (timerStop) return;
      if (checkCancel()) return;
      if (std::chrono::steady_clock::now() >= deadline) {
        stopFlag->store(true);
        return;
      }
    }
  });

  using steady_clock = std::chrono::steady_clock;
  auto t0 = steady_clock::now();

  bool engineThrew = false;
  std::string engineErr;

  try {
    auto mv = m_engine.find_best_move(pos, maxDepth, stopFlag);
    res.bestMove = mv;  // std::optional<Move>
  } catch (const std::exception& e) {
    engineThrew = true;
    engineErr = e.what();
    std::cerr << "[BotEngine] engine threw exception: " << e.what() << "\n";
    res.bestMove.reset();
  } catch (...) {
    engineThrew = true;
    engineErr = "unknown exception";
    std::cerr << "[BotEngine] engine threw unknown exception\n";
    res.bestMove.reset();
  }

  auto t1 = steady_clock::now();
  long long elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();

  {
    std::lock_guard<std::mutex> lk(m);
    timerStop = true;
  }
  cv.notify_one();
  if (timer.joinable()) timer.join();

  // >>> Important: only adapt stats, when engine succeeded
  if (!engineThrew) {
    res.stats = m_engine.getLastSearchStats();
    res.topMoves = res.stats.topMoves;
  } else {
    res.stats = SearchStats{};
    res.topMoves.clear();
  }

#if LOG
  std::string reason;
  if (externalCancel && externalCancel->load()) {
    reason = "external-cancel";
  } else if (engineThrew) {
    reason = std::string("exception: ") + engineErr;
  } else if (stopFlag->load() && thinkMillis > 0 && elapsedMs >= thinkMillis) {
    reason = "timeout";
  } else {
    reason = "normal";
  }
  std::cout << "\n[BotEngine] Search finished: reason=" << reason << "\n";
  std::cout << "[BotEngine] depth=" << maxDepth << " time=" << elapsedMs
            << "ms maxTime=" << thinkMillis << "ms threads=" << m_engine.getConfig().threads
            << "\n";

  std::cout << "[BotEngine] info nodes=" << res.stats.nodes
            << " nps=" << static_cast<long long>(res.stats.nps) << " time=" << res.stats.elapsedMs
            << " bestScore=" << res.stats.bestScore;
  if (res.stats.bestMove.has_value()) {
    std::cout << " bestMove=" << move_to_uci(res.stats.bestMove.value());
  }
  std::cout << "\n";

  if (!res.stats.bestPV.empty()) {
    std::cout << "[BotEngine] pv ";
    bool first = true;
    for (auto& mv : res.stats.bestPV) {
      if (!first) std::cout << "->";
      first = false;
      std::cout << move_to_uci(mv);
    }
    std::cout << "\n";
  }

  if (!res.topMoves.empty()) {
    std::cout << "[BotEngine] topMoves " << format_top_moves(res.topMoves) << "\n";
  }
#else
#endif

  return res;
}

const SearchStats& BotEngine::getLastSearchStats() const {
  return m_engine.getLastSearchStats();
}

}  // namespace lilia::engine
