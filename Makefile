# =================================================
# Lilia: cross-platform Makefile wrapper for CMake
#
# Architecture:
#   - chess            standalone library
#   - engine           depends on chess
#   - protocol/uci     depends on engine
#   - app              depends on chess + engine
#   - tools/texel      outside src/include lilia tree
#
# Usage:
#   make engine        # build lilia_engine
#   make texel         # build texel_tuner (requires TEXEL=ON)
#   make tools         # build lilia_engine + texel_tuner (if TEXEL=ON)
#   make app           # build lilia_app + bundled lilia_engine (+ texel if TEXEL=ON)
#   make test-chess    # build/run chess tests
#   make test-engine   # build/run engine tests
#   make test-app      # build/run app tests
#   make test-all      # build/run all tests
#   make all           # alias for app
#   make clean         # remove build dirs
# =================================================

# ---- Detect platform ----
UNAME_S := $(shell uname -s 2>/dev/null || echo Unknown)

IS_MINGW  := $(findstring MINGW,$(UNAME_S))
IS_MSYS   := $(findstring MSYS,$(UNAME_S))
IS_CYGWIN := $(findstring CYGWIN,$(UNAME_S))

ifeq ($(UNAME_S),Darwin)
  PLATFORM := macOS
else ifneq (,$(IS_MINGW))
  PLATFORM := Windows
else ifneq (,$(IS_MSYS))
  PLATFORM := Windows
else ifneq (,$(IS_CYGWIN))
  PLATFORM := Windows
else
  PLATFORM := Linux
endif

# ---- Generator selection ----
ifeq ($(PLATFORM),Windows)
  CMAKE_GEN ?= MinGW Makefiles
else
  CMAKE_GEN ?= Unix Makefiles
endif

# ---- Parallel jobs ----
ifeq ($(PLATFORM),Windows)
  JOBS ?= $(NUMBER_OF_PROCESSORS)
else
  JOBS ?= $(shell nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)
endif

# ---- Build toggles / defaults ----
BUILD_TYPE     ?= Release
NATIVE         ?= ON
FAST_MATH      ?= ON
LTO            ?= ON
UNIVERSAL2     ?= OFF
TEXEL          ?= OFF
TESTS          ?= OFF
BUNDLE_ENGINES ?= ON

# ---- Build directories ----
BUILD_DIR_CORE := build-core
BUILD_DIR_APP  := build-app

# ---- Common CMake flags ----
CMAKE_COMMON_FLAGS = \
	-G "$(CMAKE_GEN)" \
	-DCMAKE_BUILD_TYPE=$(BUILD_TYPE) \
	-DLILIA_NATIVE=$(NATIVE) \
	-DLILIA_FAST_MATH=$(FAST_MATH) \
	-DLILIA_LTO=$(LTO) \
	-DLILIA_UNIVERSAL2=$(UNIVERSAL2) \
	-DLILIA_BUILD_TEXEL=$(TEXEL) \
	-DLILIA_BUILD_TESTS=$(TESTS) \
	-DLILIA_BUNDLE_LILIA_ENGINE=$(BUNDLE_ENGINES)

# ---- Target groups ----
ifeq ($(TEXEL),ON)
  TOOLS_TARGETS := lilia_engine texel_tuner
  APP_TARGETS   := lilia_engine texel_tuner lilia_app
else
  TOOLS_TARGETS := lilia_engine
  APP_TARGETS   := lilia_engine lilia_app
endif

.PHONY: help \
        configure-core configure-app \
        engine texel tools app all clean \
        test-chess test-engine test-app test-all

help:
	@echo "Platform: $(PLATFORM)"
	@echo ""
	@echo "Targets:"
	@echo "  make engine      -> configure+build lilia_engine"
	@echo "  make texel       -> configure+build texel_tuner (requires TEXEL=ON)"
	@echo "  make tools       -> configure+build lilia_engine (+ texel_tuner if TEXEL=ON)"
	@echo "  make app         -> configure+build lilia_app (+ lilia_engine, + texel if TEXEL=ON)"
	@echo "  make test-chess  -> configure+build+run chess tests"
	@echo "  make test-engine -> configure+build+run engine tests"
	@echo "  make test-app    -> configure+build+run app tests"
	@echo "  make test-all    -> run all test groups"
	@echo "  make clean       -> remove build dirs"
	@echo ""
	@echo "Overrides:"
	@echo "  BUILD_TYPE=Release|RelWithDebInfo|Debug"
	@echo "  NATIVE=ON/OFF FAST_MATH=ON/OFF LTO=ON/OFF"
	@echo "  UNIVERSAL2=ON/OFF   (macOS only; disables NATIVE inside CMake)"
	@echo "  TEXEL=ON/OFF"
	@echo "  TESTS=ON/OFF"
	@echo "  BUNDLE_ENGINES=ON/OFF"
	@echo "  JOBS=<n>"

configure-core:
	cmake -S . -B "$(BUILD_DIR_CORE)" $(CMAKE_COMMON_FLAGS) \
	  -DLILIA_BUILD_APP=OFF \
	  -DLILIA_PGO_GENERATE=OFF \
	  -DLILIA_PGO_USE=OFF

configure-app:
	cmake -S . -B "$(BUILD_DIR_APP)" $(CMAKE_COMMON_FLAGS) \
	  -DLILIA_BUILD_APP=ON \
	  -DLILIA_PGO_GENERATE=OFF \
	  -DLILIA_PGO_USE=OFF

engine: configure-core
	cmake --build "$(BUILD_DIR_CORE)" --target lilia_engine -- -j$(JOBS)

texel:
ifeq ($(TEXEL),ON)
	$(MAKE) configure-core
	cmake --build "$(BUILD_DIR_CORE)" --target texel_tuner -- -j$(JOBS)
else
	@echo "texel_tuner is disabled because TEXEL=OFF"
	@exit 1
endif

tools: configure-core
	cmake --build "$(BUILD_DIR_CORE)" --target $(TOOLS_TARGETS) -- -j$(JOBS)

app: configure-app
	cmake --build "$(BUILD_DIR_APP)" --target $(APP_TARGETS) -- -j$(JOBS)

all: app

test-chess:
	$(MAKE) configure-core TESTS=ON
	cmake --build "$(BUILD_DIR_CORE)" --target chess_tests -- -j$(JOBS)
	ctest --test-dir "$(BUILD_DIR_CORE)" -R "^chess_tests$$" --output-on-failure

test-engine:
	$(MAKE) configure-core TESTS=ON
	cmake --build "$(BUILD_DIR_CORE)" --target engine_tests -- -j$(JOBS)
	ctest --test-dir "$(BUILD_DIR_CORE)" -R "^engine_tests$$" --output-on-failure

test-app:
	$(MAKE) configure-app TESTS=ON
	cmake --build "$(BUILD_DIR_APP)" --target app_tests -- -j$(JOBS)
	ctest --test-dir "$(BUILD_DIR_APP)" -R "^app_tests$$" --output-on-failure

test-all: test-chess test-engine test-app

clean:
	@cmake -E rm -rf "$(BUILD_DIR_CORE)" "$(BUILD_DIR_APP)"
