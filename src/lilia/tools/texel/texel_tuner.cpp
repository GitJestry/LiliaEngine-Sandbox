#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <mutex>
#include <numeric>
#include <optional>
#include <random>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#else
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#include "lilia/constants.hpp"
#include "lilia/engine/engine.hpp"
#include "lilia/engine/eval.hpp"
#include "lilia/engine/eval_shared.hpp"
#include "lilia/model/chess_game.hpp"
#include "lilia/model/core/model_types.hpp"

namespace fs = std::filesystem;
using namespace std::string_literals;

#define M_PI 3.14159265358979323846

namespace lilia::tools::texel {

// ------------------------ Progress meter ------------------------
struct ProgressMeter {
  std::string label;
  std::size_t total = 0;
  std::atomic<std::size_t> current{0};
  int intervalMs = 750;

  std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
  std::chrono::steady_clock::time_point last = start;
  std::atomic<bool> finished{false};
  bool threadSafe = false;
  mutable std::mutex mutex_;
  std::string status;

  ProgressMeter(std::string label_, std::size_t total_, int intervalMs_ = 750,
                bool threadSafe_ = false)
      : label(std::move(label_)), total(total_), intervalMs(intervalMs_), threadSafe(threadSafe_) {}

  static std::string fmt_hms(std::chrono::seconds s) {
    long t = s.count();
    int h = static_cast<int>(t / 3600);
    int m = static_cast<int>((t % 3600) / 60);
    int sec = static_cast<int>(t % 60);
    std::ostringstream os;
    if (h > 0)
      os << h << ":" << std::setw(2) << std::setfill('0') << m << ":" << std::setw(2) << sec;
    else
      os << m << ":" << std::setw(2) << std::setfill('0') << sec;
    return os.str();
  }

  void add(std::size_t delta = 1) {
    if (finished.load(std::memory_order_acquire)) return;
    if (threadSafe) {
      current.fetch_add(delta, std::memory_order_relaxed);
    } else {
      auto cur = current.load(std::memory_order_relaxed);
      cur = std::min(cur + delta, total);
      current.store(cur, std::memory_order_relaxed);
    }
    tick();
  }

  void update(std::size_t newCurrent) {
    if (finished.load(std::memory_order_acquire)) return;
    current.store(std::min(newCurrent, total), std::memory_order_relaxed);
    tick();
  }

  void tick(bool force = false) {
    if (!force && finished.load(std::memory_order_acquire)) return;

    std::unique_lock<std::mutex> lock(mutex_, std::defer_lock);
    if (threadSafe) lock.lock();

    auto now = std::chrono::steady_clock::now();
    std::size_t cur = current.load(std::memory_order_relaxed);
    if (cur > total) cur = total;

    auto since = std::chrono::duration_cast<std::chrono::milliseconds>(now - last).count();
    bool timeToPrint = force || since >= intervalMs || cur == total;
    if (!timeToPrint) return;
    last = now;

    double pct = total ? (100.0 * double(cur) / double(total)) : 0.0;
    double elapsedSec = std::chrono::duration<double>(now - start).count();
    double rate = elapsedSec > 0.0 ? cur / elapsedSec : 0.0;
    double remainSec = (rate > 0.0 && total >= cur) ? (total - cur) / rate : 0.0;

    auto eta = std::chrono::seconds((long long)(remainSec + 0.5));
    auto elapsed = std::chrono::seconds((long long)(elapsedSec + 0.5));

    std::ostringstream line;
    line << "\r" << label << " " << std::fixed << std::setprecision(1) << pct << "% "
         << "(" << cur << "/" << total << ")  "
         << "elapsed " << fmt_hms(elapsed) << "  ETA ~" << fmt_hms(eta);
    if (rate > 0.0) {
      line << "  rate " << std::setprecision(1) << rate << "/s";
    }
    if (!status.empty()) {
      line << "  " << status;
    }
    std::cout << line.str() << std::flush;
  }

  void finish() {
    if (finished.exchange(true, std::memory_order_acq_rel)) return;
    current.store(total, std::memory_order_relaxed);
    tick(true);
    if (threadSafe) {
      std::lock_guard<std::mutex> lk(mutex_);
      std::cout << "\n";
    } else {
      std::cout << "\n";
    }
  }

  void set_status(std::string newStatus, bool flush = false) {
    if (finished.load(std::memory_order_acquire)) return;
    {
      std::unique_lock<std::mutex> lock(mutex_, std::defer_lock);
      if (threadSafe) lock.lock();
      status = std::move(newStatus);
    }
    if (flush) tick(true);
  }
};

// ------------------------ Worker pool (fixed threads) ------------------------
class WorkerPool {
 public:
  explicit WorkerPool(int n) : n_(std::max(1, n)) {
    for (int i = 0; i < n_; ++i) threads_.emplace_back([this, i] { worker_loop(i); });
  }
  ~WorkerPool() {
    {
      std::lock_guard<std::mutex> lk(m_);
      stop_ = true;
      ++ticket_;
    }
    cv_.notify_all();
    for (auto& t : threads_) t.join();
  }

  void run(const std::function<void(int)>& f) {
    std::unique_lock<std::mutex> lk(m_);
    task_ = f;
    done_ = 0;
    ++ticket_;
    auto my_ticket = ticket_;
    lk.unlock();
    cv_.notify_all();

    std::unique_lock<std::mutex> lk2(m_);
    done_cv_.wait(lk2, [&] { return done_ticket_ == my_ticket && done_ == n_; });
  }

  int size() const { return n_; }

 private:
  int n_;
  std::vector<std::thread> threads_;
  std::mutex m_;
  std::condition_variable cv_, done_cv_;
  std::function<void(int)> task_;
  uint64_t ticket_ = 0;
  uint64_t done_ticket_ = 0;
  int done_ = 0;
  bool stop_ = false;

  void worker_loop(int id) {
    uint64_t seen_ticket = 0;
    for (;;) {
      std::function<void(int)> local_task;
      uint64_t my_ticket = 0;

      {
        std::unique_lock<std::mutex> lk(m_);
        cv_.wait(lk, [&] { return stop_ || ticket_ != seen_ticket; });
        if (stop_) return;
        seen_ticket = ticket_;
        local_task = task_;
        my_ticket = ticket_;
      }

      local_task(id);

      {
        std::lock_guard<std::mutex> lk(m_);
        if (done_ticket_ != my_ticket) {
          done_ticket_ = my_ticket;
          done_ = 0;
        }
        if (++done_ == n_) done_cv_.notify_one();
      }
    }
  }
};

// ------------------------ Defaults & CLI ------------------------
struct DefaultPaths {
  fs::path dataFile;
  fs::path weightsFile;
  std::optional<fs::path> stockfish;
};

struct Options {
  bool generateData = false;
  bool tune = false;

  std::string stockfishPath;
  int games = 8;
  int depth = 12;
  int maxPlies = 160;
  int sampleSkip = 6;
  int sampleStride = 4;

  std::string dataFile;
  int iterations = 200;
  double learningRate = 0.0005;
  double logisticScale = 256.0;  // init scale (may be learned)
  double l2 = 0.0;               // legacy L2 (adds to grad)

  std::optional<std::string> weightsOutput;
  std::optional<int> sampleLimit;
  bool shuffleBeforeTraining = true;
  int progressIntervalMs = 750;

  // Engine / self-play
  int threads = std::max(1, int(std::thread::hardware_concurrency()));
  int multipv = 4;
  double tempCp = 80.0;
  int movetimeMs = 0;
  int movetimeJitterMs = 0;
  std::optional<int> skillLevel;
  std::optional<int> elo;
  std::optional<int> contempt;

  // Performance / training
  int genWorkers = std::max(1, int(std::thread::hardware_concurrency()));
  int trainWorkers = std::max(1, int(std::thread::hardware_concurrency()));
  bool useAdam = true;  // Adam or SGD
  // Adam params
  double adamBeta1 = 0.9;
  double adamBeta2 = 0.999;
  double adamEps = 1e-8;
  // AdamW (decoupled weight decay)
  double weightDecay = 0.0;  // 0 disables

  int logEvery = 0;   // 0 => auto
  uint64_t seed = 0;  // 0 => nondeterministic
  int batchSize = 0;  // 0 => full-batch
  double valSplit = 0.0;
  int evalEvery = 0;
  int earlyStopPatience = 0;
  double earlyStopDelta = 0.0;
  double gradClip = 0.0;

  // LR schedule
  int lrWarmup = 0;  // steps of linear warmup
  int lrCosine = 0;  // if >0, cosine decay over this many steps

  // Prepared cache
  std::optional<std::string> preparedCache;
  bool loadPreparedIfExists = true;
  bool savePrepared = true;

  // Warm start
  std::optional<std::string> initWeightsPath;

  // Relinearization
  int relinEvery = 0;      // iterations; 0=off
  double relinFrac = 0.0;  // 0..1 (1.0 = full)
  int relinDelta = 1;      // finite-diff step

  // Auto-scale
  bool autoScale = false;

  // New: learnable extras
  bool learnBias = true;    // add bias parameter b
  bool learnScale = false;  // optimize scale via log-param

