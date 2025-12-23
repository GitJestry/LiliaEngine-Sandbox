#include "lilia/uci/uci.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <iostream>
#include <limits>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "lilia/engine/bot_engine.hpp"
#include "lilia/model/chess_game.hpp"
#include "lilia/uci/uci_helper.hpp"

namespace lilia {

static std::vector<std::string> split_ws(const std::string& s) {
  std::istringstream iss(s);
  std::vector<std::string> out;
  std::string tok;
  while (iss >> tok) out.push_back(tok);
  return out;
}

static bool ieq(const std::string& a, const std::string& b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i) {
    if (std::tolower((unsigned char)a[i]) != std::tolower((unsigned char)b[i])) return false;
  }
  return true;
}

static bool to_bool(std::string v) {
  std::transform(v.begin(), v.end(), v.begin(),
                 [](unsigned char c) { return (unsigned char)std::tolower(c); });
  return (v == "true" || v == "1" || v == "on" || v == "yes");
}

static std::string join_tokens(const std::vector<std::string>& t, size_t from, size_t to_excl) {
  std::ostringstream oss;
  for (size_t i = from; i < to_excl; ++i) {
    if (oss.tellp() > 0) oss << ' ';
    oss << t[i];
  }
  return oss.str();
}

void UCI::showOptions() {
  const auto& c = m_options.cfg;
  std::cout << "option name Hash type spin default " << c.ttSizeMb << " min 1 max 131072\n";
  std::cout << "option name Threads type spin default " << c.threads << " min 0 max 64\n";
  std::cout << "option name Max Depth type spin default " << c.maxDepth << " min 1 max "
            << engine::MAX_PLY << "\n";
  std::cout << "option name Max Nodes type spin default " << c.maxNodes
            << " min 0 max 1000000000\n";
  std::cout << "option name Use Null Move type check default " << (c.useNullMove ? "true" : "false")
            << "\n";
  std::cout << "option name Use LMR type check default " << (c.useLMR ? "true" : "false") << "\n";
  std::cout << "option name Use Aspiration type check default "
            << (c.useAspiration ? "true" : "false") << "\n";
  std::cout << "option name Aspiration Window type spin default " << c.aspirationWindow
            << " min 1 max 1000\n";
  std::cout << "option name Use LMP type check default " << (c.useLMP ? "true" : "false") << "\n";
  std::cout << "option name Use IID type check default " << (c.useIID ? "true" : "false") << "\n";
  std::cout << "option name Use Singular Extension type check default "
            << (c.useSingularExt ? "true" : "false") << "\n";
  std::cout << "option name LMP Depth Max type spin default " << c.lmpDepthMax << " min 0 max 10\n";
  std::cout << "option name LMP Base type spin default " << c.lmpBase << " min 0 max 10\n";
  std::cout << "option name Use Futility type check default " << (c.useFutility ? "true" : "false")
            << "\n";
  std::cout << "option name Futility Margin type spin default " << c.futilityMargin
            << " min 0 max 1000\n";
  std::cout << "option name Use Reverse Futility type check default "
            << (c.useReverseFutility ? "true" : "false") << "\n";
  std::cout << "option name Use SEE Pruning type check default "
            << (c.useSEEPruning ? "true" : "false") << "\n";
  std::cout << "option name Use Prob Cut type check default " << (c.useProbCut ? "true" : "false")
            << "\n";
  std::cout << "option name Qsearch Quiet Checks type check default "
            << (c.qsearchQuietChecks ? "true" : "false") << "\n";
  std::cout << "option name LMR Base type spin default " << c.lmrBase << " min 0 max 10\n";
  std::cout << "option name LMR Max type spin default " << c.lmrMax << " min 0 max 10\n";
  std::cout << "option name LMR Use History type check default "
            << (c.lmrUseHistory ? "true" : "false") << "\n";
  std::cout << "option name Ponder type check default " << (m_options.ponder ? "true" : "false")
            << "\n";
  std::cout << "option name Move Overhead type spin default " << m_options.moveOverhead
            << " min 0 max 5000\n";
}

