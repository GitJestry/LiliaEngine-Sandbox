#include <iostream>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>
#include <algorithm>

#include "lilia/engine/engine.hpp"
#include "lilia/engine/eval.hpp"
#include "lilia/tools/texel/common.hpp"
#include "lilia/tools/texel/dataset.hpp"
#include "lilia/tools/texel/options.hpp"
#include "lilia/tools/texel/prepared_cache.hpp"
#include "lilia/tools/texel/texel_trainer.hpp"

int main(int argc, char **argv)
{
  using namespace lilia::tools::texel;

  try
  {
    lilia::engine::Engine::init();

    const DefaultPaths defaults = compute_default_paths(argc > 0 ? argv[0] : nullptr);
    Options opts = parse_args(argc, argv, defaults);

    if (opts.generateData && opts.stockfishPath.empty())
    {
      throw std::runtime_error(
          "Stockfish executable not found. Place it next to texel_tuner, under tools/texel, or pass --stockfish <path>.");
    }

    std::cout << "Dataset path: " << opts.dataFile << "\n";
    if (opts.weightsOutput)
      std::cout << "Weights output path: " << *opts.weightsOutput << "\n";

    if (opts.generateData)
    {
      std::cout << "Using Stockfish at " << opts.stockfishPath << "\n";
      std::cout << "Threads=" << opts.threads
                << " MultiPV=" << opts.multipv
                << " temp(cp)=" << opts.tempCp
                << (opts.movetimeMs > 0
                        ? (" movetime=" + std::to_string(opts.movetimeMs) +
                           "ms jitter=" + std::to_string(opts.movetimeJitterMs) + "ms")
                        : (" depth=" + std::to_string(opts.depth)))
                << (opts.skillLevel ? (" skill=" + std::to_string(*opts.skillLevel)) : "")
                << (opts.elo ? (" elo=" + std::to_string(*opts.elo)) : "")
                << (opts.contempt ? (" contempt=" + std::to_string(*opts.contempt)) : "")
                << " gen_workers=" << opts.genWorkers << "\n";

      auto samples = generate_samples_parallel(opts);
      if (samples.empty())
      {
        std::cerr << "No samples generated.\n";
      }
      else
      {
        write_dataset(samples, opts.dataFile);
      }
    }

    if (opts.tune)
    {
      auto rawSamples = read_dataset(opts.dataFile);
      if (rawSamples.empty())
        throw std::runtime_error("Dataset is empty: " + opts.dataFile);

      lilia::engine::Evaluator evaluator;
      lilia::engine::reset_eval_params();

      auto defaultsVals = lilia::engine::get_eval_param_values();
      auto entriesSpan = lilia::engine::eval_param_entries();

      std::vector<PreparedSample> prepared;
      std::vector<PreparedSample> valPrepared;

      // Cache compatibility hash.
      const uint64_t defHash = hash_defaults(entriesSpan, defaultsVals, opts.relinDelta, 0);

      bool loadedFromCache = false;
      bool cacheHasFen = false;

      if (opts.preparedCache && opts.loadPreparedIfExists)
      {
        loadedFromCache = load_prepared_cache(*opts.preparedCache, prepared,
                                              static_cast<uint32_t>(entriesSpan.size()),
                                              opts.logisticScale, defHash, opts.relinDelta,
                                              cacheHasFen);
        if (loadedFromCache)
        {
          std::cout << "Loaded prepared samples from cache: " << *opts.preparedCache
                    << " (fen=" << (cacheHasFen ? "yes" : "no") << ")\n";
        }
      }

      if (!loadedFromCache)
      {
        prepared = prepare_samples(rawSamples, evaluator, defaultsVals, entriesSpan, opts);
        std::cout << "Prepared " << prepared.size() << " samples for tuning\n";

        if (opts.preparedCache && opts.savePrepared)
        {
          if (save_prepared_cache(*opts.preparedCache, prepared,
                                  static_cast<uint32_t>(entriesSpan.size()),
                                  opts.logisticScale, defHash, opts.relinDelta))
          {
            std::cout << "Saved prepared cache to " << *opts.preparedCache << "\n";
          }
          else
          {
            std::cout << "Warning: failed to save prepared cache to " << *opts.preparedCache << "\n";
          }
        }
      }
      else if (opts.relinEvery > 0 && !cacheHasFen)
      {
        std::cout << "Note: cache has no FEN (v1). Relinearization is effectively disabled.\n";
      }

      // Train/val split (deterministic with seed).
      if (opts.valSplit > 0.0 && prepared.size() > 10)
      {
        std::mt19937_64 rng(opts.seed ? (opts.seed ^ 0x41C64E6DA3BC0074ull) : std::random_device{}());
        std::shuffle(prepared.begin(), prepared.end(), rng);

        size_t nval = static_cast<size_t>(std::round(opts.valSplit * prepared.size()));
        nval = std::min(nval, prepared.size() / 2);
        valPrepared.insert(valPrepared.end(), prepared.begin(), prepared.begin() + nval);
        prepared.erase(prepared.begin(), prepared.begin() + nval);

        std::cout << "Train samples: " << prepared.size() << ", Val samples: " << valPrepared.size() << "\n";
      }

      auto result = train_texel(prepared, valPrepared, defaultsVals, entriesSpan, opts);
      emit_weights(result, defaultsVals, entriesSpan, opts);
    }

    return 0;
  }
  catch (const std::exception &ex)
  {
    std::cerr << "Error: " << ex.what() << "\n";
    return 1;
  }
}