  // Logging
  std::optional<std::string> logCsv;
};

struct RawSample {
  std::string fen;
  double result = 0.5;  // from side-to-move POV
};

struct PreparedSample {
  std::string fen;  // needed for relinearization
  float result = 0.5f;
  float baseEval = 0.0f;
  float weight = 1.0f;           // per-sample weight
  std::vector<float> gradients;  // dEval/dw_j at linearization point
};

// --- Utility to find Stockfish near exe / project ---
std::optional<fs::path> find_stockfish_in_dir(const fs::path& dir) {
  if (dir.empty()) return std::nullopt;
  std::error_code ec;
  if (!fs::exists(dir, ec)) return std::nullopt;

  const std::array<const char*, 2> names = {"stockfish", "stockfish.exe"};
  for (const auto* name : names) {
    const fs::path candidate = dir / name;
    std::error_code e2;
    if (fs::exists(candidate, e2) && fs::is_regular_file(candidate, e2)) return candidate;
  }
  for (fs::directory_iterator it{dir, ec}; !ec && it != fs::directory_iterator{}; ++it) {
    std::error_code rf, sl;
    bool isFile = it->is_regular_file(rf) || it->is_symlink(sl);
    if (!isFile) continue;
    if (it->path().stem().string().rfind("stockfish", 0) == 0) return it->path();
  }
  return std::nullopt;
}

std::string fen_key(std::string_view fen) {
  std::array<std::string, 6> tok{};
  size_t i = 0, start = 0;
  for (; i < 6; ++i) {
    auto sp = fen.find(' ', start);
    if (sp == std::string_view::npos) {
      tok[i] = std::string(fen.substr(start));
      ++i;
      break;
    }
    tok[i] = std::string(fen.substr(start, sp - start));
    start = sp + 1;
  }
  std::ostringstream os;
  os << tok[0] << ' ' << tok[1] << ' ' << tok[2] << ' ' << tok[3];
  return os.str();
}

fs::path locate_project_root(fs::path start) {
  std::error_code ec;
  if (!start.is_absolute()) start = fs::absolute(start, ec), void(ec);
  while (true) {
    if (fs::exists(start / "CMakeLists.txt")) return start;
    const auto parent = start.parent_path();
    if (parent.empty() || parent == start) return fs::current_path();
    start = parent;
  }
}

fs::path default_user_texel_dir() {
#ifdef _WIN32
  if (const char* appData = std::getenv("APPDATA"); appData && *appData)
    return fs::path(appData) / "Lilia" / "texel";
  if (const char* userProfile = std::getenv("USERPROFILE"); userProfile && *userProfile)
    return fs::path(userProfile) / "AppData" / "Roaming" / "Lilia" / "texel";
#else
  if (const char* xdg = std::getenv("XDG_DATA_HOME"); xdg && *xdg)
    return fs::path(xdg) / "lilia" / "texel";
  if (const char* home = std::getenv("HOME"); home && *home)
    return fs::path(home) / ".local" / "share" / "lilia" / "texel";
#endif
  return fs::current_path() / "texel_data";
}

DefaultPaths compute_default_paths(const char* argv0) {
  fs::path exePath;
#ifdef _WIN32
  wchar_t buffer[MAX_PATH];
  DWORD len = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
  if (len > 0) exePath.assign(buffer, buffer + len);
  if (exePath.empty() && argv0 && *argv0) exePath = fs::path(argv0);
#else
  std::error_code ec;
  exePath = fs::read_symlink("/proc/self/exe", ec);
  if (ec && argv0 && *argv0) exePath = fs::absolute(fs::path(argv0), ec);
  if (ec) exePath.clear();
#endif
  if (exePath.empty()) exePath = fs::current_path();
  fs::path exeDir = exePath.has_filename() ? exePath.parent_path() : exePath;
  if (exeDir.empty()) exeDir = fs::current_path();

  const fs::path projectRoot = locate_project_root(exeDir);
  const bool hasProjectRoot = fs::exists(projectRoot / "CMakeLists.txt");

  const fs::path texelDir = hasProjectRoot ? projectRoot / "texel_data"
                                           : default_user_texel_dir();
  DefaultPaths defaults;
  defaults.dataFile = texelDir / "texel_dataset.txt";
  defaults.weightsFile = texelDir / "texel_weights.txt";
  defaults.stockfish = find_stockfish_in_dir(exeDir);
  if (!defaults.stockfish)
    defaults.stockfish = find_stockfish_in_dir(projectRoot / "tools" / "texel");
  return defaults;
}

[[noreturn]] void usage_and_exit(const DefaultPaths& d) {
  std::cerr << "Usage: texel_tuner [--generate-data] [--tune] [options]\n"
               "Options:\n"
               "  --stockfish <path>        Path to Stockfish binary (default autodetect)\n"
               "  --games <N>               Self-play games (default 8)\n"
               "  --depth <D>               Stockfish depth (default 12)\n"
               "  --movetime <ms>           Use movetime instead of depth (default off)\n"
               "  --jitter <ms>             +/- movetime jitter (default 0)\n"
               "  --threads <N>             Stockfish Threads (default hw threads)\n"
               "  --multipv <N>             MultiPV for sampling (default 4)\n"
               "  --temp <cp>               Softmax temperature in centipawns (default 80)\n"
               "  --skill <0..20>           Stockfish Skill Level (optional)\n"
               "  --elo <E>                 UCI_LimitStrength with UCI_Elo=E (optional)\n"
               "  --contempt <C>            Engine Contempt (e.g. 20)\n"
               "  --max-plies <N>           Max plies per game (default 160)\n"
               "  --sample-skip <N>         Skip first N plies before sampling (default 6)\n"
               "  --sample-stride <N>       Sample every N plies thereafter (default 4)\n"
               "  --data <file>             Dataset path (default "
            << d.dataFile.string()
            << ")\n"
               "  --iterations <N>          Training iterations (default 200)\n"
               "  --learning-rate <v>       Learning rate (default 5e-4)\n"
               "  --scale <v>               Logistic scale in centipawns (default 256)\n"
               "  --l2 <v>                  L2 regularization (legacy, default 0)\n"
               "  --no-shuffle              Do not shuffle dataset before training\n"
               "  --weights-output <file>   Write tuned weights (default "
            << d.weightsFile.string()
            << ")\n"
               "  --sample-limit <N>        Limit training samples\n"
               "  --progress-interval <ms>  Progress update interval (default 750)\n"
               "\nPerformance & training:\n"
               "  --gen-workers <N>         Parallel self-play workers (default = hw threads)\n"
               "  --train-workers <N>       Training workers (default = hw threads)\n"
               "  --adam 0|1                Use Adam optimizer (default 1)\n"
               "  --adam-b1 <v>             Adam beta1 (default 0.9)\n"
               "  --adam-b2 <v>             Adam beta2 (default 0.999)\n"
               "  --adam-eps <v>            Adam epsilon (default 1e-8)\n"
               "  --weight-decay <v>        AdamW decoupled weight decay (default 0)\n"
               "  --log-every <N>           Log every N iterations (auto if 0)\n"
               "  --seed <u64>              RNG seed (0 => nondeterministic)\n"
               "  --batch-size <N>          Minibatch size (0 => full-batch)\n"
               "  --val-split <r>           Validation split ratio, 0..0.5 (default 0)\n"
               "  --eval-every <N>          Validate every N steps (default logEvery)\n"
               "  --early-stop <N>          Early-stop patience (0 => off)\n"
               "  --early-delta <v>         Min val-loss improvement to reset patience\n"
               "  --grad-clip <v>           L2 gradient clipping (0 => off)\n"
               "  --lr-warmup <N>           Linear warmup steps (default 0)\n"
               "  --lr-cosine <N>           Cosine decay horizon in steps (default 0)\n"
               "  --log-csv <file>          Write training log CSV to file\n"
               "\nInit & linearization:\n"
               "  --init-weights <file>     Warm-start from weights file\n"
               "  --relin-every <N>         Relinearize every N iters (0 => off)\n"
               "  --relin-frac <r>          Fraction 0..1 of samples to relinearize\n"
               "  --relin-delta <D>         Finite-diff step for (re)linearization (default 1)\n"
               "  --prepared-cache <file>   Path to prepared cache (v1/v2/v3)\n"
               "  --no-load-prepared        Do not attempt to load prepared cache\n"
               "  --no-save-prepared        Do not save prepared cache\n"
               "\nExtras:\n"
               "  --auto-scale              One-shot auto-tune of logistic scale on startup\n"
               "  --learn-scale             Learn logistic scale jointly (log-param)\n"
               "  --no-bias                 Disable bias parameter (default on)\n";
  std::exit(1);
}

Options parse_args(int argc, char** argv, const DefaultPaths& defaults) {
  Options o;
  o.dataFile = defaults.dataFile.string();
  if (defaults.stockfish) o.stockfishPath = defaults.stockfish->string();
  if (!defaults.weightsFile.empty()) o.weightsOutput = defaults.weightsFile.string();

  auto require_value = [&](int& i, const char* name, int argc, char** argv) -> std::string {
    if (i + 1 >= argc) {
      std::cerr << "Missing value for " << name << "\n";
      usage_and_exit(defaults);
    }
    return argv[++i];
  };

  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--generate-data")
      o.generateData = true;
    else if (arg == "--tune")
      o.tune = true;
    else if (arg == "--stockfish")
      o.stockfishPath = require_value(i, "--stockfish", argc, argv);
    else if (arg == "--games")
      o.games = std::stoi(require_value(i, "--games", argc, argv));
    else if (arg == "--depth")
      o.depth = std::stoi(require_value(i, "--depth", argc, argv));
    else if (arg == "--movetime")
      o.movetimeMs = std::stoi(require_value(i, "--movetime", argc, argv));
    else if (arg == "--jitter")
      o.movetimeJitterMs = std::stoi(require_value(i, "--jitter", argc, argv));
    else if (arg == "--threads")
      o.threads = std::max(1, std::stoi(require_value(i, "--threads", argc, argv)));
    else if (arg == "--multipv")
      o.multipv = std::max(1, std::stoi(require_value(i, "--multipv", argc, argv)));
    else if (arg == "--temp")
      o.tempCp = std::stod(require_value(i, "--temp", argc, argv));
    else if (arg == "--skill")
      o.skillLevel = std::stoi(require_value(i, "--skill", argc, argv));
    else if (arg == "--elo")
      o.elo = std::stoi(require_value(i, "--elo", argc, argv));
    else if (arg == "--contempt")
      o.contempt = std::stoi(require_value(i, "--contempt", argc, argv));
    else if (arg == "--max-plies")
      o.maxPlies = std::stoi(require_value(i, "--max-plies", argc, argv));
    else if (arg == "--sample-skip")
      o.sampleSkip = std::stoi(require_value(i, "--sample-skip", argc, argv));
    else if (arg == "--sample-stride")
      o.sampleStride = std::stoi(require_value(i, "--sample-stride", argc, argv));
    else if (arg == "--data")
      o.dataFile = require_value(i, "--data", argc, argv);
    else if (arg == "--iterations")
      o.iterations = std::stoi(require_value(i, "--iterations", argc, argv));
    else if (arg == "--learning-rate")
      o.learningRate = std::stod(require_value(i, "--learning-rate", argc, argv));
    else if (arg == "--scale")
      o.logisticScale = std::stod(require_value(i, "--scale", argc, argv));
    else if (arg == "--l2")
      o.l2 = std::stod(require_value(i, "--l2", argc, argv));
    else if (arg == "--no-shuffle")
      o.shuffleBeforeTraining = false;
    else if (arg == "--weights-output")
      o.weightsOutput = require_value(i, "--weights-output", argc, argv);
    else if (arg == "--sample-limit")
      o.sampleLimit = std::stoi(require_value(i, "--sample-limit", argc, argv));
    else if (arg == "--progress-interval")
      o.progressIntervalMs = std::stoi(require_value(i, "--progress-interval", argc, argv));
    else if (arg == "--gen-workers")
      o.genWorkers = std::max(1, std::stoi(require_value(i, "--gen-workers", argc, argv)));
    else if (arg == "--train-workers")
      o.trainWorkers = std::max(1, std::stoi(require_value(i, "--train-workers", argc, argv)));
    else if (arg == "--adam")
      o.useAdam = std::stoi(require_value(i, "--adam", argc, argv)) != 0;
    else if (arg == "--adam-b1")
      o.adamBeta1 = std::stod(require_value(i, "--adam-b1", argc, argv));
    else if (arg == "--adam-b2")
      o.adamBeta2 = std::stod(require_value(i, "--adam-b2", argc, argv));
    else if (arg == "--adam-eps")
      o.adamEps = std::stod(require_value(i, "--adam-eps", argc, argv));
    else if (arg == "--weight-decay")
      o.weightDecay = std::stod(require_value(i, "--weight-decay", argc, argv));
    else if (arg == "--log-every")
      o.logEvery = std::stoi(require_value(i, "--log-every", argc, argv));
    else if (arg == "--seed")
      o.seed = static_cast<uint64_t>(std::stoull(require_value(i, "--seed", argc, argv)));
    else if (arg == "--batch-size")
      o.batchSize = std::stoi(require_value(i, "--batch-size", argc, argv));
    else if (arg == "--val-split")
      o.valSplit = std::stod(require_value(i, "--val-split", argc, argv));
    else if (arg == "--eval-every")
      o.evalEvery = std::stoi(require_value(i, "--eval-every", argc, argv));
    else if (arg == "--early-stop")
      o.earlyStopPatience = std::stoi(require_value(i, "--early-stop", argc, argv));
    else if (arg == "--early-delta")
      o.earlyStopDelta = std::stod(require_value(i, "--early-delta", argc, argv));
    else if (arg == "--grad-clip")
      o.gradClip = std::stod(require_value(i, "--grad-clip", argc, argv));
    else if (arg == "--prepared-cache")
      o.preparedCache = require_value(i, "--prepared-cache", argc, argv);
    else if (arg == "--no-load-prepared")
      o.loadPreparedIfExists = false;
    else if (arg == "--no-save-prepared")
      o.savePrepared = false;
    else if (arg == "--init-weights")
      o.initWeightsPath = require_value(i, "--init-weights", argc, argv);
    else if (arg == "--relin-every")
      o.relinEvery = std::stoi(require_value(i, "--relin-every", argc, argv));
    else if (arg == "--relin-frac")
      o.relinFrac = std::stod(require_value(i, "--relin-frac", argc, argv));
    else if (arg == "--relin-delta")
      o.relinDelta = std::stoi(require_value(i, "--relin-delta", argc, argv));
    else if (arg == "--auto-scale")
      o.autoScale = true;
    else if (arg == "--learn-scale")
      o.learnScale = true;
    else if (arg == "--no-bias")
      o.learnBias = false;
    else if (arg == "--lr-warmup")
      o.lrWarmup = std::stoi(require_value(i, "--lr-warmup", argc, argv));
    else if (arg == "--lr-cosine")
      o.lrCosine = std::stoi(require_value(i, "--lr-cosine", argc, argv));
    else if (arg == "--log-csv")
      o.logCsv = require_value(i, "--log-csv", argc, argv);
    else if (arg == "--help" || arg == "-h")
      usage_and_exit(defaults);
    else {
      std::cerr << "Unknown option: " << arg << "\n";
      usage_and_exit(defaults);
    }
  }