void UCI::setOption(const std::string& line) {
  auto tokens = split_ws(line);
  std::string name;
  std::string value;

  // UCI format: setoption name <id> [value <x>]
  for (size_t i = 1; i < tokens.size(); ++i) {
    if (tokens[i] == "name") {
      size_t j = i + 1;
      std::ostringstream n;
      while (j < tokens.size() && tokens[j] != "value") {
        if (n.tellp() > 0) n << ' ';
        n << tokens[j++];
      }
      name = n.str();
      i = j - 1;
    } else if (tokens[i] == "value") {
      value = join_tokens(tokens, i + 1, tokens.size());
      break;
    }
  }
  if (name.empty()) return;

  try {
    if (name == "Hash") {
      if (value.empty()) return;
      int v = std::stoi(value);
      v = std::max(1, std::min(131072, v));
      m_options.cfg.ttSizeMb = v;
    } else if (name == "Threads") {
      if (value.empty()) return;
      int v = std::stoi(value);
      v = std::max(0, std::min(64, v));
      m_options.cfg.threads = v;
    } else if (name == "Max Depth") {
      if (value.empty()) return;
      int v = std::stoi(value);
      v = std::max(1, std::min(engine::MAX_PLY, v));
      m_options.cfg.maxDepth = v;
    } else if (name == "Max Nodes") {
      if (value.empty()) return;
      std::uint64_t v = std::stoull(value);
      if (v > 1000000000ULL) v = 1000000000ULL;
      m_options.cfg.maxNodes = v;
    } else if (name == "Use Null Move") {
      m_options.cfg.useNullMove = to_bool(value);
    } else if (name == "Use LMR") {
      m_options.cfg.useLMR = to_bool(value);
    } else if (name == "Use Aspiration") {
      m_options.cfg.useAspiration = to_bool(value);
    } else if (name == "Aspiration Window") {
      if (value.empty()) return;
      int v = std::stoi(value);
      v = std::max(1, std::min(1000, v));
      m_options.cfg.aspirationWindow = v;
    } else if (name == "Use LMP") {
      m_options.cfg.useLMP = to_bool(value);
    } else if (name == "Use IID") {
      m_options.cfg.useIID = to_bool(value);
    } else if (name == "Use Singular Extension") {
      m_options.cfg.useSingularExt = to_bool(value);
    } else if (name == "LMP Depth Max") {
      if (value.empty()) return;
      int v = std::stoi(value);
      m_options.cfg.lmpDepthMax = std::max(0, std::min(10, v));
    } else if (name == "LMP Base") {
      if (value.empty()) return;
      int v = std::stoi(value);
      m_options.cfg.lmpBase = std::max(0, std::min(10, v));
    } else if (name == "Use Futility") {
      m_options.cfg.useFutility = to_bool(value);
    } else if (name == "Futility Margin") {
      if (value.empty()) return;
      int v = std::stoi(value);
      m_options.cfg.futilityMargin = std::max(0, std::min(1000, v));
    } else if (name == "Use Reverse Futility") {
      m_options.cfg.useReverseFutility = to_bool(value);
    } else if (name == "Use SEE Pruning") {
      m_options.cfg.useSEEPruning = to_bool(value);
    } else if (name == "Use Prob Cut") {
      m_options.cfg.useProbCut = to_bool(value);
    } else if (name == "Qsearch Quiet Checks") {
      m_options.cfg.qsearchQuietChecks = to_bool(value);
    } else if (name == "LMR Base") {
      if (value.empty()) return;
      int v = std::stoi(value);
      m_options.cfg.lmrBase = std::max(0, std::min(10, v));
    } else if (name == "LMR Max") {
      if (value.empty()) return;
      int v = std::stoi(value);
      m_options.cfg.lmrMax = std::max(0, std::min(10, v));
    } else if (name == "LMR Use History") {
      m_options.cfg.lmrUseHistory = to_bool(value);
    } else if (name == "Ponder") {
      m_options.ponder = to_bool(value);
    } else if (name == "Move Overhead") {
      if (value.empty()) return;
      int v = std::stoi(value);
      m_options.moveOverhead = std::max(0, v);
    }
  } catch (...) {
    // Ignore malformed setoption rather than crashing the engine.
  }
}

