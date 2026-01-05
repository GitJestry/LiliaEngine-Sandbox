#include "lilia/tools/texel/uci_engine.hpp"

#include <filesystem>
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <utility>

#ifdef _WIN32
  #include <fcntl.h>
  #include <io.h>
  #include <windows.h>
#else
  #include <signal.h>
  #include <sys/types.h>
  #include <sys/wait.h>
  #include <unistd.h>
#endif

namespace lilia::tools::texel {

namespace {

inline bool starts_with(const std::string& s, std::string_view pfx) {
  return s.rfind(std::string(pfx), 0) == 0;
}

inline int to_int(const std::string& s) {
  try { return std::stoi(s); } catch (...) { return 0; }
}

inline std::vector<std::string> split_ws(const std::string& s) {
  std::vector<std::string> v;
  std::istringstream is(s);
  std::string t;
  while (is >> t) v.push_back(std::move(t));
  return v;
}

inline std::string trim_crlf(std::string s) {
  while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
  return s;
}

}  // namespace

struct UciEngine::Impl {
  std::string exePath;
  Options opts;
  std::mt19937_64 rng;

#ifdef _WIN32
  PROCESS_INFORMATION pi{};
  HANDLE hInWrite{NULL};
  HANDLE hOutRead{NULL};
#else
  pid_t pid{-1};
  int in_w{-1};
  int out_r{-1};
#endif

  FILE* fin{nullptr};
  FILE* fout{nullptr};

  explicit Impl(std::string path, const Options& o, uint64_t seed)
      : exePath(std::move(path)), opts(o), rng(seed ? seed : std::random_device{}()) {}

  void spawn() {
#ifdef _WIN32
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE hOutWrite = NULL;
    if (!CreatePipe(&hOutRead, &hOutWrite, &sa, 0))
      throw std::runtime_error("CreatePipe(stdout) failed");
    if (!SetHandleInformation(hOutRead, HANDLE_FLAG_INHERIT, 0))
      throw std::runtime_error("SetHandleInformation(stdout) failed");

    HANDLE hInRead = NULL;
    if (!CreatePipe(&hInRead, &hInWrite, &sa, 0))
      throw std::runtime_error("CreatePipe(stdin) failed");
    if (!SetHandleInformation(hInWrite, HANDLE_FLAG_INHERIT, 0))
      throw std::runtime_error("SetHandleInformation(stdin) failed");

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdInput = hInRead;
    si.hStdOutput = hOutWrite;
    si.hStdError = hOutWrite;

    std::wstring app = std::filesystem::path(exePath).wstring();
    if (!CreateProcessW(app.c_str(), nullptr, nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr,
                        nullptr, &si, &pi))
      throw std::runtime_error("CreateProcessW failed to start UCI engine");

    CloseHandle(hOutWrite);
    CloseHandle(hInRead);

    const int fdIn = _open_osfhandle(reinterpret_cast<intptr_t>(hInWrite), _O_WRONLY | _O_BINARY);
    const int fdOut = _open_osfhandle(reinterpret_cast<intptr_t>(hOutRead), _O_RDONLY | _O_BINARY);
    if (fdIn == -1 || fdOut == -1) throw std::runtime_error("_open_osfhandle failed");

    fout = _fdopen(fdIn, "wb");
    fin = _fdopen(fdOut, "rb");
    if (!fin || !fout) throw std::runtime_error("_fdopen failed");

    setvbuf(fout, nullptr, _IONBF, 0);
#else
    int inpipe[2]{}, outpipe[2]{};
    if (pipe(inpipe) != 0 || pipe(outpipe) != 0) throw std::runtime_error("pipe() failed");

    pid = fork();
    if (pid == -1) throw std::runtime_error("fork() failed");

    if (pid == 0) {
      dup2(inpipe[0], STDIN_FILENO);
      dup2(outpipe[1], STDOUT_FILENO);
      dup2(outpipe[1], STDERR_FILENO);
      close(inpipe[0]); close(inpipe[1]);
      close(outpipe[0]); close(outpipe[1]);
      execl(exePath.c_str(), exePath.c_str(), (char*)nullptr);
      _exit(127);
    }

    close(inpipe[0]);
    close(outpipe[1]);
    in_w = inpipe[1];
    out_r = outpipe[0];

    fout = fdopen(in_w, "w");
    fin = fdopen(out_r, "r");
    if (!fin || !fout) throw std::runtime_error("fdopen failed");

    setvbuf(fout, nullptr, _IONBF, 0);
#endif
  }