  if (!o.generateData && !o.tune) {
    std::cerr << "Nothing to do: specify --generate-data and/or --tune.\n";
    usage_and_exit(defaults);
  }
  if (o.valSplit < 0.0) o.valSplit = 0.0;
  if (o.valSplit > 0.5) o.valSplit = 0.5;
  if (o.batchSize < 0) o.batchSize = 0;
  if (o.evalEvery < 0) o.evalEvery = 0;
  if (o.earlyStopPatience < 0) o.earlyStopPatience = 0;
  if (o.gradClip < 0.0) o.gradClip = 0.0;
  if (o.relinEvery < 0) o.relinEvery = 0;
  if (o.relinFrac < 0.0) o.relinFrac = 0.0;
  if (o.relinFrac > 1.0) o.relinFrac = 1.0;
  if (o.relinDelta <= 0) o.relinDelta = 1;
  if (o.lrWarmup < 0) o.lrWarmup = 0;
  if (o.lrCosine < 0) o.lrCosine = 0;
  if (o.weightDecay < 0.0) o.weightDecay = 0.0;
  return o;
}

// ------------------------ Helpers ------------------------
core::Color flip_color(core::Color c) {
  return c == core::Color::White ? core::Color::Black : core::Color::White;
}

double result_from_pov(core::GameResult res, core::Color winner, core::Color pov) {
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

// ------------------------ Read weights (warm start) ------------------------
std::optional<std::vector<int>> read_weights_file(
    const std::string& path, const std::span<const engine::EvalParamEntry>& entries) {
  std::ifstream in(path);
  if (!in) return std::nullopt;
  std::unordered_map<std::string, int> kv;
  std::string line;
  while (std::getline(in, line)) {
    if (line.empty() || line[0] == '#') continue;
    auto hash = line.find('#');
    if (hash != std::string::npos) line = line.substr(0, hash);
    auto eq = line.find('=');
    if (eq == std::string::npos) continue;
    std::string k = line.substr(0, eq), v = line.substr(eq + 1);
    auto trim = [](std::string& s) {
      size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
      if (a == std::string::npos)
        s.clear();
      else
        s = s.substr(a, b - a + 1);
    };
    trim(k);
    trim(v);
    try {
      kv[k] = std::stoi(v);
    } catch (...) {
    }
  }
  if (kv.empty()) return std::nullopt;
  std::vector<int> w(entries.size(), 0);
  for (size_t i = 0; i < entries.size(); ++i) {
    auto it = kv.find(entries[i].name);
    if (it == kv.end()) return std::nullopt;
    w[i] = it->second;
  }
  return w;
}

// ------------------------ Persistent UCI Engine ------------------------
class UciEngine {
 public:
  explicit UciEngine(const std::string& exe, const Options& opts, uint64_t seed = 0)
      : exePath_(exe), opts_(opts), rng_(seed ? seed : std::random_device{}()) {
    if (exePath_.empty()) throw std::runtime_error("UCI engine path is empty");
    spawn();
    uci_handshake();
    apply_options();
  }
  ~UciEngine() { terminate(); }

  void ucinewgame() {
    sendln("ucinewgame");
    isready();
  }

  // Choose move for "position startpos [moves ...]" using MultiPV sampling
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
    if (opts_.movetimeMs > 0) {
      int mt = opts_.movetimeMs;
      if (opts_.movetimeJitterMs > 0) {
        std::uniform_int_distribution<int> dist(-opts_.movetimeJitterMs, opts_.movetimeJitterMs);
        mt = std::max(5, mt + dist(rng_));
      }
      goCmd = "go movetime " + std::to_string(mt);
    } else if (opts_.depth > 0)
      goCmd = "go depth " + std::to_string(opts_.depth);
    else
      goCmd = "go movetime 1000";
    sendln(goCmd);

    struct Cand {
      std::string move;
      double scoreCp = 0.0;
      int multipv = 1;
    };
    std::vector<Cand> cands;
    int bestDepth = -1;

    for (;;) {
      auto opt = readline_blocking();
      if (!opt) throw std::runtime_error("UCI engine closed");
      const std::string& line = *opt;
      if (line.empty()) continue;

      if (starts_with(line, "info ")) {
        auto tok = tokenize(line);
        int depth = -1, mpv = 1;
        bool haveScore = false, isMate = false;
        int scoreCp = 0, matePly = 0;
        std::string firstMove;
        for (size_t i = 0; i + 1 < tok.size(); ++i) {
          if (tok[i] == "depth")
            depth = to_int(tok[i + 1]);
          else if (tok[i] == "multipv")
            mpv = std::max(1, to_int(tok[i + 1]));
          else if (tok[i] == "score" && i + 2 < tok.size()) {
            if (tok[i + 1] == "cp") {
              haveScore = true;
              scoreCp = to_int(tok[i + 2]);
            } else if (tok[i + 1] == "mate") {
              haveScore = true;
              isMate = true;
              matePly = to_int(tok[i + 2]);
            }
          } else if (tok[i] == "pv" && i + 1 < tok.size()) {
            firstMove = tok[i + 1];
            break;
          }
        }
        if (depth >= 0 && haveScore && !firstMove.empty()) {
          if (depth > bestDepth) {
            bestDepth = depth;
            cands.clear();
          }
          if (depth == bestDepth) {
            double cp = isMate ? (matePly >= 0 ? 30000.0 : -30000.0) : double(scoreCp);
            cands.push_back(Cand{firstMove, cp, mpv});
          }
        }
        continue;
      }

      if (starts_with(line, "bestmove ")) {
        std::string best = word_after(line, "bestmove");
        if (cands.empty() || opts_.multipv <= 1) return best.empty() ? "(none)" : best;

        std::sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b) {
          if (a.multipv != b.multipv) return a.multipv < b.multipv;
          if (a.scoreCp != b.scoreCp) return a.scoreCp > b.scoreCp;
          return a.move < b.move;
        });
        cands.erase(std::unique(cands.begin(), cands.end(),
                                [](const Cand& a, const Cand& b) { return a.move == b.move; }),
                    cands.end());

        const double T = std::max(1e-3, opts_.tempCp);
        double maxCp = -1e300;
        for (const auto& c : cands) maxCp = std::max(maxCp, c.scoreCp);
        std::vector<double> w;
        w.reserve(cands.size());
        double sum = 0.0;
        for (const auto& c : cands) {
          double wi = std::exp((c.scoreCp - maxCp) / T);
          w.push_back(wi);
          sum += wi;
        }
        if (sum <= 0.0) return best.empty() ? "(none)" : best;

        std::uniform_real_distribution<double> U(0.0, sum);
        double r = U(rng_), acc = 0.0;
        for (size_t i = 0; i < cands.size(); ++i) {
          acc += w[i];
          if (r <= acc) return cands[i].move;
        }
        return cands.back().move;
      }
    }
  }

 private:
