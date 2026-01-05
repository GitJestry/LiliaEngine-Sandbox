#include "lilia/tools/texel/texel_trainer.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numeric>
#include <numbers>
#include <random>
#include <sstream>
#include <unordered_map>

#include "lilia/engine/eval.hpp"
#include "lilia/model/chess_game.hpp"
#include "lilia/model/core/model_types.hpp"
#include "lilia/tools/texel/prepared_cache.hpp"
#include "lilia/tools/texel/progress.hpp"
#include "lilia/tools/texel/worker_pool.hpp"

namespace lilia::tools::texel {
namespace fs = std::filesystem;

namespace {

struct TrainExtrasIdx {
  int biasIdx = -1;   // in extended vector
  int scaleIdx = -1;  // in extended vector (log-scale)
};

inline double sigmoid(double x) {
  // Stable logistic sigmoid.
  if (x >= 50.0) return 1.0;
  if (x <= -50.0) return 0.0;
  return 1.0 / (1.0 + std::exp(-x));
}

inline double clamp_log_scale(double logScale) {
  // Avoid pathological exp overflow / denorms.
  const double lo = std::log(1.0);
  const double hi = std::log(1e6);
  return std::clamp(logScale, lo, hi);
}

double lr_schedule(const Options& o, int step) {
  double lr = o.learningRate;
  if (o.lrWarmup > 0 && step < o.lrWarmup) {
    lr *= (double)(step + 1) / (double)std::max(1, o.lrWarmup);
  }
  if (o.lrCosine > 0) {
    int t = std::min(step, o.lrCosine);
    const double cosdec = 0.5 * (1.0 + std::cos(std::numbers::pi * (double)t / (double)o.lrCosine));
    lr *= cosdec;
  }
  return std::max(lr, 1e-12);
}

std::optional<std::vector<int>> read_weights_file(
    const std::string& path, const std::span<const engine::EvalParamEntry>& entries) {
  std::ifstream in(path);
  if (!in) return std::nullopt;

  std::unordered_map<std::string, int> kv;
  std::string line;

  auto trim = [](std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    if (a == std::string::npos) s.clear();
    else s = s.substr(a, b - a + 1);
  };

  while (std::getline(in, line)) {
    if (line.empty() || line[0] == '#') continue;
    auto hash = line.find('#');
    if (hash != std::string::npos) line = line.substr(0, hash);
    auto eq = line.find('=');
    if (eq == std::string::npos) continue;

    std::string k = line.substr(0, eq);
    std::string v = line.substr(eq + 1);
    trim(k); trim(v);

    try { kv[k] = std::stoi(v); } catch (...) {}
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
  prepared.result = static_cast<float>(result);
  prepared.gradients.resize(entries.size());

  const auto stm = game.getGameState().sideToMove;
  const double sgn = (stm == core::Color::White) ? 1.0 : -1.0;

  // Set linearization point in global engine state.
  engine::set_eval_param_values(linpoint);

  evaluator.clearCaches();
  prepared.baseEval = static_cast<float>(sgn * static_cast<double>(evaluator.evaluate(pos)));

  // Weight: emphasize uncertain/balanced positions.
  const double denom = 1.0 + std::pow(std::abs(static_cast<double>(prepared.baseEval)) /
                                          std::max(1.0, scaleForWeight),
                                      2.0);
  prepared.weight = static_cast<float>(1.0 / denom);

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
    prepared.gradients[i] = static_cast<float>((plus - minus) / (2.0 * delta));
  }

  evaluator.clearCaches();
  return prepared;
}

double compute_avg_loss_pool(WorkerPool& pool, const std::vector<PreparedSample>& samples,
                             const std::vector<double>& wEngine, const std::vector<double>& w0,
                             double bias, double logScale) {
  const size_t P = wEngine.size();
  const size_t N = samples.size();
  if (N == 0) return 0.0;

  const int TW = pool.size();
  std::vector<double> tLossSum(TW, 0.0), tSumW(TW, 0.0);
  std::vector<size_t> cuts(TW + 1, 0);
  for (int t = 0; t < TW; ++t) cuts[t] = (N * static_cast<size_t>(t)) / static_cast<size_t>(TW);
  cuts[TW] = N;

  logScale = clamp_log_scale(logScale);
  const double scale = std::exp(logScale);

  pool.run([&](int t) {
    size_t start = cuts[t], end = cuts[t + 1];
    double lossSum = 0.0, sumW = 0.0;

    for (size_t i = start; i < end; ++i) {
      const auto& s = samples[i];
      const float* gptr = s.gradients.data();

      double eval = s.baseEval;
      for (size_t j = 0; j < P; ++j) eval += (wEngine[j] - w0[j]) * static_cast<double>(gptr[j]);
      eval += bias;

      const double z = std::clamp(eval / scale, -50.0, 50.0);
      const double prob = sigmoid(z);
      const double target = s.result;
      const double w = std::max(0.0f, s.weight);

      const double epsStab = 1e-12;
      lossSum += w * (-(target * std::log(std::max(prob, epsStab)) +
                        (1.0 - target) * std::log(std::max(1.0 - prob, epsStab))));
      sumW += w;
    }

    tLossSum[t] = lossSum;
    tSumW[t] = sumW;
  });

  double totalLossSum = 0.0, totalW = 0.0;
  for (int t = 0; t < TW; ++t) { totalLossSum += tLossSum[t]; totalW += tSumW[t]; }
  return (totalW > 0.0) ? (totalLossSum / totalW) : 0.0;
}

double autotune_scale(WorkerPool& pool, const std::vector<PreparedSample>& setForScale,
                      const std::vector<double>& w, const std::vector<double>& w0,
                      double bias, double initScale) {
  if (setForScale.empty()) return initScale;
  const std::array<double, 7> factors{0.5, 0.75, 1.0, 1.25, 1.5, 1.75, 2.0};

  double best = initScale;
  double bestL = compute_avg_loss_pool(pool, setForScale, w, w0, bias, std::log(initScale));
  for (double f : factors) {
    const double s = std::max(1.0, initScale * f);
    const double L = compute_avg_loss_pool(pool, setForScale, w, w0, bias, std::log(s));
    if (L < bestL) { bestL = L; best = s; }
  }
  std::cout << "Auto-scale: " << initScale << " -> " << best << " (loss " << bestL << ")\n";
  return best;
}

}  // namespace

std::vector<PreparedSample> prepare_samples(const std::vector<RawSample>& rawSamples,
                                            engine::Evaluator& evaluator,
                                            const std::vector<int>& linpoint,
                                            const std::span<const engine::EvalParamEntry>& entries,
                                            const Options& opts) {
  std::vector<RawSample> work = rawSamples;
  if (opts.sampleLimit && work.size() > static_cast<size_t>(*opts.sampleLimit))
    work.resize(static_cast<size_t>(*opts.sampleLimit));

  if (opts.shuffleBeforeTraining) {
    std::mt19937_64 rng{opts.seed ? (opts.seed ^ 0xD1B54A32D192ED03ull) : std::random_device{}()};
    std::shuffle(work.begin(), work.end(), rng);
  }

  std::vector<PreparedSample> prepared(work.size());

  ProgressMeter pm("Preparing samples (finite-diff)", work.size(), opts.progressIntervalMs);
  for (size_t i = 0; i < work.size(); ++i) {
    prepared[i] = prepare_sample_with_delta(work[i].fen, work[i].result, evaluator, linpoint,
                                            entries, opts.relinDelta, opts.logisticScale);
    pm.add(1);
  }
  pm.finish();
  return prepared;
}

TrainingResult train_texel(std::vector<PreparedSample>& samples,
                           std::vector<PreparedSample>& valSamples,
                           const std::vector<int>& defaults,
                           const std::span<const engine::EvalParamEntry>& entries,
                           const Options& opts) {
  if (samples.empty()) throw std::runtime_error("No samples to train on");
  const size_t Pengine = entries.size();

  WorkerPool pool(std::max(1, opts.trainWorkers));
  std::vector<double> wEngine(defaults.begin(), defaults.end());
  std::vector<double> w0(defaults.begin(), defaults.end());

  double bias = 0.0;
  double logScale = std::log(std::max(1.0, opts.logisticScale));

  // Warm start
  if (opts.initWeightsPath) {
    if (auto wInit = read_weights_file(*opts.initWeightsPath, entries)) {
      for (size_t j = 0; j < Pengine; ++j) wEngine[j] = static_cast<double>((*wInit)[j]);
      std::cout << "Initialized weights from " << *opts.initWeightsPath << "\n";
    } else {
      std::cout << "Warning: could not parse init weights; using defaults.\n";
    }
  }

  // Optional one-shot auto-scale (only when not learning scale).
  double initScale = std::max(1.0, opts.logisticScale);
  if (opts.autoScale && !opts.learnScale) {
    const auto& setForScale = !valSamples.empty() ? valSamples : samples;
    initScale = autotune_scale(pool, setForScale, wEngine, w0, bias, initScale);
    logScale = std::log(initScale);
  }

  const int logEvery = (opts.logEvery > 0) ? opts.logEvery : std::max(1, opts.iterations / 5);
  const int evalEvery = (opts.evalEvery > 0) ? opts.evalEvery : logEvery;

  // Extended parameter indexing.
  TrainExtrasIdx idxs{};
  size_t Ptot = Pengine;
  if (opts.learnBias) idxs.biasIdx = static_cast<int>(Ptot++);
  if (opts.learnScale) idxs.scaleIdx = static_cast<int>(Ptot++);

  // Adam state for extended vector.
  std::vector<double> m(Ptot, 0.0), v(Ptot, 0.0);
  double b1 = opts.adamBeta1, b2 = opts.adamBeta2, eps = opts.adamEps;
  double b1t = 1.0, b2t = 1.0;

  // Minibatch scheduling.
  std::mt19937_64 rng(opts.seed ? (opts.seed ^ 0xA0761D6478BD642Full) : std::random_device{}());
  const size_t Ntrain = samples.size();
  const size_t B = (opts.batchSize > 0 && opts.batchSize < static_cast<int>(Ntrain))
                       ? static_cast<size_t>(opts.batchSize)
                       : Ntrain;

  std::vector<size_t> perm(Ntrain);
  std::iota(perm.begin(), perm.end(), 0);
  if (B < Ntrain) std::shuffle(perm.begin(), perm.end(), rng);
  size_t cursor = 0;

  auto build_batch = [&](std::vector<size_t>& batchIdx) {
    batchIdx.resize(B);
    if (B == Ntrain) {
      std::copy(perm.begin(), perm.end(), batchIdx.begin());
      return;
    }
    for (size_t i = 0; i < B; ++i) {
      if (cursor >= Ntrain) {
        std::shuffle(perm.begin(), perm.end(), rng);
        cursor = 0;
      }
      batchIdx[i] = perm[cursor++];
    }
  };

  // CSV logging.
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

  // Thread-local accumulators.
  const int TW = pool.size();
  std::vector<std::vector<double>> tg(TW, std::vector<double>(Ptot, 0.0));
  std::vector<double> threadLossSum(TW, 0.0), threadSumW(TW, 0.0);
  std::vector<size_t> cuts(TW + 1, 0);

  auto partition = [&](size_t L) {
    for (int t = 0; t < TW; ++t) cuts[t] = (L * static_cast<size_t>(t)) / static_cast<size_t>(TW);
    cuts[TW] = L;
  };

  ProgressMeter pm("Training (Texel)", static_cast<std::size_t>(opts.iterations), opts.progressIntervalMs);

  double bestVal = std::numeric_limits<double>::infinity();
  int patienceLeft = opts.earlyStopPatience;
  std::vector<double> bestEngine = wEngine;
  double bestBias = bias;
  double bestLogScale = logScale;

  // Evaluator used for relinearization.
  lilia::engine::Evaluator evaluator;

  std::vector<size_t> batchIdx;
  batchIdx.reserve(B);

  int executedIters = 0;

  for (int iter = 0; iter < opts.iterations; ++iter) {
    build_batch(batchIdx);
    partition(batchIdx.size());

    for (int t = 0; t < TW; ++t) {
      std::fill(tg[t].begin(), tg[t].end(), 0.0);
      threadLossSum[t] = 0.0;
      threadSumW[t] = 0.0;
    }

    const double lrNow = lr_schedule(opts, iter);
    logScale = clamp_log_scale(logScale);
    const double scale = std::exp(logScale);

    pool.run([&](int t) {
      const size_t s0 = cuts[t], s1 = cuts[t + 1];
      auto& G = tg[t];
      double lossSum = 0.0, sumW = 0.0;

      for (size_t k = s0; k < s1; ++k) {
        const auto& s = samples[batchIdx[k]];
        const float* gptr = s.gradients.data();

        double eval = s.baseEval;
        for (size_t j = 0; j < Pengine; ++j) eval += (wEngine[j] - w0[j]) * static_cast<double>(gptr[j]);
        if (opts.learnBias) eval += bias;

        const double z = std::clamp(eval / scale, -50.0, 50.0);
        const double prob = sigmoid(z);
        const double target = s.result;
        const double w = std::max(0.0f, s.weight);

        const double epsStab = 1e-12;
        lossSum += w * (-(target * std::log(std::max(prob, epsStab)) +
                          (1.0 - target) * std::log(std::max(1.0 - prob, epsStab))));
        sumW += w;

        const double diff = w * (prob - target);  // derivative wrt z
        // engine grads
        for (size_t j = 0; j < Pengine; ++j) G[j] += (diff / scale) * static_cast<double>(gptr[j]);
        if (opts.learnBias) G[static_cast<size_t>(idxs.biasIdx)] += (diff / scale);
        if (opts.learnScale) G[static_cast<size_t>(idxs.scaleIdx)] += -(diff) * (eval / scale);
      }

      threadLossSum[t] = lossSum;
      threadSumW[t] = sumW;
    });

    // Reduce gradients (normalize by total weight).
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

    // Legacy L2 on deltas relative to linpoint.
    if (opts.l2 > 0.0) {
      for (size_t j = 0; j < Pengine; ++j) {
        const double d = (wEngine[j] - w0[j]);
        g[j] += opts.l2 * d;
        loss += 0.5 * opts.l2 * d * d;
      }
    }

    // Gradient clipping (L2 norm).
    if (opts.gradClip > 0.0) {
      double n2 = 0.0;
      for (double x : g) n2 += x * x;
      const double nrm = std::sqrt(n2);
      if (nrm > opts.gradClip && nrm > 0.0) {
        const double sc = opts.gradClip / nrm;
        for (double& x : g) x *= sc;
      }
    }

    // Update (Adam or SGD).
    if (opts.useAdam) {
      b1t *= b1;
      b2t *= b2;
      for (size_t j = 0; j < Ptot; ++j) {
        m[j] = b1 * m[j] + (1.0 - b1) * g[j];
        v[j] = b2 * v[j] + (1.0 - b2) * (g[j] * g[j]);

        const double mhat = m[j] / (1.0 - b1t);
        const double vhat = v[j] / (1.0 - b2t);
        const double step = lrNow * mhat / (std::sqrt(vhat) + eps);

        if (j < Pengine) wEngine[j] -= step;
        else if (static_cast<int>(j) == idxs.biasIdx) bias -= step;
        else if (static_cast<int>(j) == idxs.scaleIdx) logScale -= step;
      }
      // AdamW decoupled decay on engine+bias (not on logScale).
      if (opts.weightDecay > 0.0) {
        const double wd = opts.weightDecay * lrNow;
        for (size_t j = 0; j < Pengine; ++j) wEngine[j] *= (1.0 - wd);
        if (opts.learnBias) bias *= (1.0 - wd);
      }
    } else {
      for (size_t j = 0; j < Pengine; ++j) wEngine[j] -= lrNow * g[j];
      if (opts.learnBias) bias -= lrNow * g[static_cast<size_t>(idxs.biasIdx)];
      if (opts.learnScale) logScale -= lrNow * g[static_cast<size_t>(idxs.scaleIdx)];

      if (opts.weightDecay > 0.0) {
        const double wd = opts.weightDecay * lrNow;
        for (size_t j = 0; j < Pengine; ++j) wEngine[j] *= (1.0 - wd);
        if (opts.learnBias) bias *= (1.0 - wd);
      }
    }

    logScale = clamp_log_scale(logScale);

    // Logging & validation.
    const bool doLog = ((iter + 1) % logEvery == 0) || (iter == opts.iterations - 1);
    const bool doEval =
        (opts.valSplit > 0.0) &&
        (((iter + 1) % evalEvery == 0) || (iter == opts.iterations - 1));

    double vloss = std::numeric_limits<double>::quiet_NaN();
    if (doEval && !valSamples.empty()) {
      vloss = compute_avg_loss_pool(pool, valSamples, wEngine, w0, opts.learnBias ? bias : 0.0, logScale);

      if (vloss + opts.earlyStopDelta < bestVal) {
        bestVal = vloss;
        bestEngine = wEngine;
        bestBias = bias;
        bestLogScale = logScale;
        patienceLeft = opts.earlyStopPatience;
      } else if (opts.earlyStopPatience > 0) {
        --patienceLeft;
      }
    }

    if (doLog) {
      std::cout << "\nIter " << (iter + 1) << "/" << opts.iterations
                << ": loss=" << loss
                << " scale=" << std::exp(logScale);
      if (opts.learnBias) std::cout << " bias=" << bias;
      if (!std::isnan(vloss)) std::cout << " val=" << vloss;
      std::cout << "\n";
    }

    if (csv) {
      csv << (iter + 1) << "," << loss << "," << (std::isnan(vloss) ? 0.0 : vloss) << ","
          << std::exp(logScale) << "," << (opts.learnBias ? bias : 0.0) << "," << lrNow << "\n";
    }

    std::ostringstream status;
    status << std::fixed << std::setprecision(4) << "loss=" << loss;
    if (!std::isnan(vloss)) status << " val=" << vloss;
    status << std::defaultfloat << std::setprecision(3) << " lr=" << lrNow;
    pm.set_status(status.str());

    // Early stopping: if patience reaches zero after eval, restore best and stop.
    if (opts.earlyStopPatience > 0 && doEval && !valSamples.empty() && patienceLeft <= 0) {
      std::cout << "  [early stop] restoring best validation checkpoint\n";
      wEngine = bestEngine;
      bias = bestBias;
      logScale = bestLogScale;
      executedIters = iter + 1;
      pm.add(1);
      break;
    }

    // Optional relinearization (serial).
    if (opts.relinEvery > 0 && ((iter + 1) % opts.relinEvery == 0)) {
      pm.set_status(status.str() + "  [relinearizing]", true);

      std::vector<int> w_int(Pengine);
      for (size_t j = 0; j < Pengine; ++j) w_int[j] = static_cast<int>(std::llround(wEngine[j]));
      engine::set_eval_param_values(w_int);
      w0 = wEngine;

      size_t M = samples.size();
      if (opts.relinFrac > 0.0 && opts.relinFrac < 1.0) {
        M = static_cast<size_t>(std::max<double>(1.0, std::llround(opts.relinFrac * double(samples.size()))));
      }

      std::vector<size_t> idx(samples.size());
      std::iota(idx.begin(), idx.end(), 0);
      if (M < idx.size()) {
        std::mt19937_64 rr(opts.seed ? (opts.seed ^ 0xC2B2AE3D27D4EB4Full) : std::random_device{}());
        std::shuffle(idx.begin(), idx.end(), rr);
      }

      ProgressMeter relPM("Relinearizing samples", M, opts.progressIntervalMs);
      const double wScale = std::exp(logScale);
      for (size_t k = 0; k < M; ++k) {
        const size_t i = idx[k];
        if (samples[i].fen.empty()) continue;
        samples[i] = prepare_sample_with_delta(samples[i].fen, samples[i].result, evaluator, w_int,
                                               entries, opts.relinDelta, wScale);
        relPM.add(1);
      }
      relPM.finish();
      pm.set_status(status.str());
    }

    pm.add(1);
    executedIters = iter + 1;
  }

  pm.finish();
  if (csv) csv.close();

  // If early-stopped, ensure we used best checkpoint already (handled above).
  const double finalLoss = compute_avg_loss_pool(pool, samples, wEngine, w0, opts.learnBias ? bias : 0.0, logScale);

  TrainingResult tr;
  tr.weights = std::move(wEngine);
  tr.finalLoss = finalLoss;
  tr.learnedBias = opts.learnBias ? bias : 0.0;
  tr.learnedScale = std::exp(logScale);
  return tr;
}

void emit_weights(const TrainingResult& result, const std::vector<int>& defaults,
                  const std::span<const engine::EvalParamEntry>& entries, const Options& opts) {
  std::vector<int> tuned;
  tuned.reserve(result.weights.size());
  for (double w : result.weights) tuned.push_back(static_cast<int>(std::llround(w)));

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
    if (!file) throw std::runtime_error("Unable to open weights output file: " + p.string());
    out = &file;
  }

