# Texel Tuner (Refactored)

This is a modular, cross-platform refactor of the original monolithic `texel_tuner` source.

## Key changes vs the original single-file version

- Split into focused translation units (UCI process, dataset I/O, prepared cache, trainer, CLI).
- Fixed **incorrect clamping** in sigmoid path: we now clamp **eval/scale** to [-50, 50] (logistic-stable) rather than using ±500 and a separate sigmoid clamp.
- Fixed **undefined behavior**: no `const_cast` mutation of `Options`.
- More robust UCI subprocess termination on POSIX (SIGTERM/SIGKILL fallback).
- Dataset generation drops aborted/non-terminal games to avoid label noise.

## Files

- `include/lilia/tools/texel/common.hpp`, `src/common.cpp`: default paths and Stockfish discovery.
- `include/lilia/tools/texel/options.hpp`, `src/options.cpp`: CLI parsing.
- `include/lilia/tools/texel/progress.hpp`: progress meter.
- `include/lilia/tools/texel/worker_pool.hpp`: fixed thread pool.
- `include/lilia/tools/texel/uci_engine.hpp`, `src/uci_engine.cpp`: persistent UCI engine wrapper.
- `include/lilia/tools/texel/dataset.hpp`, `src/dataset.cpp`: self-play generation and dataset I/O.
- `include/lilia/tools/texel/prepared_cache.hpp`, `src/prepared_cache.cpp`: prepared cache v1/v2/v3 I/O (writes v3).
- `include/lilia/tools/texel/texel_trainer.hpp`, `src/texel_trainer.cpp`: preparation + optimizer + emit weights.
- `src/main.cpp`: orchestration.

## Integration

This folder contains a standalone `CMakeLists.txt`. You must:
1. Edit `LILIA_TARGET` to the name of your engine target.
2. Add `add_subdirectory(path/to/this/dir)` from your root CMake.

## Compatibility notes

- Prepared cache compatibility is enforced via:
  - param count
  - logistic scale
  - defaults hash (parameter names + default values + delta step)
  - checksum (v3 only)
- Relinearization requires cache v2/v3 (FEN stored). For v1 caches, relinearization is skipped.