#ifdef _WIN32
  PROCESS_INFORMATION pi_{};                // child
  HANDLE hInWrite_{NULL}, hOutRead_{NULL};  // our handles
#else
  pid_t pid_ = -1;
  int in_w_ = -1, out_r_ = -1;
#endif
  FILE* fin_ = nullptr;   // read from engine stdout
  FILE* fout_ = nullptr;  // write to engine stdin
  std::string exePath_;
  Options opts_;
  std::mt19937_64 rng_;

  static bool starts_with(const std::string& s, const char* pfx) { return s.rfind(pfx, 0) == 0; }
  static int to_int(const std::string& s) {
    try {
      return std::stoi(s);
    } catch (...) {
      return 0;
    }
  }
  static std::vector<std::string> tokenize(const std::string& s) {
    std::vector<std::string> v;
    std::istringstream is(s);
    std::string t;
    while (is >> t) v.push_back(std::move(t));
    return v;
  }
  static std::string word_after(const std::string& s, const char* key) {
    std::istringstream is(s);
    std::string w;
    is >> w;
    if (w != key) return {};
    return (is >> w) ? w : std::string();
  }

  void sendln(const std::string& s) {
    if (!fout_) throw std::runtime_error("UCI engine stdin closed");
    std::fputs(s.c_str(), fout_);
    std::fputc('\n', fout_);
    std::fflush(fout_);
  }

  std::optional<std::string> readline_blocking() {
    std::string line;
    int ch;
    bool any = false;
    while ((ch = std::fgetc(fin_)) != EOF) {
      any = true;
      if (ch == '\r') continue;
      if (ch == '\n') break;
      line.push_back((char)ch);
    }
    if (!any && std::feof(fin_)) return std::nullopt;
    return line;
  }

  void isready() {
    sendln("isready");
    for (;;) {
      auto l = readline_blocking();
      if (!l) throw std::runtime_error("UCI engine closed");
      if (*l == "readyok") break;
    }
  }
  void uci_handshake() {
    sendln("uci");
    for (;;) {
      auto l = readline_blocking();
      if (!l) throw std::runtime_error("UCI engine closed");
      if (*l == "uciok") break;
    }
    isready();
  }
  void apply_options() {
    sendln("setoption name Threads value " + std::to_string(std::max(1, opts_.threads)));
    if (opts_.skillLevel)
      sendln("setoption name Skill Level value " + std::to_string(*opts_.skillLevel));
    if (opts_.elo) {
      sendln("setoption name UCI_LimitStrength value true");
      sendln("setoption name UCI_Elo value " + std::to_string(*opts_.elo));
    }
    if (opts_.contempt) sendln("setoption name Contempt value " + std::to_string(*opts_.contempt));
    sendln("setoption name MultiPV value " + std::to_string(std::max(1, opts_.multipv)));
    isready();
  }

  void spawn() {
#ifdef _WIN32
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    HANDLE hOutWrite = NULL, hInReadLocal = NULL;
    if (!CreatePipe(&hOutRead_, &hOutWrite, &sa, 0))
      throw std::runtime_error("CreatePipe stdout failed");
    if (!SetHandleInformation(hOutRead_, HANDLE_FLAG_INHERIT, 0))
      throw std::runtime_error("stdout SetHandleInformation failed");
    HANDLE hInRead = NULL;
    if (!CreatePipe(&hInRead, &hInWrite_, &sa, 0))
      throw std::runtime_error("CreatePipe stdin failed");
    if (!SetHandleInformation(hInWrite_, HANDLE_FLAG_INHERIT, 0))
      throw std::runtime_error("stdin SetHandleInformation failed");
    hInReadLocal = hInRead;
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdInput = hInReadLocal;
    si.hStdOutput = hOutWrite;
    si.hStdError = hOutWrite;
    std::wstring app = fs::path(exePath_).wstring();
    if (!CreateProcessW(app.c_str(), nullptr, nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr,
                        nullptr, &si, &pi_))
      throw std::runtime_error("CreateProcessW failed for Stockfish");
    CloseHandle(hOutWrite);
    CloseHandle(hInReadLocal);
    int fdIn = _open_osfhandle(reinterpret_cast<intptr_t>(hInWrite_), _O_WRONLY | _O_BINARY);
    int fdOut = _open_osfhandle(reinterpret_cast<intptr_t>(hOutRead_), _O_RDONLY | _O_BINARY);
    if (fdIn == -1 || fdOut == -1) throw std::runtime_error("_open_osfhandle failed");
    fout_ = _fdopen(fdIn, "wb");
    fin_ = _fdopen(fdOut, "rb");
    if (!fin_ || !fout_) throw std::runtime_error("_fdopen failed");
    setvbuf(fout_, nullptr, _IONBF, 0);
#else
    int inpipe[2]{}, outpipe[2]{};
    if (pipe(inpipe) != 0 || pipe(outpipe) != 0) throw std::runtime_error("pipe() failed");
    pid_ = fork();
    if (pid_ == -1) throw std::runtime_error("fork() failed");
    if (pid_ == 0) {
      dup2(inpipe[0], STDIN_FILENO);
      dup2(outpipe[1], STDOUT_FILENO);
      dup2(outpipe[1], STDERR_FILENO);
      close(inpipe[0]);
      close(inpipe[1]);
      close(outpipe[0]);
      close(outpipe[1]);
      execl(exePath_.c_str(), exePath_.c_str(), (char*)nullptr);
      _exit(127);
    }
    close(inpipe[0]);
    close(outpipe[1]);
    in_w_ = inpipe[1];
    out_r_ = outpipe[0];
    fout_ = fdopen(in_w_, "w");
    fin_ = fdopen(out_r_, "r");
    if (!fin_ || !fout_) throw std::runtime_error("fdopen failed");
    setvbuf(fout_, nullptr, _IONBF, 0);
#endif
  }

  void terminate() {
#ifdef _WIN32
    if (fout_) {
      std::fputs("quit\n", fout_);
      std::fflush(fout_);
    }
    if (pi_.hProcess) {
      WaitForSingleObject(pi_.hProcess, 500);
      CloseHandle(pi_.hThread);
      CloseHandle(pi_.hProcess);
      pi_.hThread = pi_.hProcess = NULL;
    }
#else
    if (fout_) {
      std::fputs("quit\n", fout_);
      std::fflush(fout_);
    }
    if (pid_ > 0) {
      int status = 0;
      waitpid(pid_, &status, 0);
      pid_ = -1;
    }
#endif
    if (fin_) {
      std::fclose(fin_);
      fin_ = nullptr;
    }
    if (fout_) {
      std::fclose(fout_);
      fout_ = nullptr;
    }
  }
};