int UCI::run() {
  engine::Engine::init();
  std::string line;

  std::mutex stateMutex;
  std::thread searchThread;
  std::atomic<bool> cancelToken(false);
  bool searchRunning = false;

  auto stopSearch = [&]() {
    std::thread t;
    {
      std::lock_guard<std::mutex> lk(stateMutex);
      if (searchThread.joinable()) {
        cancelToken.store(true, std::memory_order_release);
        t = std::move(searchThread);
      }
      searchRunning = false;
    }
    if (t.joinable()) t.join();
    cancelToken.store(false, std::memory_order_release);
  };

  auto startSearch = [&](model::ChessGame gameCopy, engine::EngineConfig cfg, int depth,
                         int thinkMillis) {
    stopSearch();

    {
      std::lock_guard<std::mutex> lk(stateMutex);
      cancelToken.store(false, std::memory_order_release);
      searchRunning = true;

      searchThread = std::thread([game = std::move(gameCopy), cfg, depth, thinkMillis, this,
                                  &cancelToken, &stateMutex, &searchRunning]() mutable {
        lilia::engine::BotEngine engine(cfg);

        // If your engine treats thinkMillis==0 as "return immediately", avoid that for
        // infinite/ponder. We use a large value instead of 0 for "unbounded" searches.
        auto res = engine.findBestMove(game, depth, thinkMillis, &cancelToken);

        model::Move best = model::Move{};
        if (res.bestMove.has_value()) best = res.bestMove.value();

        if (best.from() >= 0 && best.to() >= 0) {
          std::cout << "bestmove " << move_to_uci(best) << "\n";
        } else {
          std::cout << "bestmove 0000\n";
        }
        std::cout.flush();

        {
          std::lock_guard<std::mutex> lk2(stateMutex);
          searchRunning = false;
        }
      });
    }
  };

  while (std::getline(std::cin, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.empty()) continue;

    auto tokens = split_ws(line);
    if (tokens.empty()) continue;

    const std::string& cmd = tokens[0];

    if (cmd == "uci") {
      // Prefer the conventional form: include version in the name.
      std::cout << "id name " << m_name << " " << m_version << "\n";
      std::cout << "id author unknown\n";
      showOptions();
      std::cout << "uciok\n";
      std::cout.flush();
      continue;
    }

    if (cmd == "isready") {
      std::cout << "readyok\n";
      std::cout.flush();
      continue;
    }

    if (cmd == "setoption") {
      setOption(line);
      continue;
    }

    if (cmd == "ucinewgame") {
      stopSearch();
      m_game = model::ChessGame{};
      m_game.setPosition(core::START_FEN);
      continue;
    }

    if (cmd == "position") {
      // Critical: stop an active search before mutating position.
      stopSearch();

      // UCI: position startpos [moves ...]
      //      position fen <fen-string> [moves ...]
      size_t i = 1;

      if (i < tokens.size() && tokens[i] == "startpos") {
        m_game.setPosition(core::START_FEN);
        ++i;
      } else {
        // Find "fen" token (usually tokens[1] == "fen", but be tolerant)
        size_t fenPos = tokens.size();
        for (size_t k = 1; k < tokens.size(); ++k) {
          if (tokens[k] == "fen") {
            fenPos = k;
            break;
          }
        }

        if (fenPos < tokens.size()) {
          size_t j = fenPos + 1;
          std::ostringstream fen;
          while (j < tokens.size() && tokens[j] != "moves") {
            if (fen.tellp() > 0) fen << ' ';
            fen << tokens[j++];
          }
          const std::string fenStr = fen.str();
          if (!fenStr.empty()) {
            try {
              m_game.setPosition(fenStr);
            } catch (...) {
              std::cerr << "[UCI] warning: setPosition failed for fen: " << fenStr << "\n";
            }
          }
          i = j;
        }
      }

      if (i < tokens.size() && tokens[i] == "moves") {
        ++i;
        for (; i < tokens.size(); ++i) {
          try {
            m_game.doMoveUCI(tokens[i]);
          } catch (...) {
            std::cerr << "[UCI] warning: applyMoveUCI failed for " << tokens[i] << "\n";
          }
        }
      }

      continue;
    }

    if (cmd == "go") {
      int depth = -1;
      int movetime = -1;
      int wtime = -1, btime = -1, winc = 0, binc = 0, movestogo = 0;
      std::uint64_t nodes = 0;
      bool infinite = false;
      bool ponder = false;

      for (size_t i = 1; i < tokens.size(); ++i) {
        try {
          if (tokens[i] == "depth" && i + 1 < tokens.size()) {
            depth = std::stoi(tokens[++i]);
          } else if (tokens[i] == "movetime" && i + 1 < tokens.size()) {
            movetime = std::stoi(tokens[++i]);
          } else if (tokens[i] == "wtime" && i + 1 < tokens.size()) {
            wtime = std::stoi(tokens[++i]);
          } else if (tokens[i] == "btime" && i + 1 < tokens.size()) {
            btime = std::stoi(tokens[++i]);
          } else if (tokens[i] == "winc" && i + 1 < tokens.size()) {
            winc = std::stoi(tokens[++i]);
          } else if (tokens[i] == "binc" && i + 1 < tokens.size()) {
            binc = std::stoi(tokens[++i]);
          } else if (tokens[i] == "movestogo" && i + 1 < tokens.size()) {
            movestogo = std::stoi(tokens[++i]);
          } else if (tokens[i] == "nodes" && i + 1 < tokens.size()) {
            nodes = std::stoull(tokens[++i]);
          } else if (tokens[i] == "infinite") {
            infinite = true;
          } else if (tokens[i] == "ponder") {
            ponder = true;
          }
        } catch (...) {
          // Ignore malformed go parameters.
        }
      }

      // Snapshot position to avoid any shared-state issues.
      model::ChessGame gameCopy = m_game;

      // Compute depth and time budget.
      int searchDepth = (depth > 0 ? depth : m_options.cfg.maxDepth);

      // Use a large value (instead of 0) for "unbounded" searches to avoid accidental immediate
      // returns. 1e9 ms is ~11.6 days, which is effectively infinite for UCI, and cancellation
      // still stops it.
      constexpr int UCI_UNBOUNDED_MS = 1'000'000'000;

      int thinkMillis = 0;
      if (movetime > 0) {
        thinkMillis = movetime;
      } else if (infinite || (ponder && m_options.ponder)) {
        thinkMillis = UCI_UNBOUNDED_MS;
      } else {
        int timeLeft = (m_game.getGameState().sideToMove == core::Color::White ? wtime : btime);
        int inc = (m_game.getGameState().sideToMove == core::Color::White ? winc : binc);

        if (timeLeft >= 0) {
          if (movestogo > 0)
            thinkMillis = timeLeft / std::max(1, movestogo);
          else
            thinkMillis = timeLeft / 30;

          thinkMillis += inc;
          thinkMillis -= m_options.moveOverhead;
          if (thinkMillis < 0) thinkMillis = 0;
        } else {
          // No time controls provided: just search to depth.
          thinkMillis = 0;
        }
      }

      auto cfg = m_options.toEngineConfig();
      if (nodes > 0) cfg.maxNodes = nodes;

      startSearch(std::move(gameCopy), cfg, searchDepth, thinkMillis);
      continue;
    }

    if (cmd == "stop") {
      stopSearch();
      continue;
    }

    if (cmd == "ponderhit") {
      // If you later implement true pondering, convert this into "continue as normal move search"
      // rather than "ignore".
      continue;
    }

    if (cmd == "quit") {
      stopSearch();
      break;
    }
  }

  stopSearch();
  return 0;
}

}  // namespace lilia