  *out << "# Tuned evaluation parameters\n";
  *out << "# Texel training loss: " << result.finalLoss << "\n";
  *out << "# scale_final=" << result.learnedScale << " bias_final=" << result.learnedBias << "\n";
  *out << "# scale_init=" << opts.logisticScale
       << " lr=" << opts.learningRate
       << " iters=" << opts.iterations
       << " l2=" << opts.l2
       << " weight_decay=" << opts.weightDecay
       << " batch_size=" << opts.batchSize
       << " val_split=" << opts.valSplit
       << " grad_clip=" << opts.gradClip
       << " seed=" << opts.seed
       << " relin_every=" << opts.relinEvery
       << " relin_frac=" << opts.relinFrac
       << " relin_delta=" << opts.relinDelta
       << " autoscale=" << (opts.autoScale ? "yes" : "no")
       << " learn_scale=" << (opts.learnScale ? "yes" : "no")
       << " learn_bias=" << (opts.learnBias ? "yes" : "no")
       << " lr_warmup=" << opts.lrWarmup
       << " lr_cosine=" << opts.lrCosine
       << " adam=" << (opts.useAdam ? "yes" : "no")
       << " train_workers=" << opts.trainWorkers
       << " shuffled=" << (opts.shuffleBeforeTraining ? "yes" : "no")
       << " sample_limit=" << (opts.sampleLimit ? std::to_string(*opts.sampleLimit) : "none")
       << "\n";

  for (size_t i = 0; i < entries.size(); ++i) {
    *out << entries[i].name << "=" << tuned[i]
         << "  # default=" << defaults[i]
         << " tuned=" << result.weights[i] << "\n";
  }
  *out << "# NOTE: bias and scale are not engine parameters; recorded above for calibration.\n";

  if (file) std::cout << "Wrote tuned weights to " << *opts.weightsOutput << "\n";
}

}  // namespace lilia::tools::texel
