# Lilia

**Lilia** is a modern **C++23 chess engine and analysis sandbox**.
The repository contains two main applications:

- **`lilia_engine`** — the standalone chess engine with **UCI** support
- **`lilia_app`** — the desktop sandbox for analysis, testing, and engine tooling

Lilia is developed by **Julian Meyer**, a university student at the **University of Bonn**.

## Overview

This project started as a chess engine and grew into a broader sandbox for engine development and analysis.
The goal is simple:

- build a strong and fast chess engine
- keep the engine code modular and easy to experiment with
- provide a practical desktop app for testing, analysis, and visualization

## What is included

### Engine
Lilia currently uses a classical search architecture with modern pruning and move-ordering ideas, including:

- iterative deepening
- aspiration windows
- principal variation search
- late move reductions
- null move pruning
- futility pruning
- razoring
- SEE-based pruning
- ProbCut
- transposition tables
- history, killer, counter-move, and continuation heuristics

### Evaluation
The engine uses a handcrafted evaluation with material, mobility, structure, and positional terms.
An NNUE-based evaluation is planned for the future.

### App
The sandbox app is built with **SFML** and is meant for:

- local analysis
- engine testing
- visual debugging
- experimentation with engine behavior and tooling

## Project layout

```text
.
├── apps/                  # application entry points
│   ├── lilia_engine/      # UCI engine executable
│   └── lilia_app/         # sandbox application
├── assets/                # runtime assets for the app
├── include/lilia/         # public headers
├── src/lilia/
│   ├── chess/             # reusable chess domain logic
│   ├── engine/            # search, eval, TT, engine internals
│   ├── protocol/uci/      # UCI protocol implementation
│   ├── app/               # app/session layer
│   ├── controller/        # app control flow
│   └── view/              # SFML UI
├── tools/                 # developer tools such as texel tuning
└── tests/                 # tests
````

## Requirements

* **CMake 3.21+**
* a compiler with **C++23** support

Supported platforms:

* **Windows** — MSVC / Visual Studio 2022 recommended
* **Linux** — GCC or Clang
* **macOS** — AppleClang

## Build

### Fastest option

Use the provided Makefile wrapper:

```bash
make engine   # build engine only
make app      # build app
make all      # build everything
make clean    # remove build folders
```

### Engine only

```bash
make engine
```

### App

```bash
make app
```

### macOS universal build

```bash
make app UNIVERSAL2=ON
```

## Direct CMake build

### Engine only

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DLILIA_BUILD_UI=OFF
cmake --build build --target lilia_engine
```

### App + engine

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DLILIA_BUILD_UI=ON
cmake --build build --target lilia_app lilia_engine
```

### Windows with Visual Studio

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DLILIA_BUILD_UI=ON
cmake --build build --config Release --target lilia_app lilia_engine
```

## Output

Build artifacts are placed in the build directory, typically under:

```text
build/bin/
```

The app also needs the `assets/` directory beside the executable.
The build system copies runtime assets automatically when needed.

## Run

### UCI engine

```bash
./lilia_engine
```

You can use `lilia_engine` in any UCI-compatible chess GUI.

### Sandbox app

Run `lilia_app` after building to launch the desktop interface.

## Releases

Prebuilt binaries are available on the project’s **GitHub Releases** page.

## Notes

This repository combines engine development and desktop tooling in one place.
The long-term direction is to keep the chess core reusable while improving both:

* engine strength
* analysis and debugging tools

## Acknowledgements

* **SFML** for graphics, windowing, and audio
