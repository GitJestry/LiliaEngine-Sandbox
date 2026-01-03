# Lilia Chess Engine

**Lilia** is a cross-platform chess engine and sandbox written in modern **C++23**. This repository contains:

* **`lilia_engine`** — the standalone engine core with **UCI** support
* **`lilia_app`** — an optional **SFML-based GUI** (sandbox + visual tooling)

Prebuilt binaries are available in the GitHub **Releases**.

* Download: Releases tab (Windows, Linux, macOS artifacts)
  [https://github.com/JustAnoAim/Lilia/releases](https://github.com/JustAnoAim/Lilia/releases)

## Project Goals

Lilia is built as a playground for experimenting with modern chess-engine ideas and analysis tooling. The codebase is modular by design, so search/evaluation components can be swapped with minimal friction.

The long-term direction is to provide analysis tooling comparable to chess.com and extend beyond it with deeper visualization and engine debugging utilities.

---

## Search

The current search is a classic **Negamax with Alpha-Beta pruning**, augmented with modern heuristics and pruning concepts:

* Iterative deepening, aspiration windows, principal variation search
* Late Move Reductions (LMR) with tuned reduction tables
* Null-move pruning, razoring, multi-stage futility pruning
* SEE pruning, ProbCut, light check extensions
* Move ordering: TT, killer moves, quiet/capture histories, counter moves, history pruning

---

## Evaluation

The handcrafted evaluator combines material, mobility, and structural heuristics to produce a more “human-like” style.
Planned: integrate an **NNUE** backend (industry standard) for evaluation.

---

## Transposition Table

The engine currently uses **TT5**: a compact **16-byte entry** table with two-stage key verification and generation-based aging for fast lookups.

---

## Project Structure

```
.
├── assets/         # textures/audio/fonts for the GUI runtime
├── examples/       # entry points / integration examples
├── include/
│   └── lilia/      # public headers
└── src/lilia/
    ├── app/        # GUI front-end logic
    ├── bot/        # engine/bot integration
    ├── controller/ # MVC controllers (GUI)
    ├── engine/     # search, eval, TT
    ├── model/      # board state & move generation
    ├── uci/        # UCI protocol implementation
    └── view/       # SFML rendering + UI
```

---

## Requirements

### Common

* **CMake ≥ 3.21**
* A C++ compiler with **C++23** support:

  * Windows: MSVC (VS 2022) or Clang/MinGW
  * Linux: GCC or Clang
  * macOS: AppleClang

### GUI (optional)

`lilia_app` uses **SFML**. The repository builds SFML via **FetchContent** when the UI is enabled, but your system still needs typical audio/windowing dependencies on Linux/macOS.

---

## Building

Lilia can be built in two ways:

1. **CMake directly** (recommended for CI / IDE workflows)
2. **Makefile wrapper** (convenient developer workflow on macOS/Linux and MSYS2/MinGW environments)

### Build Targets

* `lilia_engine` (UCI engine core)
* `lilia_app` (GUI sandbox; optional)
* `texel_tuner` (tooling target)

> By default the UI is **OFF** in CMake. Enable it with `-DLILIA_BUILD_UI=ON`.

---

## Building with the Makefile Wrapper

The repo ships a cross-platform Makefile wrapper around CMake.

### Linux / macOS

Build engine + app:

```bash
make all
```

Build only the engine (no UI, no SFML):

```bash
make engine
```

Build only the GUI app:

```bash
make app
```

Clean:

```bash
make clean
```

### macOS Universal Binary (universal2)

To build a universal binary (arm64 + x86_64), use:

```bash
make all UNIVERSAL2=ON
```

Notes:

* Universal builds automatically disable native CPU flags (`-march=native`) since they do not apply to universal2.
* Output is produced in `build-ui/bin/` (for UI builds) and `build-engine/bin/` (engine-only builds).

### Windows (Makefile wrapper)

The Makefile wrapper works on Windows only in Unix-like environments such as **MSYS2 / MinGW / Git Bash / Cygwin**.
For native Windows builds, prefer the MSVC workflow below.

---

## Building with CMake (Recommended / CI)

### Windows (MSVC / Visual Studio)

Configure:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 `
  -DCMAKE_BUILD_TYPE=Release `
  -DLILIA_BUILD_UI=ON `
  -DLILIA_NATIVE=ON -DLILIA_FAST_MATH=ON -DLILIA_LTO=ON
```

Build:

```powershell
cmake --build build --config Release --target lilia_app lilia_engine
```

Artifacts typically land in:

* `build/bin/Release/` (multi-config layout)

### Linux (Ninja or Unix Makefiles)

```bash
cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DLILIA_BUILD_UI=ON \
  -DLILIA_NATIVE=ON -DLILIA_FAST_MATH=ON -DLILIA_LTO=ON

cmake --build build --target lilia_app lilia_engine
```

### macOS (AppleClang)

```bash
cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DLILIA_BUILD_UI=ON \
  -DLILIA_NATIVE=ON -DLILIA_FAST_MATH=ON -DLILIA_LTO=ON

cmake --build build --target lilia_app lilia_engine
```

Universal2 via CMake:

```bash
cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DLILIA_BUILD_UI=ON \
  -DLILIA_UNIVERSAL2=ON \
  -DLILIA_FAST_MATH=ON -DLILIA_LTO=ON

cmake --build build --target lilia_app lilia_engine
```

---

## Runtime Assets

The GUI requires the `assets/` folder next to the executable.
The build system copies assets automatically into the target output directory for convenience.

---

## Using the Engine (UCI)

`lilia_engine` implements the [UCI protocol](https://en.wikipedia.org/wiki/Universal_Chess_Interface) and can be used in any UCI-compatible chess GUI.

Example (CLI):

```bash
./lilia_engine
```

For minimal integration examples, see:

* `examples/main.cpp`

---

## Future Work

* Integrate NNUE evaluation backend
* Continue exploring pruning/search improvements
* Expand visual analysis tooling and debugging features

---

## Acknowledgements

* Graphics, windowing, and audio: [SFML](https://www.sfml-dev.org/)

---
