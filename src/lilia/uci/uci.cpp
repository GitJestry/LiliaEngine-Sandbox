#include "lilia/uci/uci.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <charconv>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

#include "lilia/engine/bot_engine.hpp"
#include "lilia/model/chess_game.hpp"
#include "lilia/uci/uci_helper.hpp"

namespace lilia {

namespace {

// ---------------- Small, allocation-free utilities ----------------

static inline bool is_space(char c) noexcept {
  return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

static inline char lower_ascii(char c) noexcept {
  return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + ('a' - 'A')) : c;
}

static inline std::string_view trim(std::string_view s) noexcept {
  while (!s.empty() && is_space(s.front())) s.remove_prefix(1);
  while (!s.empty() && is_space(s.back())) s.remove_suffix(1);
  return s;
}

static inline bool ieq_lit(std::string_view s, const char* lit) noexcept {
  // Case-insensitive compare with ASCII lowering, for small option values.
  const char* p = lit;
  size_t i = 0;
  for (; p[i] != '\0'; ++i) {
    if (i >= s.size()) return false;
    if (lower_ascii(s[i]) != lower_ascii(p[i])) return false;
  }
  return i == s.size();
}

static inline bool to_bool_sv(std::string_view v) noexcept {
  v = trim(v);
  if (v.empty()) return false;
  // Fast common cases:
  if (v.size() == 1) {
    const char c = lower_ascii(v[0]);
    return c == '1' || c == 't' || c == 'y';
  }
  return ieq_lit(v, "true") || ieq_lit(v, "on") || ieq_lit(v, "yes");
}

template <class T>
static inline bool parse_int(std::string_view s, T& out) noexcept {
  s = trim(s);
  if (s.empty()) return false;
  const char* b = s.data();
  const char* e = b + s.size();
  auto [ptr, ec] = std::from_chars(b, e, out);
  return ec == std::errc{} && ptr == e;
}

template <typename T>
static inline T clampv(T v, T lo, T hi) noexcept {
  return v < lo ? lo : (v > hi ? hi : v);
}

// Fixed-size tokenizer: avoids heap churn in the UCI loop.
struct Tokenizer {
  static constexpr size_t MAX = 128;
  std::array<std::string_view, MAX> t{};
  size_t n = 0;

  void split(std::string_view s) noexcept {
    n = 0;
    const char* p = s.data();
    const char* end = p + s.size();

    while (p < end) {
      while (p < end && is_space(*p)) ++p;
      if (p >= end) break;
      const char* start = p;
      while (p < end && !is_space(*p)) ++p;
      if (n < MAX)
        t[n++] = std::string_view(start, static_cast<size_t>(p - start));
      else
        break;
    }
  }

