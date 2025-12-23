
#include "lilia/uci/uci.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <future>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
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

static std::string extract_fen_after(const std::string& line) {
  auto pos = line.find("fen");
  if (pos == std::string::npos) return "";
  pos += 3;
  while (pos < line.size() && isspace((unsigned char)line[pos])) ++pos;
  auto moves_pos = line.find(" moves ", pos);
  if (moves_pos == std::string::npos) return line.substr(pos);
  return line.substr(pos, moves_pos - pos);
}

void UCI::showOptions() {
  const auto& c = m_options.cfg;
  std::cout << "option name Hash type spin default " << c.ttSizeMb << " min 1 max 131072\n";
  std::cout << "option name Threads type spin default " << c.threads << " min 1 max 64\n";
  std::cout << "option name Max Depth type spin default " << c.maxDepth << " min 1 max "
            << engine::MAX_PLY << "\n";
  std::cout << "option name Max Nodes type spin default " << c.maxNodes
            << " min 0 max 1000000000\n";
  std::cout << "option name Use Null Move type check default "
            << (c.useNullMove ? "true" : "false") << "\n";
  std::cout << "option name Use LMR type check default "
            << (c.useLMR ? "true" : "false") << "\n";
  std::cout << "option name Use Aspiration type check default "
            << (c.useAspiration ? "true" : "false") << "\n";
  std::cout << "option name Aspiration Window type spin default " << c.aspirationWindow
            << " min 1 max 1000\n";
  std::cout << "option name Use LMP type check default "
            << (c.useLMP ? "true" : "false") << "\n";
  std::cout << "option name Use IID type check default "
            << (c.useIID ? "true" : "false") << "\n";
  std::cout << "option name Use Singular Extension type check default "
            << (c.useSingularExt ? "true" : "false") << "\n";
  std::cout << "option name LMP Depth Max type spin default " << c.lmpDepthMax
            << " min 0 max 10\n";
  std::cout << "option name LMP Base type spin default " << c.lmpBase
            << " min 0 max 10\n";
  std::cout << "option name Use Futility type check default "
            << (c.useFutility ? "true" : "false") << "\n";
  std::cout << "option name Futility Margin type spin default " << c.futilityMargin
            << " min 0 max 1000\n";
  std::cout << "option name Use Reverse Futility type check default "
            << (c.useReverseFutility ? "true" : "false") << "\n";
  std::cout << "option name Use SEE Pruning type check default "
            << (c.useSEEPruning ? "true" : "false") << "\n";
  std::cout << "option name Use Prob Cut type check default "
            << (c.useProbCut ? "true" : "false") << "\n";
  std::cout << "option name Qsearch Quiet Checks type check default "
            << (c.qsearchQuietChecks ? "true" : "false") << "\n";
  std::cout << "option name LMR Base type spin default " << c.lmrBase
            << " min 0 max 10\n";
  std::cout << "option name LMR Max type spin default " << c.lmrMax
            << " min 0 max 10\n";
  std::cout << "option name LMR Use History type check default "
            << (c.lmrUseHistory ? "true" : "false") << "\n";
  std::cout << "option name Ponder type check default "
            << (m_options.ponder ? "true" : "false") << "\n";
  std::cout << "option name Move Overhead type spin default " << m_options.moveOverhead
            << " min 0 max 5000\n";
}