  void sendln(const std::string& s) {
    if (!fout) throw std::runtime_error("UCI stdin closed");
    std::fputs(s.c_str(), fout);
    std::fputc('\n', fout);
    std::fflush(fout);
  }

  std::optional<std::string> readline() {
    if (!fin) return std::nullopt;
    char buf[8192];
    if (!std::fgets(buf, sizeof(buf), fin)) {
      if (std::feof(fin)) return std::nullopt;
      return std::nullopt;
    }
    return trim_crlf(std::string(buf));
  }

  void isready() {
    sendln("isready");
    for (;;) {
      auto l = readline();
      if (!l) throw std::runtime_error("UCI engine closed during isready");
      if (*l == "readyok") return;
      // ignore other lines
    }
  }

  void uci_handshake() {
    sendln("uci");
    for (;;) {
      auto l = readline();
      if (!l) throw std::runtime_error("UCI engine closed during uci handshake");
      if (*l == "uciok") break;
    }
    isready();
  }

  void apply_options() {
    sendln("setoption name Threads value " + std::to_string(std::max(1, opts.threads)));
    if (opts.skillLevel)
      sendln("setoption name Skill Level value " + std::to_string(*opts.skillLevel));
    if (opts.elo) {
      sendln("setoption name UCI_LimitStrength value true");
      sendln("setoption name UCI_Elo value " + std::to_string(*opts.elo));
    }
    if (opts.contempt)
      sendln("setoption name Contempt value " + std::to_string(*opts.contempt));
    sendln("setoption name MultiPV value " + std::to_string(std::max(1, opts.multipv)));
    isready();
  }

  void start() {
    if (exePath.empty()) throw std::runtime_error("UCI engine path is empty");
    spawn();
    uci_handshake();
    apply_options();
  }

  void terminate() noexcept {
    // best effort
    try {
      if (fout) { std::fputs("quit\n", fout); std::fflush(fout); }
    } catch (...) {}

#ifdef _WIN32
    if (pi.hProcess) {
      DWORD code = STILL_ACTIVE;
      GetExitCodeProcess(pi.hProcess, &code);
      if (code == STILL_ACTIVE) {
        WaitForSingleObject(pi.hProcess, 750);
        GetExitCodeProcess(pi.hProcess, &code);
      }
      if (code == STILL_ACTIVE) {
        TerminateProcess(pi.hProcess, 1);
        WaitForSingleObject(pi.hProcess, 250);
      }
      CloseHandle(pi.hThread);
      CloseHandle(pi.hProcess);
      pi.hThread = pi.hProcess = NULL;
    }
#else
    if (pid > 0) {
      // Wait briefly, then SIGTERM, then SIGKILL.
      for (int i = 0; i < 10; ++i) {
        int status = 0;
        pid_t r = waitpid(pid, &status, WNOHANG);
        if (r == pid) { pid = -1; break; }
        usleep(50 * 1000);
      }
      if (pid > 0) {
        kill(pid, SIGTERM);
        for (int i = 0; i < 10; ++i) {
          int status = 0;
          pid_t r = waitpid(pid, &status, WNOHANG);
          if (r == pid) { pid = -1; break; }
          usleep(50 * 1000);
        }
      }
      if (pid > 0) {
        kill(pid, SIGKILL);
        int status = 0;
        waitpid(pid, &status, 0);
        pid = -1;
      }
    }
#endif

    if (fin) { std::fclose(fin); fin = nullptr; }
    if (fout) { std::fclose(fout); fout = nullptr; }
  }

  void new_game() {
    sendln("ucinewgame");
    isready();
  }

