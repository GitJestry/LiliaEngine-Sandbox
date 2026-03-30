#include "lilia/tools/texel/dataset.hpp"

#include <array>
#include <algorithm>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <random>
#include <thread>
#include <unordered_set>
#include <utility>

#include "lilia/model/chess_game.hpp"
#include "lilia/model/core/model_types.hpp"
#include "lilia/tools/texel/common.hpp"
#include "lilia/tools/texel/progress.hpp"
#include "lilia/tools/texel/uci_engine.hpp"

namespace lilia::tools::texel {
namespace fs = std::filesystem;

static core::Color flip_color(core::Color c) {
  return c == core::Color::White ? core::Color::Black : core::Color::White;
}

static double result_from_pov(core::GameResult res, core::Color winner, core::Color pov) {
  switch (res) {
    case core::GameResult::CHECKMATE:
      return (winner == pov) ? 1.0 : 0.0;
    case core::GameResult::STALEMATE:
    case core::GameResult::REPETITION:
    case core::GameResult::MOVERULE:
    case core::GameResult::INSUFFICIENT:
      return 0.5;
    default:
      return 0.5;
  }
}

static void run_games_worker(int workerId, const Options& opts, std::atomic<int>& nextGame,
                             int totalGames, std::vector<RawSample>& outSamples,
                             std::mutex& outMutex, ProgressMeter& pm) {
  const uint64_t engineSeed =
      opts.seed ? (opts.seed ^ (0x9E3779B97F4A7C15ull + static_cast<uint64_t>(workerId))) : 0ull;
  UciEngine engine(opts.stockfishPath, opts, engineSeed);

  std::vector<RawSample> local;
  local.reserve(8192);
  std::vector<std::string> moveHistory;

  for (;;) {
    int g = nextGame.fetch_add(1, std::memory_order_relaxed);
    if (g >= totalGames) break;

    engine.new_game();

    model::ChessGame game;
    game.setPosition(core::START_FEN);
    moveHistory.clear();

    std::vector<std::pair<std::string, core::Color>> sampledPositions;
    sampledPositions.reserve(static_cast<size_t>(opts.maxPlies / std::max(1, opts.sampleStride)));

    std::array<int, 2> sideSampleCounters{0, 0};

    bool aborted = false;
    for (int ply = 0; ply < opts.maxPlies; ++ply) {
      game.checkGameResult();
      if (game.getResult() != core::GameResult::ONGOING) break;

      if (ply >= opts.sampleSkip) {
        const auto stm = game.getGameState().sideToMove;
        auto& counter = sideSampleCounters[static_cast<size_t>(stm)];
        if (counter % std::max(1, opts.sampleStride) == 0) {
          sampledPositions.emplace_back(game.getFen(), stm);
        }
        ++counter;
      }

      const std::string mv = engine.pick_move_from_startpos(moveHistory);
      if (mv.empty() || mv == "(none)") { aborted = true; break; }
      if (!game.doMoveUCI(mv)) { aborted = true; break; }
      moveHistory.push_back(mv);
    }

    game.checkGameResult();
    const core::GameResult finalRes = game.getResult();

    // If the game did not reach a terminal state (e.g., illegal move, engine issue), drop it.
    if (aborted || finalRes == core::GameResult::ONGOING) {
      pm.add(1);
      continue;
    }

    // For CHECKMATE, the side to move is checkmated, so the winner is the opposite.
    core::Color winner = flip_color(game.getGameState().sideToMove);

    for (const auto& [fen, pov] : sampledPositions) {
      RawSample s;
      s.fen = fen;
      s.result = result_from_pov(finalRes, winner, pov);
      local.push_back(std::move(s));
    }

    pm.add(1);
  }

  {
    std::lock_guard<std::mutex> lk(outMutex);
    outSamples.insert(outSamples.end(), std::make_move_iterator(local.begin()),
                      std::make_move_iterator(local.end()));
  }
}

std::vector<RawSample> generate_samples_parallel(const Options& opts) {
  if (!opts.generateData) return {};
  if (opts.stockfishPath.empty()) throw std::runtime_error("Stockfish path required for data generation");

  const int W = std::max(1, opts.genWorkers);
  std::vector<std::thread> threads;
  threads.reserve(W);

  std::vector<RawSample> samples;
  samples.reserve(static_cast<size_t>(opts.games) * 32u);

  std::mutex samplesMutex;
  std::atomic<int> nextGame{0};

  ProgressMeter pm("Generating self-play games (parallel)", static_cast<std::size_t>(opts.games),
                   opts.progressIntervalMs, true);

  for (int w = 0; w < W; ++w) {
    threads.emplace_back(run_games_worker, w, std::cref(opts), std::ref(nextGame), opts.games,
                         std::ref(samples), std::ref(samplesMutex), std::ref(pm));
  }
  for (auto& t : threads) t.join();
  pm.finish();

  // Deduplicate by position key (keep first occurrence).
  std::unordered_set<std::string> seen;
  seen.reserve(samples.size() * 2 + 16);

  std::vector<RawSample> unique;
  unique.reserve(samples.size());

  for (auto& s : samples) {
    auto key = fen_key(s.fen);
    if (seen.insert(key).second) unique.push_back(std::move(s));
  }

  if (opts.sampleLimit && unique.size() > static_cast<size_t>(*opts.sampleLimit))
    unique.resize(static_cast<size_t>(*opts.sampleLimit));
  return unique;
}

void write_dataset(const std::vector<RawSample>& samples, const std::string& path) {
  if (samples.empty()) return;

  fs::path p{path};
  if (p.has_parent_path()) {
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
  }

  std::ofstream out(path, std::ios::trunc);
  if (!out) throw std::runtime_error("Unable to write dataset: " + path);

  out << "# FEN|result\n";
  for (const auto& s : samples) out << s.fen << '|' << s.result << '\n';
  std::cout << "Wrote " << samples.size() << " unique samples to " << path << "\n";
}

std::vector<RawSample> read_dataset(const std::string& path) {
  std::ifstream in(path);
  if (!in) throw std::runtime_error("Unable to open dataset: " + path);

  std::vector<RawSample> samples;
  std::string line;
  while (std::getline(in, line)) {
    if (line.empty() || line[0] == '#') continue;
    const auto bar = line.find_last_of('|');
    if (bar == std::string::npos) continue;
    RawSample sample;
    sample.fen = line.substr(0, bar);
    sample.result = std::stod(line.substr(bar + 1));
    samples.push_back(std::move(sample));
  }
  return samples;
}

}  // namespace lilia::tools::texel