void UCI::setOption(const std::string& line) {
  auto tokens = split_ws(line);
  std::string name;
  std::string value;
  for (size_t i = 1; i + 1 < tokens.size(); ++i) {
    if (tokens[i] == "name") {
      size_t j = i + 1;
      std::ostringstream n;
      while (j < tokens.size() && tokens[j] != "value") {
        if (n.tellp() > 0) n << ' ';
        n << tokens[j++];
      }
      name = n.str();
    }
    if (tokens[i] == "value") {
      size_t j = i + 1;
      std::ostringstream v;
      while (j < tokens.size()) {
        if (v.tellp() > 0) v << ' ';
        v << tokens[j++];
      }
      value = v.str();
    }
  }
  if (name.empty()) return;

  auto to_bool = [](std::string v) {
    std::transform(v.begin(), v.end(), v.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return (v == "true" || v == "1" || v == "on");
  };

  if (name == "Hash") {
    int v = std::stoi(value);
    v = std::max(1, std::min(131072, v));
    m_options.cfg.ttSizeMb = v;
  } else if (name == "Threads") {
    int v = std::stoi(value);
    v = std::max(1, std::min(64, v));
    m_options.cfg.threads = v;
  } else if (name == "Max Depth") {
    int v = std::stoi(value);
    v = std::max(1, std::min(engine::MAX_PLY, v));
    m_options.cfg.maxDepth = v;
  } else if (name == "Max Nodes") {
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
    int v = std::stoi(value);
    if (v < 1) v = 1;
    if (v > 1000) v = 1000;
    m_options.cfg.aspirationWindow = v;
  } else if (name == "Use LMP") {
    m_options.cfg.useLMP = to_bool(value);
  } else if (name == "Use IID") {
    m_options.cfg.useIID = to_bool(value);
  } else if (name == "Use Singular Extension") {
    m_options.cfg.useSingularExt = to_bool(value);
  } else if (name == "LMP Depth Max") {
    int v = std::stoi(value);
    if (v < 0) v = 0;
    if (v > 10) v = 10;
    m_options.cfg.lmpDepthMax = v;
  } else if (name == "LMP Base") {
    int v = std::stoi(value);
    if (v < 0) v = 0;
    if (v > 10) v = 10;
    m_options.cfg.lmpBase = v;
  } else if (name == "Use Futility") {
    m_options.cfg.useFutility = to_bool(value);
  } else if (name == "Futility Margin") {
    int v = std::stoi(value);
    if (v < 0) v = 0;
    if (v > 1000) v = 1000;
    m_options.cfg.futilityMargin = v;
  } else if (name == "Use Reverse Futility") {
    m_options.cfg.useReverseFutility = to_bool(value);
  } else if (name == "Use SEE Pruning") {
    m_options.cfg.useSEEPruning = to_bool(value);
  } else if (name == "Use Prob Cut") {
    m_options.cfg.useProbCut = to_bool(value);
  } else if (name == "Qsearch Quiet Checks") {
    m_options.cfg.qsearchQuietChecks = to_bool(value);
  } else if (name == "LMR Base") {
    int v = std::stoi(value);
    if (v < 0) v = 0;
    if (v > 10) v = 10;
    m_options.cfg.lmrBase = v;
  } else if (name == "LMR Max") {
    int v = std::stoi(value);
    if (v < 0) v = 0;
    if (v > 10) v = 10;
    m_options.cfg.lmrMax = v;
  } else if (name == "LMR Use History") {
    m_options.cfg.lmrUseHistory = to_bool(value);
  } else if (name == "Ponder") {
    m_options.ponder = to_bool(value);
  } else if (name == "Move Overhead") {
    int v = std::stoi(value);
    m_options.moveOverhead = std::max(0, v);
  }
}

int UCI::run() {
  engine::Engine::init();
  std::string line;

  std::mutex stateMutex;
  std::future<lilia::model::Move> searchFuture;
  std::thread printerThread;
  std::atomic<bool> cancelToken(false);
  bool searchRunning = false;

  while (std::getline(std::cin, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.empty()) continue;

    auto tokens = split_ws(line);
    if (tokens.empty()) continue;
    const std::string& cmd = tokens[0];

    if (cmd == "uci") {
      std::cout << "id name " << m_name << "\n";
      std::cout << "id author unknown\n";
      std::cout << "id version " << m_version << "\n";
      showOptions();
      std::cout << "uciok\n";
      continue;
    }

    if (cmd == "isready") {
      std::cout << "readyok\n";
      continue;
    }

    if (cmd == "setoption") {
      setOption(line);
      continue;
    }

    if (cmd == "ucinewgame") {
      continue;
    }

    if (cmd == "position") {
      if (line.find("startpos") != std::string::npos) {
        m_game.setPosition(core::START_FEN);
      } else if (line.find("fen") != std::string::npos) {
        std::string fen = extract_fen_after(line);
        if (!fen.empty()) {
          m_game.setPosition(fen);
        }
      }
      auto posMoves = line.find("moves");
      if (posMoves != std::string::npos) {
        std::string movesStr = line.substr(posMoves + 6);
        std::istringstream iss(movesStr);
        std::string moveUci;
        while (iss >> moveUci) {
          try {
            m_game.doMoveUCI(moveUci);
          } catch (...) {
            std::cerr << "[UCI] warning: applyMoveUCI failed for " << moveUci << "\n";
          }
        }
      }
      continue;
    }

    if (cmd == "go") {
      int depth = -1;
      int movetime = -1;
      int wtime = -1, btime = -1, winc = 0, binc = 0, movestogo = 0;
      bool infinite = false;
      bool ponder = false;
      for (size_t i = 1; i < tokens.size(); ++i) {
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
        } else if (tokens[i] == "infinite") {
          infinite = true;
        } else if (tokens[i] == "ponder") {
          ponder = true;
        }
      }

      {
        std::lock_guard<std::mutex> lk(stateMutex);
        if (searchRunning) {
          cancelToken.store(true);
          if (printerThread.joinable()) printerThread.join();
          searchRunning = false;
          cancelToken.store(false);
        }
      }

      // determine think time
      int thinkMillis = 0;
      if (movetime > 0)
        thinkMillis = movetime;
      else if (!infinite && !(ponder && m_options.ponder)) {
        int timeLeft = (m_game.getGameState().sideToMove == core::Color::White ? wtime : btime);
        int inc = (m_game.getGameState().sideToMove == core::Color::White ? winc : binc);
        if (timeLeft >= 0) {
          if (movestogo > 0)
            thinkMillis = timeLeft / movestogo;
          else
            thinkMillis = timeLeft / 30;
          thinkMillis += inc;
          thinkMillis -= m_options.moveOverhead;
          if (thinkMillis < 0) thinkMillis = 0;
        }
      }

      cancelToken.store(false);
      {
        std::lock_guard<std::mutex> lk(stateMutex);
        auto cfg = m_options.toEngineConfig();
        int searchDepth = depth > 0 ? depth : m_options.cfg.maxDepth;
        searchFuture = std::async(
            std::launch::async,
            [this, searchDepth, thinkMillis, &cancelToken, cfg]() -> model::Move {
              lilia::engine::BotEngine engine(cfg);
              auto res = engine.findBestMove(m_game, searchDepth, thinkMillis, &cancelToken);
              if (res.bestMove.has_value()) return res.bestMove.value();
              return model::Move{};
            });

        searchRunning = true;

        printerThread = std::thread([&searchFuture, &stateMutex, &searchRunning, &cancelToken]() {
          model::Move best;
          try {
            best = searchFuture.get();
          } catch (...) {
            best = model::Move{};
          }

          if (best.from() >= 0 && best.to() >= 0) {
            std::cout << "bestmove " << move_to_uci(best) << "\n";
          } else {
            std::cout << "bestmove 0000\n";
          }

          {
            std::lock_guard<std::mutex> lk2(stateMutex);
            searchRunning = false;
            cancelToken.store(false);
          }
        });
      }

      continue;
    }

    if (cmd == "stop") {
      cancelToken.store(true);
      {
        std::lock_guard<std::mutex> lk(stateMutex);
        if (searchRunning && printerThread.joinable()) {
          printerThread.join();
          searchRunning = false;
          cancelToken.store(false);
        }
      }
      continue;
    }

    if (cmd == "ponderhit") {
      // no special handling needed in this simple engine
      continue;
    }

    if (cmd == "quit") {
      cancelToken.store(true);
      {
        std::lock_guard<std::mutex> lk(stateMutex);
        if (searchRunning && printerThread.joinable()) {
          printerThread.join();
        }
      }
      break;
    }
  }

  {
    std::lock_guard<std::mutex> lk(stateMutex);
    if (searchRunning && printerThread.joinable()) printerThread.join();
  }

  return 0;
}

}  // namespace lilia::uci