  std::string pick_move_from_startpos(const std::vector<std::string>& moves) {
    {
      std::ostringstream os;
      os << "position startpos";
      if (!moves.empty()) {
        os << " moves";
        for (const auto& m : moves) os << ' ' << m;
      }
      sendln(os.str());
    }

    std::string goCmd;
    if (opts.movetimeMs > 0) {
      int mt = opts.movetimeMs;
      if (opts.movetimeJitterMs > 0) {
        std::uniform_int_distribution<int> dist(-opts.movetimeJitterMs, opts.movetimeJitterMs);
        mt = std::max(5, mt + dist(rng));
      }
      goCmd = "go movetime " + std::to_string(mt);
    } else if (opts.depth > 0) {
      goCmd = "go depth " + std::to_string(opts.depth);
    } else {
      goCmd = "go movetime 1000";
    }
    sendln(goCmd);

    struct Cand { std::string move; double scoreCp{0}; int multipv{1}; };
    std::vector<Cand> cands;
    int bestDepth = -1;

    for (;;) {
      auto optLine = readline();
      if (!optLine) throw std::runtime_error("UCI engine closed during search");
      const std::string& line = *optLine;
      if (line.empty()) continue;

      if (starts_with(line, "info ")) {
        auto tok = split_ws(line);
        int depth = -1, mpv = 1;
        bool haveScore = false, isMate = false;
        int scoreCp = 0, matePly = 0;
        std::string firstMove;

        for (size_t i = 0; i + 1 < tok.size(); ++i) {
          if (tok[i] == "depth") depth = to_int(tok[i + 1]);
          else if (tok[i] == "multipv") mpv = std::max(1, to_int(tok[i + 1]));
          else if (tok[i] == "score" && i + 2 < tok.size()) {
            if (tok[i + 1] == "cp") { haveScore = true; scoreCp = to_int(tok[i + 2]); }
            else if (tok[i + 1] == "mate") { haveScore = true; isMate = true; matePly = to_int(tok[i + 2]); }
          } else if (tok[i] == "pv" && i + 1 < tok.size()) {
            firstMove = tok[i + 1];
            break;
          }
        }

        if (depth >= 0 && haveScore && !firstMove.empty()) {
          if (depth > bestDepth) { bestDepth = depth; cands.clear(); }
          if (depth == bestDepth) {
            const double cp = isMate ? (matePly >= 0 ? 30000.0 : -30000.0) : double(scoreCp);
            cands.push_back(Cand{firstMove, cp, mpv});
          }
        }
        continue;
      }

      if (starts_with(line, "bestmove ")) {
        std::istringstream is(line);
        std::string kw, best;
        is >> kw >> best;
        if (best.empty()) best = "(none)";

        if (cands.empty() || opts.multipv <= 1) return best;

        // Unique by move; keep best depth candidates.
        std::sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b) {
          if (a.multipv != b.multipv) return a.multipv < b.multipv;
          if (a.scoreCp != b.scoreCp) return a.scoreCp > b.scoreCp;
          return a.move < b.move;
        });
        cands.erase(std::unique(cands.begin(), cands.end(),
                                [](const Cand& a, const Cand& b) { return a.move == b.move; }),
                    cands.end());

        const double T = std::max(1e-3, opts.tempCp);
        double maxCp = -1e300;
        for (const auto& c : cands) maxCp = std::max(maxCp, c.scoreCp);

        double sum = 0.0;
        std::vector<double> w;
        w.reserve(cands.size());
        for (const auto& c : cands) {
          const double wi = std::exp((c.scoreCp - maxCp) / T);
          w.push_back(wi);
          sum += wi;
        }
        if (sum <= 0.0) return best;

        std::uniform_real_distribution<double> U(0.0, sum);
        double r = U(rng), acc = 0.0;
        for (size_t i = 0; i < cands.size(); ++i) {
          acc += w[i];
          if (r <= acc) return cands[i].move;
        }
        return cands.back().move;
      }
    }
  }
};

UciEngine::UciEngine(const std::string& exePath, const Options& opts, uint64_t seed) {
  auto* p = new Impl(exePath, opts, seed);
  p->start();
  impl_ = p;
}

UciEngine::~UciEngine() {
  if (impl_) {
    impl_->terminate();
    delete impl_;
    impl_ = nullptr;
  }
}

void UciEngine::new_game() { impl_->new_game(); }

std::string UciEngine::pick_move_from_startpos(const std::vector<std::string>& moves) {
  return impl_->pick_move_from_startpos(moves);
}

}  // namespace lilia::tools::texel
