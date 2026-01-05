#include "lilia/tools/texel/options.hpp"

#include <thread>
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

namespace lilia::tools::texel {

[[noreturn]] static void usage_and_exit(const DefaultPaths& d) {
  std::cerr
      << "Usage: texel_tuner [--generate-data] [--tune] [options]\n"
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
         "  --contempt <C>            Engine Contempt (optional)\n"
         "  --max-plies <N>           Max plies per game (default 160)\n"
         "  --sample-skip <N>         Skip first N plies before sampling (default 6)\n"
         "  --sample-stride <N>       Sample every N plies thereafter (default 4)\n"
         "  --data <file>             Dataset path (default " << d.dataFile.string() << ")\n"
         "  --iterations <N>          Training iterations (default 200)\n"
         "  --learning-rate <v>       Learning rate (default 5e-4)\n"
         "  --scale <v>               Logistic scale in centipawns (default 256)\n"
         "  --l2 <v>                  L2 regularization (legacy, default 0)\n"
         "  --no-shuffle              Do not shuffle dataset before training\n"
         "  --weights-output <file>   Write tuned weights (default " << d.weightsFile.string() << ")\n"
         "  --sample-limit <N>        Limit samples (applies to generation and training)\n"
         "  --progress-interval <ms>  Progress update interval (default 750)\n"
         "\nPerformance & training:\n"
         "  --gen-workers <N>         Parallel self-play workers (default hw threads)\n"
         "  --train-workers <N>       Training workers (default hw threads)\n"
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
         "  --log-csv <file>          Write training log CSV\n"
         "\nInit & linearization:\n"
         "  --init-weights <file>     Warm-start from weights file\n"
         "  --relin-every <N>         Relinearize every N iters (0 => off)\n"
         "  --relin-frac <r>          Fraction 0..1 of samples to relinearize\n"
         "  --relin-delta <D>         Finite-diff step for (re)linearization (default 1)\n"
         "  --prepared-cache <file>   Prepared cache file (v3)\n"
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
  o.weightsOutput = defaults.weightsFile.string();
  if (defaults.stockfish) o.stockfishPath = defaults.stockfish->string();

  const int hw = std::max(1u, std::thread::hardware_concurrency());
  o.threads = hw;
  o.genWorkers = hw;
  o.trainWorkers = hw;

  auto require_value = [&](int& i, const char* name) -> std::string {
    if (i + 1 >= argc) {
      std::cerr << "Missing value for " << name << "\n";
      usage_and_exit(defaults);
    }
    return argv[++i];
  };

  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];