// ------------------------ Data generation (parallel self-play) ------------------------
static void run_games_worker(int workerId, const Options& opts, std::atomic<int>& nextGame,
                             int totalGames, std::vector<RawSample>& outSamples,
                             std::mutex& outMutex, ProgressMeter& pm) {
  uint64_t engineSeed = opts.seed ? (opts.seed ^ (0x9E3779B97F4A7C15ull + workerId)) : 0ull;
  UciEngine engine(opts.stockfishPath, opts, engineSeed);

  std::vector<RawSample> local;
  local.reserve(8192);
  std::vector<std::string> moveHistory;

  for (;;) {
    int g = nextGame.fetch_add(1, std::memory_order_relaxed);
    if (g >= totalGames) break;

    engine.ucinewgame();
    model::ChessGame game;
    game.setPosition(core::START_FEN);
    moveHistory.clear();

    std::vector<std::pair<std::string, core::Color>> gamePositions;
    std::array<int, 2> sideSampleCounters{0, 0};

    for (int ply = 0; ply < opts.maxPlies; ++ply) {
      game.checkGameResult();
      if (game.getResult() != core::GameResult::ONGOING) break;

      if (ply >= opts.sampleSkip) {
        const auto sideToMove = game.getGameState().sideToMove;
        auto& counter = sideSampleCounters[(size_t)sideToMove];
        if (counter % std::max(1, opts.sampleStride) == 0) {
          const auto fen = game.getFen();
          gamePositions.emplace_back(fen, sideToMove);
        }
        ++counter;
      }

      // Pick and play move
      std::string mv = engine.pick_move_from_startpos(moveHistory);
      if (mv.empty() || mv == "(none)") {
        game.checkGameResult();
        break;
      }
      if (!game.doMoveUCI(mv)) break;
      moveHistory.push_back(mv);

      game.checkGameResult();
      if (game.getResult() != core::GameResult::ONGOING) break;
    }

    const core::GameResult finalRes = game.getResult();
    core::Color winner = flip_color(game.getGameState().sideToMove);

    for (const auto& [fen, pov] : gamePositions) {
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
  if (opts.stockfishPath.empty())
    throw std::runtime_error("Stockfish path required for data generation");

  const int W = std::max(1, opts.genWorkers);
  std::vector<std::thread> threads;
  std::vector<RawSample> samples;
  samples.reserve(size_t(opts.games) * 32u);
  std::mutex samplesMutex;
  std::atomic<int> nextGame{0};

  ProgressMeter pm("Generating self-play games (parallel)", (std::size_t)opts.games,
                   opts.progressIntervalMs, true);

  threads.reserve(W);
  for (int w = 0; w < W; ++w) {
    threads.emplace_back(run_games_worker, w, std::cref(opts), std::ref(nextGame), opts.games,
                         std::ref(samples), std::ref(samplesMutex), std::ref(pm));
  }
  for (auto& t : threads) t.join();
  pm.finish();

  // Deduplicate FEN keys globally (keep first occurrence)
  std::unordered_set<std::string> seen;
  seen.reserve(samples.size() * 2 + 16);
  std::vector<RawSample> unique;
  unique.reserve(samples.size());
  for (auto& s : samples) {
    auto key = fen_key(s.fen);
    if (seen.insert(key).second) unique.push_back(std::move(s));
  }

  if (opts.sampleLimit && unique.size() > (size_t)*opts.sampleLimit)
    unique.resize((size_t)*opts.sampleLimit);
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

// ------------------------ Prepared cache I/O ------------------------
struct PreparedCacheHeaderV1 {
  uint32_t magic = 0x54455845u;  // 'TEXE'
  uint32_t version = 1;
  uint32_t paramCount = 0;
  uint64_t sampleCount = 0;
  double logisticScale = 256.0;
};

struct PreparedCacheHeaderV2 {
  uint32_t magic = 0x54455845u;  // 'TEXE'
  uint32_t version = 2;
  uint32_t paramCount = 0;
  uint64_t sampleCount = 0;
  double logisticScale = 256.0;
  uint64_t defaultsHash = 0;
  uint32_t deltaStep = 1;
  uint32_t engineId = 0;  // reserved
};

struct PreparedCacheHeaderV3 {
  uint32_t magic = 0x54455845u;  // 'TEXE'
  uint32_t version = 3;
  uint32_t paramCount = 0;
  uint64_t sampleCount = 0;
  double logisticScale = 256.0;
  uint64_t defaultsHash = 0;
  uint32_t deltaStep = 1;
  uint32_t engineId = 0;  // reserved
  uint64_t checksum = 0;  // FNV-1a of content
};

static uint64_t fnv1a64_update(uint64_t h, uint64_t x) {
  h ^= x;
  h *= 1099511628211ull;
  return h;
}
static uint64_t hash_defaults(const std::span<const engine::EvalParamEntry>& entries,
                              const std::vector<int>& defaults, int deltaStep, uint32_t engineId) {
  uint64_t h = 1469598103934665603ull;  // FNV-1a
  auto mix = [&](uint64_t x) { h = fnv1a64_update(h, x); };
  mix((uint64_t)entries.size());
  for (size_t i = 0; i < entries.size(); ++i) {
    for (unsigned char c : entries[i].name) mix(c);
    mix((uint64_t)(int64_t)defaults[i]);
  }
  mix((uint64_t)deltaStep);
  mix((uint64_t)engineId);
  return h;
}

static uint64_t checksum_samples(const std::vector<PreparedSample>& v) {
  uint64_t h = 1469598103934665603ull;
  for (const auto& s : v) {
    for (unsigned char c : s.fen) h = fnv1a64_update(h, c);
    h = fnv1a64_update(h, (uint64_t)std::llround(s.result * 1e6));
    h = fnv1a64_update(h, (uint64_t)std::llround(s.baseEval * 1e2));
    h = fnv1a64_update(h, (uint64_t)std::llround(s.weight * 1e6));
    for (float g : s.gradients) {
      h = fnv1a64_update(h, (uint64_t)std::llround((double)g * 1e3));
    }
  }
  return h;
}

bool load_prepared_cache(const std::string& path, std::vector<PreparedSample>& out,
                         uint32_t expectedParams, double expectedScale,
                         uint64_t expectedDefaultsHash, int expectedDelta, bool& hasFenOut) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return false;
  uint32_t magic = 0, version = 0;
  f.read(reinterpret_cast<char*>(&magic), sizeof(magic));
  f.read(reinterpret_cast<char*>(&version), sizeof(version));
  if (!f || magic != 0x54455845u) return false;

  if (version == 1) {
    PreparedCacheHeaderV1 h{};
    h.magic = magic;
    h.version = version;
    f.read(reinterpret_cast<char*>(&h.paramCount), sizeof(h.paramCount));
    f.read(reinterpret_cast<char*>(&h.sampleCount), sizeof(h.sampleCount));
    f.read(reinterpret_cast<char*>(&h.logisticScale), sizeof(h.logisticScale));
    if (!f || h.paramCount != expectedParams) return false;
    if (std::abs(h.logisticScale - expectedScale) > 1e-9) return false;

    out.clear();
    out.resize(h.sampleCount);
    for (uint64_t i = 0; i < h.sampleCount; ++i) {
      float res, base;
      f.read(reinterpret_cast<char*>(&res), sizeof(float));
      f.read(reinterpret_cast<char*>(&base), sizeof(float));
      out[i].result = res;
      out[i].baseEval = base;
      out[i].weight = 1.0f;  // v1: no weight
    }
    for (uint64_t i = 0; i < h.sampleCount; ++i) {
      out[i].gradients.resize(h.paramCount);
      f.read(reinterpret_cast<char*>(out[i].gradients.data()), sizeof(float) * h.paramCount);
    }
    hasFenOut = false;  // v1 has no FEN
    return (bool)f;
  } else if (version == 2) {
    PreparedCacheHeaderV2 h{};
    h.magic = magic;
    h.version = version;
    f.read(reinterpret_cast<char*>(&h.paramCount), sizeof(h.paramCount));
    f.read(reinterpret_cast<char*>(&h.sampleCount), sizeof(h.sampleCount));
    f.read(reinterpret_cast<char*>(&h.logisticScale), sizeof(h.logisticScale));
    f.read(reinterpret_cast<char*>(&h.defaultsHash), sizeof(h.defaultsHash));
    f.read(reinterpret_cast<char*>(&h.deltaStep), sizeof(h.deltaStep));
    f.read(reinterpret_cast<char*>(&h.engineId), sizeof(h.engineId));
    if (!f || h.paramCount != expectedParams) return false;
    if (std::abs(h.logisticScale - expectedScale) > 1e-9) return false;
    if (h.defaultsHash != expectedDefaultsHash) return false;
    if ((int)h.deltaStep != expectedDelta) return false;

    out.clear();
    out.resize(h.sampleCount);
    for (uint64_t i = 0; i < h.sampleCount; ++i) {
      uint32_t flen = 0;
      f.read(reinterpret_cast<char*>(&flen), sizeof(flen));
      std::string fen(flen, '\0');
      if (flen) f.read(&fen[0], flen);
      float res = 0, base = 0;
      f.read(reinterpret_cast<char*>(&res), sizeof(float));
      f.read(reinterpret_cast<char*>(&base), sizeof(float));
      out[i].fen = std::move(fen);
      out[i].result = res;
      out[i].baseEval = base;
      out[i].weight = 1.0f;
    }
    for (uint64_t i = 0; i < h.sampleCount; ++i) {
      out[i].gradients.resize(h.paramCount);
      f.read(reinterpret_cast<char*>(out[i].gradients.data()), sizeof(float) * h.paramCount);
    }
    hasFenOut = true;
    return (bool)f;
  } else if (version == 3) {
    PreparedCacheHeaderV3 h{};
    h.magic = magic;
    h.version = version;
    f.read(reinterpret_cast<char*>(&h.paramCount), sizeof(h.paramCount));
    f.read(reinterpret_cast<char*>(&h.sampleCount), sizeof(h.sampleCount));
    f.read(reinterpret_cast<char*>(&h.logisticScale), sizeof(h.logisticScale));
    f.read(reinterpret_cast<char*>(&h.defaultsHash), sizeof(h.defaultsHash));
    f.read(reinterpret_cast<char*>(&h.deltaStep), sizeof(h.deltaStep));
    f.read(reinterpret_cast<char*>(&h.engineId), sizeof(h.engineId));
    f.read(reinterpret_cast<char*>(&h.checksum), sizeof(h.checksum));
    if (!f || h.paramCount != expectedParams) return false;
    if (std::abs(h.logisticScale - expectedScale) > 1e-9) return false;
    if (h.defaultsHash != expectedDefaultsHash) return false;
    if ((int)h.deltaStep != expectedDelta) return false;

    out.clear();
    out.resize(h.sampleCount);
    for (uint64_t i = 0; i < h.sampleCount; ++i) {
      uint32_t flen = 0;
      f.read(reinterpret_cast<char*>(&flen), sizeof(flen));
      std::string fen(flen, '\0');
      if (flen) f.read(&fen[0], flen);
      float res = 0, base = 0, w = 1.0f;
      f.read(reinterpret_cast<char*>(&res), sizeof(float));
      f.read(reinterpret_cast<char*>(&base), sizeof(float));
      f.read(reinterpret_cast<char*>(&w), sizeof(float));
      out[i].fen = std::move(fen);
      out[i].result = res;
      out[i].baseEval = base;
      out[i].weight = w;
    }
    for (uint64_t i = 0; i < h.sampleCount; ++i) {
      out[i].gradients.resize(h.paramCount);
      f.read(reinterpret_cast<char*>(out[i].gradients.data()), sizeof(float) * h.paramCount);
    }
    hasFenOut = true;
    // verify checksum
    if (checksum_samples(out) != h.checksum) return false;
    return (bool)f;
  }
  return false;
}

bool save_prepared_cache(const std::string& path, const std::vector<PreparedSample>& samples,
                         uint32_t paramCount, double logisticScale, uint64_t defaultsHash,
                         int deltaStep, uint32_t engineId = 0) {
  fs::path p{path};
  if (p.has_parent_path()) {
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
  }
  std::ofstream f(path, std::ios::binary | std::ios::trunc);
  if (!f) return false;
  PreparedCacheHeaderV3 h{};
  h.paramCount = paramCount;
  h.sampleCount = samples.size();
  h.logisticScale = logisticScale;
  h.defaultsHash = defaultsHash;
  h.deltaStep = (uint32_t)deltaStep;
  h.engineId = engineId;
  h.checksum = checksum_samples(samples);
  f.write(reinterpret_cast<const char*>(&h), sizeof(h));
  for (const auto& s : samples) {
    uint32_t flen = (uint32_t)s.fen.size();
    f.write(reinterpret_cast<const char*>(&flen), sizeof(flen));
    if (flen) f.write(s.fen.data(), flen);
    f.write(reinterpret_cast<const char*>(&s.result), sizeof(float));
    f.write(reinterpret_cast<const char*>(&s.baseEval), sizeof(float));
    f.write(reinterpret_cast<const char*>(&s.weight), sizeof(float));
  }
  for (const auto& s : samples) {
    f.write(reinterpret_cast<const char*>(s.gradients.data()), sizeof(float) * s.gradients.size());
  }
  return (bool)f;
}

// ------------------------ Texel preparation & training ------------------------
PreparedSample prepare_sample_with_delta(const std::string& fen, double result,
                                         engine::Evaluator& evaluator,
                                         const std::vector<int>& linpoint,
                                         const std::span<const engine::EvalParamEntry>& entries,
                                         int deltaStep, double scaleForWeight) {
  model::ChessGame game;
  game.setPosition(fen);
  auto& pos = game.getPositionRefForBot();
  pos.rebuildEvalAcc();

  PreparedSample prepared;
  prepared.fen = fen;
  prepared.result = (float)result;
  prepared.gradients.resize(entries.size());

  evaluator.clearCaches();
  const auto pov = game.getGameState().sideToMove;
  const double sgn = (pov == core::Color::White) ? 1.0 : -1.0;

  // set linearization point into engine
  engine::set_eval_param_values(linpoint);
  prepared.baseEval = (float)(sgn * (double)evaluator.evaluate(pos));

  // simple, robust sample weighting: focus on balanced positions
  double w =
      1.0 /
      (1.0 + std::pow(std::abs((double)prepared.baseEval) / std::max(1.0, scaleForWeight), 2.0));
  prepared.weight = (float)w;

  const int delta = std::max(1, deltaStep);
  for (size_t i = 0; i < entries.size(); ++i) {
    int* ptr = entries[i].value;
    const int orig = linpoint[i];

    *ptr = orig + delta;
    evaluator.clearCaches();
    const double plus = sgn * evaluator.evaluate(pos);
    *ptr = orig - delta;
    evaluator.clearCaches();
    const double minus = sgn * evaluator.evaluate(pos);
    *ptr = orig;

    prepared.gradients[i] = (float)((plus - minus) / (2.0 * delta));
  }
  evaluator.clearCaches();
  return prepared;
}

std::vector<PreparedSample> prepare_samples(const std::vector<RawSample>& rawSamples,
                                            engine::Evaluator& evaluator,
                                            const std::vector<int>& linpoint,
                                            const std::span<const engine::EvalParamEntry>& entries,
                                            const Options& opts) {
  std::vector<RawSample> work = rawSamples;
  if (opts.sampleLimit && work.size() > (size_t)*opts.sampleLimit)
    work.resize((size_t)*opts.sampleLimit);

  if (opts.shuffleBeforeTraining) {
    std::mt19937_64 rng{opts.seed ? opts.seed ^ 0xD1B54A32D192ED03ull : std::random_device{}()};
    std::shuffle(work.begin(), work.end(), rng);
  }

  std::vector<PreparedSample> prepared;
  prepared.resize(work.size());

  ProgressMeter prepPM("Preparing samples (finite-diff)", work.size(), opts.progressIntervalMs);
  // NOTE: serial on purpose — finite-diff toggles global eval params
  for (size_t i = 0; i < work.size(); ++i) {
    prepared[i] = prepare_sample_with_delta(work[i].fen, work[i].result, evaluator, linpoint,
                                            entries, opts.relinDelta, opts.logisticScale);
    prepPM.add(1);
  }
  prepPM.finish();
  return prepared;
}

struct TrainingResult {
  std::vector<double> weights;  // engine parameters only
  double finalLoss = 0.0;
  double learnedBias = 0.0;
  double learnedScale = 0.0;  // final scale actually used during training
};

struct TrainExtrasIdx {
  int biasIdx = -1;   // index in extended parameter vector
  int scaleIdx = -1;  // we store log-scale parameter at this index
};

static double sigmoid(double x) {
  if (x > 500.0) return 1.0;
  if (x < -500.0) return 0.0;
  return 1.0 / (1.0 + std::exp(-x));
}

static double lr_schedule(const Options& o, int step, int totalSteps) {
  double lr = o.learningRate;
  if (o.lrWarmup > 0 && step < o.lrWarmup) {
    lr *= (double)(step + 1) / (double)std::max(1, o.lrWarmup);
  }
  if (o.lrCosine > 0) {
    int t = std::min(step, o.lrCosine);

    double cosdec = 0.5 * (1.0 + std::cos(M_PI * (double)t / (double)o.lrCosine));
    lr *= cosdec;
  }
  return std::max(lr, 1e-12);
}

static double compute_avg_loss_pool(WorkerPool& pool, const std::vector<PreparedSample>& samples,
                                    const std::vector<double>& wEngine,
                                    const std::vector<double>& w0, double bias, double logScale) {
  const size_t P = wEngine.size(), N = samples.size();
  if (N == 0) return 0.0;

  const int TW = pool.size();
  std::vector<double> tLossSum(TW, 0.0);
  std::vector<double> tSumW(TW, 0.0);
  std::vector<size_t> cuts(TW + 1, 0);
  for (int t = 0; t < TW; ++t) cuts[t] = (N * t) / TW;
  cuts[TW] = N;

  pool.run([&](int t) {
    size_t start = cuts[t], end = cuts[t + 1];
    double lossSum = 0.0;
    double sumW = 0.0;
    for (size_t i = start; i < end; ++i) {
      const auto& s = samples[i];
      const float* gptr = s.gradients.data();
      double eval = s.baseEval;
      for (size_t j = 0; j < P; ++j) eval += (wEngine[j] - w0[j]) * (double)gptr[j];
      eval += bias;
      double scale = std::exp(logScale);
      double scaled = std::clamp(eval / scale, -500.0, 500.0);
      double prob = sigmoid(scaled);
      double target = s.result;
      double w = std::max(0.0f, s.weight);
      const double epsStab = 1e-12;
      lossSum += w * (-(target * std::log(std::max(prob, epsStab)) +
                        (1.0 - target) * std::log(std::max(1.0 - prob, epsStab))));
      sumW += w;
    }
    tLossSum[t] = lossSum;
    tSumW[t] = sumW;
  });

  // weighted average across threads
  double totalLossSum = 0.0, totalW = 0.0;
  for (int t = 0; t < TW; ++t) {
    totalLossSum += tLossSum[t];
    totalW += tSumW[t];
  }
  return (totalW > 0.0) ? (totalLossSum / totalW) : 0.0;
}

static double autotune_scale(WorkerPool& pool, const std::vector<PreparedSample>& setForScale,
                             const std::vector<double>& w, const std::vector<double>& w0,
                             double bias, double initScale) {
  if (setForScale.empty()) return initScale;
  std::array<double, 7> factors{0.5, 0.75, 1.0, 1.25, 1.5, 1.75, 2.0};
  double best = initScale;
  double bestL = compute_avg_loss_pool(pool, setForScale, w, w0, bias, std::log(initScale));
  for (double f : factors) {
    double s = std::max(1.0, initScale * f);
    double L = compute_avg_loss_pool(pool, setForScale, w, w0, bias, std::log(s));
    if (L < bestL) {
      bestL = L;
      best = s;
    }
  }
  std::cout << "Auto-scale: " << initScale << " -> " << best << " (loss " << bestL << ")\n";
  return best;
}

TrainingResult train_parallel(std::vector<PreparedSample>& samples,
                              std::vector<PreparedSample>& valSamples,
                              const std::vector<int>& defaults,
                              const std::span<const engine::EvalParamEntry>& entries,
                              const Options& opts) {
  if (samples.empty()) throw std::runtime_error("No samples to train on");
  const size_t Pengine = entries.size();
  WorkerPool pool(std::max(1, opts.trainWorkers));

  std::vector<double> wEngine(defaults.begin(), defaults.end());  // current engine weights
  std::vector<double> w0(defaults.begin(), defaults.end());       // linearization point
  double bias = 0.0;
  double logScale = std::log(std::max(1.0, opts.logisticScale));

  if (opts.initWeightsPath) {
    if (auto wInit = read_weights_file(*opts.initWeightsPath, entries)) {
      for (size_t j = 0; j < Pengine; ++j) wEngine[j] = (double)(*wInit)[j];
      std::cout << "Initialized weights from " << *opts.initWeightsPath << "\n";
    } else {
      std::cout << "Warning: could not parse init weights; using defaults.\n";
    }
  }

  // Auto-scale on startup (use val if available else train)
  if (opts.autoScale && !opts.learnScale) {
    auto& setForScale = !valSamples.empty() ? valSamples : samples;
    double best = autotune_scale(pool, setForScale, wEngine, w0, bias, opts.logisticScale);
    const_cast<double&>(opts.logisticScale) = best;  // safe (local copy)
    logScale = std::log(best);
  }

  const int logEvery = (opts.logEvery > 0) ? opts.logEvery : std::max(1, opts.iterations / 5);
  const int evalEvery = (opts.evalEvery > 0) ? opts.evalEvery : logEvery;

  // Adam/AdamW state
  std::vector<double> m(Pengine + (opts.learnBias ? 1 : 0) + (opts.learnScale ? 1 : 0), 0.0),
      v(Pengine + (opts.learnBias ? 1 : 0) + (opts.learnScale ? 1 : 0), 0.0);
  auto M = [&](size_t idx) -> double& { return m[idx]; };
  auto V = [&](size_t idx) -> double& { return v[idx]; };
  double b1 = opts.adamBeta1, b2 = opts.adamBeta2, eps = opts.adamEps;
  double b1t = 1.0, b2t = 1.0;

  // Parameter indexing helpers
  TrainExtrasIdx idxs{};
  size_t Ptot = Pengine;
  if (opts.learnBias) {
    idxs.biasIdx = (int)Ptot;
    ++Ptot;
  }
  if (opts.learnScale) {
    idxs.scaleIdx = (int)Ptot;
    ++Ptot;
  }

  // Minibatch scheduler (deterministic if seed != 0)
  std::mt19937_64 rng(opts.seed ? (opts.seed ^ 0xA0761D6478BD642Full) : std::random_device{}());
  const size_t Ntrain = samples.size();
  const size_t B =
      (opts.batchSize > 0 && opts.batchSize < (int)Ntrain) ? (size_t)opts.batchSize : Ntrain;

  std::vector<size_t> perm(Ntrain);
  std::iota(perm.begin(), perm.end(), 0);
  if (B < Ntrain) std::shuffle(perm.begin(), perm.end(), rng);
  size_t cursor = 0;

  ProgressMeter trainPM("Training (Texel)", (std::size_t)opts.iterations, opts.progressIntervalMs);

  double bestVal = std::numeric_limits<double>::infinity();
  int patienceLeft = opts.earlyStopPatience;
  std::vector<double> bestEngine = wEngine;
  double bestBias = bias;
  double bestLogScale = logScale;

  // Thread-local accumulators
  const int TW = pool.size();
  std::vector<std::vector<double>> tg(TW, std::vector<double>(Ptot, 0.0));
  std::vector<double> threadLossSum(TW, 0.0), threadSumW(TW, 0.0);
  std::vector<size_t> cuts(TW + 1, 0);

  auto build_batch = [&](std::vector<size_t>& batch) {
    batch.clear();
    batch.resize(B);
    if (B == Ntrain) {
      batch = perm;
    } else {
      for (size_t i = 0; i < B; ++i) {
        if (cursor >= Ntrain) {
          std::shuffle(perm.begin(), perm.end(), rng);
          cursor = 0;
        }
        batch[i] = perm[cursor++];
      }
    }
  };
  auto partition_batch = [&](size_t L) {
    for (int t = 0; t < TW; ++t) cuts[t] = (L * t) / TW;
    cuts[TW] = L;
  };

  std::vector<size_t> batchIdx;
  batchIdx.reserve(B);

  // CSV log
  std::ofstream csv;
  if (opts.logCsv) {
    fs::path p{*opts.logCsv};
    if (p.has_parent_path()) {
      std::error_code ec;
      fs::create_directories(p.parent_path(), ec);
    }
    csv.open(*opts.logCsv, std::ios::trunc);
    if (csv) csv << "iter,train_loss,val_loss,scale,bias,lr\n";
  }

  lilia::engine::Evaluator evaluator;  // used for relinearization when needed

  for (int iter = 0; iter < opts.iterations; ++iter) {
    build_batch(batchIdx);
    partition_batch(batchIdx.size());

    for (int t = 0; t < TW; ++t) {
      std::fill(tg[t].begin(), tg[t].end(), 0.0);
      threadLossSum[t] = 0.0;
      threadSumW[t] = 0.0;
    }

    double lrNow = lr_schedule(opts, iter, opts.iterations);

    pool.run([&](int t) {
      size_t s0 = cuts[t], s1 = cuts[t + 1];
      auto& G = tg[t];
      double lossSum = 0.0, sumW = 0.0;
      for (size_t k = s0; k < s1; ++k) {
        const auto& s = samples[batchIdx[k]];
        const float* gptr = s.gradients.data();
        double eval = s.baseEval;
        for (size_t j = 0; j < Pengine; ++j) eval += (wEngine[j] - w0[j]) * (double)gptr[j];
        if (opts.learnBias) eval += bias;
        const double scale = std::exp(logScale);
        const double scaled = std::clamp(eval / scale, -500.0, 500.0);
        const double prob = sigmoid(scaled);
        const double target = s.result;
        const double w = std::max(0.0f, s.weight);

        const double epsStab = 1e-12;
        lossSum += w * (-(target * std::log(std::max(prob, epsStab)) +
                          (1.0 - target) * std::log(std::max(1.0 - prob, epsStab))));
        sumW += w;

        const double diff = w * (prob - target);
        // engine param grads
        for (size_t j = 0; j < Pengine; ++j) G[j] += (diff / scale) * (double)gptr[j];
        // bias grad
        if (opts.learnBias) G[(size_t)idxs.biasIdx] += (diff / scale) * 1.0;
        // log-scale grad (see derivation in analysis): -(w)*(prob-target)*(eval/scale)
        if (opts.learnScale) G[(size_t)idxs.scaleIdx] += -(diff) * (eval / scale);
      }
      // No per-thread normalization; reduce with total weight later
      threadLossSum[t] = lossSum;
      threadSumW[t] = sumW;
    });

    // reduce (weighted by total sample weight)
    std::vector<double> g(Ptot, 0.0);
    double totalLossSum = 0.0, totalW = 0.0;
    for (int t = 0; t < TW; ++t) {
      for (size_t j = 0; j < Ptot; ++j) g[j] += tg[t][j];
      totalLossSum += threadLossSum[t];
      totalW += threadSumW[t];
    }
    double loss = (totalW > 0.0) ? (totalLossSum / totalW) : 0.0;
    if (totalW > 0.0) {
      const double invW = 1.0 / totalW;
      for (double& x : g) x *= invW;
    }

    // Legacy L2 (on engine deltas relative to linpoint)
    if (opts.l2 > 0.0) {
      for (size_t j = 0; j < Pengine; ++j) {
        const double d = (wEngine[j] - w0[j]);
        g[j] += opts.l2 * d;
        loss += 0.5 * opts.l2 * d * d;
      }
    }

    // Grad clipping (on full vector)
    if (opts.gradClip > 0.0) {
      double n2 = 0.0;
      for (double x : g) n2 += x * x;
      double nrm = std::sqrt(n2);
      if (nrm > opts.gradClip && nrm > 0.0) {
        double sc = opts.gradClip / nrm;
        for (double& x : g) x *= sc;
      }
    }

    // Adam/SGD update with optional AdamW weight decay (decoupled)
    if (opts.useAdam) {
      b1t *= b1;
      b2t *= b2;
      for (size_t j = 0; j < Ptot; ++j) {
        M(j) = b1 * M(j) + (1.0 - b1) * g[j];
        V(j) = b2 * V(j) + (1.0 - b2) * (g[j] * g[j]);
        double mhat = M(j) / (1.0 - b1t);
        double vhat = V(j) / (1.0 - b2t);
        double step = lrNow * mhat / (std::sqrt(vhat) + eps);
        if (j < Pengine)
          wEngine[j] -= step;
        else if ((int)j == idxs.biasIdx)
          bias -= step;
        else if ((int)j == idxs.scaleIdx)
          logScale -= step;
      }
      // AdamW decay on engine+bias (not on logScale)
      if (opts.weightDecay > 0.0) {
        const double wd = opts.weightDecay * lrNow;  // decoupled
        for (size_t j = 0; j < Pengine; ++j) wEngine[j] *= (1.0 - wd);
        if (opts.learnBias) bias *= (1.0 - wd);
      }
    } else {
      for (size_t j = 0; j < Pengine; ++j) wEngine[j] -= lrNow * g[j];
      if (opts.learnBias) bias -= lrNow * g[(size_t)idxs.biasIdx];
      if (opts.learnScale) logScale -= lrNow * g[(size_t)idxs.scaleIdx];
      if (opts.weightDecay > 0.0) {
        const double wd = opts.weightDecay * lrNow;
        for (size_t j = 0; j < Pengine; ++j) wEngine[j] *= (1.0 - wd);
        if (opts.learnBias) bias *= (1.0 - wd);
      }
    }

    // logging & validation
    bool doLog = ((iter + 1) % logEvery == 0 || iter == opts.iterations - 1);
    bool doEval =
        (opts.valSplit > 0.0) && ((iter + 1) % evalEvery == 0 || iter == opts.iterations - 1);
    if (doLog)
      std::cout << "\nIter " << (iter + 1) << "/" << opts.iterations << ": loss=" << loss
                << " scale=" << std::exp(logScale)
                << (opts.learnBias ? (" bias=" + std::to_string(bias)) : "") << "\n";

    double vloss = std::numeric_limits<double>::quiet_NaN();
    if (doEval && !valSamples.empty()) {
      vloss = compute_avg_loss_pool(pool, valSamples, wEngine, w0, bias, logScale);
      if (doLog) std::cout << "val=" << vloss << "\n";
      if (vloss + opts.earlyStopDelta < bestVal) {
        bestVal = vloss;
        bestEngine = wEngine;
        bestBias = bias;
        bestLogScale = logScale;
        patienceLeft = opts.earlyStopPatience;
      } else if (opts.earlyStopPatience > 0) {
        if (--patienceLeft <= 0) {
          std::cout << "  [early stop]\n";
          wEngine = bestEngine;
          bias = bestBias;
          logScale = bestLogScale;
          trainPM.add(1);
          break;
        }
      }
    }

    if (csv) {
      csv << (iter + 1) << "," << loss << "," << (std::isnan(vloss) ? 0.0 : vloss) << ","
          << std::exp(logScale) << "," << bias << "," << lrNow << "\n";
    }

    std::ostringstream statusStream;
    statusStream << std::fixed << std::setprecision(4) << "loss=" << loss;
    if (!std::isnan(vloss)) statusStream << " val=" << vloss;
    statusStream << std::defaultfloat << std::setprecision(3) << " lr=" << lrNow;
    std::string statusText = statusStream.str();
    trainPM.set_status(statusText);

    // optional relinearization (serial, safe)
    if (opts.relinEvery > 0 && (iter + 1) % opts.relinEvery == 0) {
      trainPM.set_status(statusText + "  [relinearizing]", true);
      std::vector<int> w_int(Pengine);
      for (size_t j = 0; j < Pengine; ++j) w_int[j] = (int)std::llround(wEngine[j]);
      engine::set_eval_param_values(w_int);
      w0 = wEngine;

      size_t M = samples.size();
      if (opts.relinFrac > 0.0 && opts.relinFrac < 1.0)
        M = (size_t)std::max<size_t>(1, std::llround(opts.relinFrac * (double)samples.size()));

      std::vector<size_t> idx(samples.size());
      std::iota(idx.begin(), idx.end(), 0);
      if (M < idx.size()) {
        std::mt19937_64 rr(opts.seed ? (opts.seed ^ 0xC2B2AE3D27D4EB4Full)
                                     : std::random_device{}());
        std::shuffle(idx.begin(), idx.end(), rr);
      }

      ProgressMeter relPM("Relinearizing samples", M, opts.progressIntervalMs);
      for (size_t k = 0; k < M; ++k) {
        auto i = idx[k];
        if (samples[i].fen.empty()) {  // from cache v1 -> can't relinearize this sample
          continue;
        }
        PreparedSample ps =
            prepare_sample_with_delta(samples[i].fen, samples[i].result, evaluator, w_int, entries,
                                      opts.relinDelta, std::exp(logScale));
        samples[i] = std::move(ps);
        relPM.add(1);
      }
      relPM.finish();
      trainPM.set_status(statusText);
    }

    trainPM.add(1);
  }
  trainPM.finish();
  if (csv) csv.close();

  double finalLoss = compute_avg_loss_pool(pool, samples, wEngine, w0, bias, logScale);
  if (opts.l2 > 0.0) {
    double reg = 0.0;
    for (size_t j = 0; j < Pengine; ++j) {
      double d = (wEngine[j] - w0[j]);
      reg += 0.5 * opts.l2 * d * d;
    }
    finalLoss += reg;
  }

  TrainingResult tr;
  tr.weights = std::move(wEngine);
  tr.finalLoss = finalLoss;
  tr.learnedBias = bias;
  tr.learnedScale = std::exp(logScale);
  return tr;
}

void emit_weights(const TrainingResult& result, const std::vector<int>& defaults,
                  const std::span<const engine::EvalParamEntry>& entries, const Options& opts,
                  const Options& originalOptsForHeader = Options{}) {
  std::vector<int> tuned;
  tuned.reserve(result.weights.size());
  for (double w : result.weights) tuned.push_back((int)std::llround(w));

  engine::set_eval_param_values(tuned);

  std::ostream* out = &std::cout;
  std::ofstream file;
  if (opts.weightsOutput) {
    fs::path p{*opts.weightsOutput};
    if (p.has_parent_path()) {
      std::error_code ec;
      fs::create_directories(p.parent_path(), ec);
    }
    file.open(p, std::ios::trunc);
    if (!file) throw std::runtime_error("Unable to open weights output file");
    out = &file;
  }

  *out << "# Tuned evaluation parameters\n";
  *out << "# Texel training loss: " << result.finalLoss << "\n";
  *out << "# scale_final=" << result.learnedScale << " bias_final=" << result.learnedBias << "\n";
  *out << "# scale_init=" << originalOptsForHeader.logisticScale
       << " lr=" << originalOptsForHeader.learningRate
       << " iters=" << originalOptsForHeader.iterations << " l2=" << originalOptsForHeader.l2
       << " weight_decay=" << originalOptsForHeader.weightDecay
       << " batch_size=" << originalOptsForHeader.batchSize
       << " val_split=" << originalOptsForHeader.valSplit
       << " grad_clip=" << originalOptsForHeader.gradClip << " seed=" << originalOptsForHeader.seed
       << " relin_every=" << originalOptsForHeader.relinEvery
       << " relin_frac=" << originalOptsForHeader.relinFrac
       << " relin_delta=" << originalOptsForHeader.relinDelta
       << " autoscale=" << (originalOptsForHeader.autoScale ? "yes" : "no")
       << " learn_scale=" << (originalOptsForHeader.learnScale ? "yes" : "no")
       << " learn_bias=" << (originalOptsForHeader.learnBias ? "yes" : "no")
       << " lr_warmup=" << originalOptsForHeader.lrWarmup
       << " lr_cosine=" << originalOptsForHeader.lrCosine
       << " adam=" << (originalOptsForHeader.useAdam ? "yes" : "no")
       << " train_workers=" << originalOptsForHeader.trainWorkers
       << " gen_workers=" << originalOptsForHeader.genWorkers
       << " shuffled=" << (originalOptsForHeader.shuffleBeforeTraining ? "yes" : "no")
       << " sample_limit="
       << (originalOptsForHeader.sampleLimit ? std::to_string(*originalOptsForHeader.sampleLimit)
                                             : "none")
       << "\n";

  for (size_t i = 0; i < entries.size(); ++i) {
    *out << entries[i].name << "=" << tuned[i] << "  # default=" << defaults[i]
         << " tuned=" << result.weights[i] << "\n";
  }
  *out << "# NOTE: bias and scale are not engine parameters; recorded above for calibration.\n";
  if (file) std::cout << "Wrote tuned weights to " << *opts.weightsOutput << "\n";
}

}  // namespace lilia::tools::texel

// ------------------------ main ------------------------
int main(int argc, char** argv) {
  using namespace lilia::tools::texel;
  try {
    lilia::engine::Engine::init();
    const DefaultPaths defaults = compute_default_paths(argc > 0 ? argv[0] : nullptr);
    Options opts = parse_args(argc, argv, defaults);

    if (opts.generateData && opts.stockfishPath.empty()) {
      std::ostringstream err;
      err << "Stockfish executable not found. Place it in tools/texel, next to texel_tuner, or"
          << " provide --stockfish.";
      throw std::runtime_error(err.str());
    }

    if (opts.generateData) {
      std::cout << "Using Stockfish at " << opts.stockfishPath << "\n";
      std::cout << "Threads=" << opts.threads << " MultiPV=" << opts.multipv
                << " temp(cp)=" << opts.tempCp
                << (opts.movetimeMs > 0
                        ? (" movetime=" + std::to_string(opts.movetimeMs) +
                           "ms jitter=" + std::to_string(opts.movetimeJitterMs) + "ms")
                        : (" depth=" + std::to_string(opts.depth)))
                << (opts.skillLevel ? (" skill=" + std::to_string(*opts.skillLevel)) : "")
                << (opts.elo ? (" elo=" + std::to_string(*opts.elo)) : "")
                << (opts.contempt ? (" contempt=" + std::to_string(*opts.contempt)) : "")
                << " gen_workers=" << opts.genWorkers << "\n";
    }

    std::cout << "Dataset path: " << opts.dataFile << "\n";
    if (opts.weightsOutput) std::cout << "Weights output path: " << *opts.weightsOutput << "\n";

    if (opts.generateData) {
      auto samples = generate_samples_parallel(opts);
      if (samples.empty())
        std::cerr << "No samples generated.\n";
      else
        write_dataset(samples, opts.dataFile);
    }

    if (opts.tune) {
      auto rawSamples = read_dataset(opts.dataFile);
      if (rawSamples.empty()) throw std::runtime_error("Dataset is empty");

      lilia::engine::Evaluator evaluator;
      lilia::engine::reset_eval_params();
      auto defaultsVals = lilia::engine::get_eval_param_values();
      auto entriesSpan = lilia::engine::eval_param_entries();

      std::vector<PreparedSample> prepared;
      std::vector<PreparedSample> valPrepared;

      // Optional: load cached prepared samples if compatible
      bool loadedFromCache = false, cacheHasFen = false;
      uint64_t defHash = hash_defaults(entriesSpan, defaultsVals, opts.relinDelta, 0);
      if (opts.preparedCache && opts.loadPreparedIfExists) {
        loadedFromCache =
            load_prepared_cache(*opts.preparedCache, prepared, (uint32_t)entriesSpan.size(),
                                opts.logisticScale, defHash, opts.relinDelta, cacheHasFen);
        if (loadedFromCache) {
          std::cout << "Loaded prepared samples from cache: " << *opts.preparedCache
                    << "  (fen=" << (cacheHasFen ? "yes" : "no") << ")\n";
        }
      }
      if (!loadedFromCache) {
        prepared = prepare_samples(rawSamples, evaluator, defaultsVals, entriesSpan, opts);
        std::cout << "Prepared " << prepared.size() << " samples for tuning\n";
        if (opts.preparedCache && opts.savePrepared) {
          if (save_prepared_cache(*opts.preparedCache, prepared, (uint32_t)entriesSpan.size(),
                                  opts.logisticScale, defHash, opts.relinDelta))
            std::cout << "Saved prepared cache to " << *opts.preparedCache << "\n";
          else
            std::cout << "Warning: failed to save prepared cache to " << *opts.preparedCache
                      << "\n";
        }
      } else if (opts.relinEvery > 0 && !cacheHasFen) {
        std::cout << "Note: cache has no FEN (v1). Relinearization disabled.\n";
      }

      // split train/val (deterministic with seed)
      if (opts.valSplit > 0.0 && prepared.size() > 10) {
        std::mt19937_64 rng(opts.seed ? (opts.seed ^ 0x41C64E6DA3BC0074ull)
                                      : std::random_device{}());
        std::shuffle(prepared.begin(), prepared.end(), rng);
        size_t nval = (size_t)std::round(opts.valSplit * prepared.size());
        nval = std::min(nval, prepared.size() / 2);
        valPrepared.insert(valPrepared.end(), prepared.begin(), prepared.begin() + nval);
        prepared.erase(prepared.begin(), prepared.begin() + nval);
        std::cout << "Train samples: " << prepared.size() << ", Val samples: " << valPrepared.size()
                  << "\n";
      }

      auto result = train_parallel(prepared, valPrepared, defaultsVals, entriesSpan, opts);
      emit_weights(result, defaultsVals, entriesSpan, opts, opts);
    }
  } catch (const std::exception& ex) {
    std::cerr << "Error: " << ex.what() << "\n";
    return 1;
  }
  return 0;
}