  std::string_view operator[](size_t i) const noexcept { return t[i]; }
};

static inline std::string join_tokens(const Tokenizer& tok, size_t from, size_t to_excl) {
  if (from >= to_excl) return {};
  size_t total = 0;
  for (size_t i = from; i < to_excl; ++i) total += tok[i].size();
  total += (to_excl - from - 1);

  std::string out;
  out.reserve(total);
  for (size_t i = from; i < to_excl; ++i) {
    if (i != from) out.push_back(' ');
    out.append(tok[i].data(), tok[i].size());
  }
  return out;
}

}  // namespace

void UCI::showOptions() {
  const auto& c = m_options.cfg;

  // Buffering reduces iostream overhead and ensures fewer syscalls.
  std::ostringstream oss;
  oss << "option name Hash type spin default " << c.ttSizeMb << " min 1 max 131072\n";
  oss << "option name Threads type spin default " << c.threads << " min 0 max 64\n";
  oss << "option name Max Depth type spin default " << c.maxDepth << " min 1 max "
      << engine::MAX_PLY << "\n";
  oss << "option name Max Nodes type spin default " << c.maxNodes << " min 0 max 1000000000\n";
  oss << "option name Use Null Move type check default " << (c.useNullMove ? "true" : "false")
      << "\n";
  oss << "option name Use LMR type check default " << (c.useLMR ? "true" : "false") << "\n";
  oss << "option name Use Aspiration type check default " << (c.useAspiration ? "true" : "false")
      << "\n";
  oss << "option name Aspiration Window type spin default " << c.aspirationWindow
      << " min 1 max 1000\n";
  oss << "option name Use LMP type check default " << (c.useLMP ? "true" : "false") << "\n";
  oss << "option name Use IID type check default " << (c.useIID ? "true" : "false") << "\n";
  oss << "option name Use Singular Extension type check default "
      << (c.useSingularExt ? "true" : "false") << "\n";
  oss << "option name LMP Depth Max type spin default " << c.lmpDepthMax << " min 0 max 10\n";
  oss << "option name LMP Base type spin default " << c.lmpBase << " min 0 max 10\n";
  oss << "option name Use Futility type check default " << (c.useFutility ? "true" : "false")
      << "\n";
  oss << "option name Futility Margin type spin default " << c.futilityMargin
      << " min 0 max 1000\n";
  oss << "option name Use Reverse Futility type check default "
      << (c.useReverseFutility ? "true" : "false") << "\n";
  oss << "option name Use SEE Pruning type check default " << (c.useSEEPruning ? "true" : "false")
      << "\n";
  oss << "option name Use Prob Cut type check default " << (c.useProbCut ? "true" : "false")
      << "\n";
  oss << "option name Qsearch Quiet Checks type check default "
      << (c.qsearchQuietChecks ? "true" : "false") << "\n";
  oss << "option name LMR Base type spin default " << c.lmrBase << " min 0 max 10\n";
  oss << "option name LMR Max type spin default " << c.lmrMax << " min 0 max 10\n";
  oss << "option name LMR Use History type check default " << (c.lmrUseHistory ? "true" : "false")
      << "\n";
  oss << "option name Ponder type check default " << (m_options.ponder ? "true" : "false") << "\n";
  oss << "option name Move Overhead type spin default " << m_options.moveOverhead
      << " min 0 max 5000\n";

  std::cout << oss.str();
}

void UCI::setOption(const std::string& line) {
  // Parse raw line to correctly handle multi-word option names and values without token-joins.
  std::string_view s(line);

  // Require "name"
  const size_t namePos = s.find("name");
  if (namePos == std::string_view::npos) return;

  size_t nameStart = namePos + 4;
  if (nameStart >= s.size()) return;
  while (nameStart < s.size() && is_space(s[nameStart])) ++nameStart;

  // Optional "value"
  // We look for " value " (with spaces) to reduce false matches inside names.
  size_t valuePos = s.find(" value ", nameStart);

  std::string_view name;
  std::string_view value;

  if (valuePos == std::string_view::npos) {
    name = trim(s.substr(nameStart));
    value = {};
  } else {
    name = trim(s.substr(nameStart, valuePos - nameStart));
    const size_t vstart = valuePos + 7;  // len(" value ")
    value = (vstart <= s.size()) ? trim(s.substr(vstart)) : std::string_view{};
  }

  if (name.empty()) return;

  // Numeric parsing is now from_chars-based: no exceptions, much lower overhead.
  if (name == "Hash") {
    int v = 0;
    if (!parse_int(value, v)) return;
    m_options.cfg.ttSizeMb = clampv(v, 1, 131072);
  } else if (name == "Threads") {
    int v = 0;
    if (!parse_int(value, v)) return;
    m_options.cfg.threads = clampv(v, 0, 64);
  } else if (name == "Max Depth") {
    int v = 0;
    if (!parse_int(value, v)) return;
    m_options.cfg.maxDepth = clampv(v, 1, engine::MAX_PLY);
  } else if (name == "Max Nodes") {
    std::uint64_t v = 0;
    if (!parse_int(value, v)) return;
    if (v > 1000000000ULL) v = 1000000000ULL;
    m_options.cfg.maxNodes = v;
  } else if (name == "Use Null Move") {
    m_options.cfg.useNullMove = to_bool_sv(value);
  } else if (name == "Use LMR") {
    m_options.cfg.useLMR = to_bool_sv(value);
  } else if (name == "Use Aspiration") {
    m_options.cfg.useAspiration = to_bool_sv(value);
  } else if (name == "Aspiration Window") {
    int v = 0;
    if (!parse_int(value, v)) return;
    m_options.cfg.aspirationWindow = clampv(v, 1, 1000);
  } else if (name == "Use LMP") {
    m_options.cfg.useLMP = to_bool_sv(value);
  } else if (name == "Use IID") {
    m_options.cfg.useIID = to_bool_sv(value);
  } else if (name == "Use Singular Extension") {
    m_options.cfg.useSingularExt = to_bool_sv(value);
  } else if (name == "LMP Depth Max") {
    int v = 0;
    if (!parse_int(value, v)) return;
    m_options.cfg.lmpDepthMax = clampv(v, 0, 10);
  } else if (name == "LMP Base") {
    int v = 0;
    if (!parse_int(value, v)) return;
    m_options.cfg.lmpBase = clampv(v, 0, 10);
  } else if (name == "Use Futility") {
    m_options.cfg.useFutility = to_bool_sv(value);
  } else if (name == "Futility Margin") {
    int v = 0;
    if (!parse_int(value, v)) return;
    m_options.cfg.futilityMargin = clampv(v, 0, 1000);
  } else if (name == "Use Reverse Futility") {
    m_options.cfg.useReverseFutility = to_bool_sv(value);
  } else if (name == "Use SEE Pruning") {
    m_options.cfg.useSEEPruning = to_bool_sv(value);
  } else if (name == "Use Prob Cut") {
    m_options.cfg.useProbCut = to_bool_sv(value);
  } else if (name == "Qsearch Quiet Checks") {
    m_options.cfg.qsearchQuietChecks = to_bool_sv(value);
  } else if (name == "LMR Base") {
    int v = 0;
    if (!parse_int(value, v)) return;
    m_options.cfg.lmrBase = clampv(v, 0, 10);
  } else if (name == "LMR Max") {
    int v = 0;
    if (!parse_int(value, v)) return;
    m_options.cfg.lmrMax = clampv(v, 0, 10);
  } else if (name == "LMR Use History") {
    m_options.cfg.lmrUseHistory = to_bool_sv(value);
  } else if (name == "Ponder") {
    m_options.ponder = to_bool_sv(value);
  } else if (name == "Move Overhead") {
    int v = 0;
    if (!parse_int(value, v)) return;
    if (v < 0) v = 0;
    m_options.moveOverhead = v;
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

        auto res = engine.findBestMove(game, depth, thinkMillis, &cancelToken);

        model::Move best = model::Move{};
        if (res.bestMove.has_value()) best = *res.bestMove;

        if (!best.isNull()) {
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

  Tokenizer tok;

  while (std::getline(std::cin, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.empty()) continue;

    tok.split(std::string_view(line));
    if (tok.n == 0) continue;

    const std::string_view cmd = tok[0];

    if (cmd == "uci") {
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
      stopSearch();

      // position startpos [moves ...]
      // position fen <fen-string> [moves ...]
      size_t i = 1;

      if (i < tok.n && tok[i] == "startpos") {
        m_game.setPosition(core::START_FEN);
        ++i;
      } else {
        // Look for "fen"
        size_t fenPos = tok.n;
        for (size_t k = 1; k < tok.n; ++k) {
          if (tok[k] == "fen") {
            fenPos = k;
            break;
          }
        }

        if (fenPos < tok.n) {
          size_t j = fenPos + 1;
          while (j < tok.n && tok[j] != "moves") ++j;

          const std::string fenStr = join_tokens(tok, fenPos + 1, j);
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

      if (i < tok.n && tok[i] == "moves") {
        ++i;
        for (; i < tok.n; ++i) {
          try {
            // doMoveUCI likely takes std::string; construct once per move token.
            m_game.doMoveUCI(std::string(tok[i]));
          } catch (...) {
            std::cerr << "[UCI] warning: applyMoveUCI failed for " << tok[i] << "\n";
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

      for (size_t i = 1; i < tok.n; ++i) {
        if (tok[i] == "depth" && i + 1 < tok.n) {
          (void)parse_int(tok[++i], depth);
        } else if (tok[i] == "movetime" && i + 1 < tok.n) {
          (void)parse_int(tok[++i], movetime);
        } else if (tok[i] == "wtime" && i + 1 < tok.n) {
          (void)parse_int(tok[++i], wtime);
        } else if (tok[i] == "btime" && i + 1 < tok.n) {
          (void)parse_int(tok[++i], btime);
        } else if (tok[i] == "winc" && i + 1 < tok.n) {
          (void)parse_int(tok[++i], winc);
        } else if (tok[i] == "binc" && i + 1 < tok.n) {
          (void)parse_int(tok[++i], binc);
        } else if (tok[i] == "movestogo" && i + 1 < tok.n) {
          (void)parse_int(tok[++i], movestogo);
        } else if (tok[i] == "nodes" && i + 1 < tok.n) {
          (void)parse_int(tok[++i], nodes);
        } else if (tok[i] == "infinite") {
          infinite = true;
        } else if (tok[i] == "ponder") {
          ponder = true;
        }
      }

      model::ChessGame gameCopy = m_game;

      const int searchDepth = (depth > 0 ? depth : m_options.cfg.maxDepth);

      constexpr int UCI_UNBOUNDED_MS = 1'000'000'000;

      int thinkMillis = 0;
      if (movetime > 0) {
        thinkMillis = movetime;
      } else if (infinite || (ponder && m_options.ponder)) {
        thinkMillis = UCI_UNBOUNDED_MS;
      } else {
        const auto gs = m_game.getGameState();  // snapshot for consistent use
        const bool whiteToMove = (gs.sideToMove == core::Color::White);

        const int timeLeft = whiteToMove ? wtime : btime;
        const int inc = whiteToMove ? winc : binc;

        if (timeLeft >= 0) {
          if (movestogo > 0)
            thinkMillis = timeLeft / std::max(1, movestogo);
          else
            thinkMillis = timeLeft / 30;

          thinkMillis += inc;
          thinkMillis -= m_options.moveOverhead;
          if (thinkMillis < 0) thinkMillis = 0;
        } else {
          thinkMillis = 0;  // depth-only / no TC
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
      // Optional: implement if you support true pondering.
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