    if (arg == "--generate-data") {
      o.generateData = true;
    } else if (arg == "--tune") {
      o.tune = true;
    } else if (arg == "--stockfish") {
      o.stockfishPath = require_value(i, "--stockfish");
    } else if (arg == "--games") {
      o.games = std::stoi(require_value(i, "--games"));
    } else if (arg == "--depth") {
      o.depth = std::stoi(require_value(i, "--depth"));
    } else if (arg == "--movetime") {
      o.movetimeMs = std::stoi(require_value(i, "--movetime"));
    } else if (arg == "--jitter") {
      o.movetimeJitterMs = std::stoi(require_value(i, "--jitter"));
    } else if (arg == "--threads") {
      o.threads = std::max(1, std::stoi(require_value(i, "--threads")));
    } else if (arg == "--multipv") {
      o.multipv = std::max(1, std::stoi(require_value(i, "--multipv")));
    } else if (arg == "--temp") {
      o.tempCp = std::stod(require_value(i, "--temp"));
    } else if (arg == "--skill") {
      o.skillLevel = std::stoi(require_value(i, "--skill"));
    } else if (arg == "--elo") {
      o.elo = std::stoi(require_value(i, "--elo"));
    } else if (arg == "--contempt") {
      o.contempt = std::stoi(require_value(i, "--contempt"));
    } else if (arg == "--max-plies") {
      o.maxPlies = std::stoi(require_value(i, "--max-plies"));
    } else if (arg == "--sample-skip") {
      o.sampleSkip = std::stoi(require_value(i, "--sample-skip"));
    } else if (arg == "--sample-stride") {
      o.sampleStride = std::stoi(require_value(i, "--sample-stride"));
    } else if (arg == "--data") {
      o.dataFile = require_value(i, "--data");
    } else if (arg == "--iterations") {
      o.iterations = std::stoi(require_value(i, "--iterations"));
    } else if (arg == "--learning-rate") {
      o.learningRate = std::stod(require_value(i, "--learning-rate"));
    } else if (arg == "--scale") {
      o.logisticScale = std::stod(require_value(i, "--scale"));
    } else if (arg == "--l2") {
      o.l2 = std::stod(require_value(i, "--l2"));
    } else if (arg == "--no-shuffle") {
      o.shuffleBeforeTraining = false;
    } else if (arg == "--weights-output") {
      o.weightsOutput = require_value(i, "--weights-output");
    } else if (arg == "--sample-limit") {
      o.sampleLimit = std::stoi(require_value(i, "--sample-limit"));
    } else if (arg == "--progress-interval") {
      o.progressIntervalMs = std::stoi(require_value(i, "--progress-interval"));
    } else if (arg == "--gen-workers") {
      o.genWorkers = std::max(1, std::stoi(require_value(i, "--gen-workers")));
    } else if (arg == "--train-workers") {
      o.trainWorkers = std::max(1, std::stoi(require_value(i, "--train-workers")));
    } else if (arg == "--adam") {
      o.useAdam = std::stoi(require_value(i, "--adam")) != 0;
    } else if (arg == "--adam-b1") {
      o.adamBeta1 = std::stod(require_value(i, "--adam-b1"));
    } else if (arg == "--adam-b2") {
      o.adamBeta2 = std::stod(require_value(i, "--adam-b2"));
    } else if (arg == "--adam-eps") {
      o.adamEps = std::stod(require_value(i, "--adam-eps"));
    } else if (arg == "--weight-decay") {
      o.weightDecay = std::stod(require_value(i, "--weight-decay"));
    } else if (arg == "--log-every") {
      o.logEvery = std::stoi(require_value(i, "--log-every"));
    } else if (arg == "--seed") {
      o.seed = static_cast<uint64_t>(std::stoull(require_value(i, "--seed")));
    } else if (arg == "--batch-size") {
      o.batchSize = std::stoi(require_value(i, "--batch-size"));
    } else if (arg == "--val-split") {
      o.valSplit = std::stod(require_value(i, "--val-split"));
    } else if (arg == "--eval-every") {
      o.evalEvery = std::stoi(require_value(i, "--eval-every"));
    } else if (arg == "--early-stop") {
      o.earlyStopPatience = std::stoi(require_value(i, "--early-stop"));
    } else if (arg == "--early-delta") {
      o.earlyStopDelta = std::stod(require_value(i, "--early-delta"));
    } else if (arg == "--grad-clip") {
      o.gradClip = std::stod(require_value(i, "--grad-clip"));
    } else if (arg == "--lr-warmup") {
      o.lrWarmup = std::stoi(require_value(i, "--lr-warmup"));
    } else if (arg == "--lr-cosine") {
      o.lrCosine = std::stoi(require_value(i, "--lr-cosine"));
    } else if (arg == "--prepared-cache") {
      o.preparedCache = require_value(i, "--prepared-cache");
    } else if (arg == "--no-load-prepared") {
      o.loadPreparedIfExists = false;
    } else if (arg == "--no-save-prepared") {
      o.savePrepared = false;
    } else if (arg == "--init-weights") {
      o.initWeightsPath = require_value(i, "--init-weights");
    } else if (arg == "--relin-every") {
      o.relinEvery = std::stoi(require_value(i, "--relin-every"));
    } else if (arg == "--relin-frac") {
      o.relinFrac = std::stod(require_value(i, "--relin-frac"));
    } else if (arg == "--relin-delta") {
      o.relinDelta = std::stoi(require_value(i, "--relin-delta"));
    } else if (arg == "--auto-scale") {
      o.autoScale = true;
    } else if (arg == "--learn-scale") {
      o.learnScale = true;
    } else if (arg == "--no-bias") {
      o.learnBias = false;
    } else if (arg == "--log-csv") {
      o.logCsv = require_value(i, "--log-csv");
    } else if (arg == "--help" || arg == "-h") {
      usage_and_exit(defaults);
    } else {
      std::cerr << "Unknown option: " << arg << "\n";
      usage_and_exit(defaults);
    }
  }

  if (!o.generateData && !o.tune) {
    std::cerr << "Nothing to do: specify --generate-data and/or --tune.\n";
    usage_and_exit(defaults);
  }

  // Normalize/clip for safety.
  o.valSplit = std::clamp(o.valSplit, 0.0, 0.5);
  o.batchSize = std::max(0, o.batchSize);
  o.evalEvery = std::max(0, o.evalEvery);
  o.earlyStopPatience = std::max(0, o.earlyStopPatience);
  o.earlyStopDelta = std::max(0.0, o.earlyStopDelta);
  o.gradClip = std::max(0.0, o.gradClip);
  o.relinEvery = std::max(0, o.relinEvery);
  o.relinFrac = std::clamp(o.relinFrac, 0.0, 1.0);
  o.relinDelta = std::max(1, o.relinDelta);
  o.lrWarmup = std::max(0, o.lrWarmup);
  o.lrCosine = std::max(0, o.lrCosine);
  o.weightDecay = std::max(0.0, o.weightDecay);
  o.multipv = std::max(1, o.multipv);
  o.sampleStride = std::max(1, o.sampleStride);
  o.sampleSkip = std::max(0, o.sampleSkip);
  o.maxPlies = std::max(1, o.maxPlies);
  o.threads = std::max(1, o.threads);
  o.genWorkers = std::max(1, o.genWorkers);
  o.trainWorkers = std::max(1, o.trainWorkers);
  o.depth = std::max(0, o.depth);
  o.movetimeMs = std::max(0, o.movetimeMs);
  o.movetimeJitterMs = std::max(0, o.movetimeJitterMs);
  o.logisticScale = std::max(1.0, o.logisticScale);
  return o;
}

}  // namespace lilia::tools::texel
