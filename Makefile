# =================================================
# Lilia: cross-platform Makefile wrapper for CMake
#
# Usage:
#   make engine     # build CLI engine only (no UI/SFML)
#   make texel      # build texel_tuner only (no UI/SFML)
#   make tools      # build lilia_engine + texel_tuner
#   make app        # build UI app (downloads/builds SFML) + lilia_engine + texel_tuner
#   make all        # same as app
#   make clean      # remove build dirs
#
# Optional overrides:
#   make tools BUILD_TYPE=Release NATIVE=ON FAST_MATH=ON LTO=ON
#   make all UNIVERSAL2=ON         (macOS only; disables NATIVE via CMake)
#   make app JOBS=12
# =================================================

# ---- Detect platform ----
UNAME_S := $(shell uname -s 2>/dev/null || echo Unknown)

IS_MINGW := $(findstring MINGW,$(UNAME_S))
IS_MSYS  := $(findstring MSYS,$(UNAME_S))
IS_CYGWIN:= $(findstring CYGWIN,$(UNAME_S))

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

# ---- Generator selection (Make-based) ----
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
BUILD_TYPE ?= Release
NATIVE     ?= ON
FAST_MATH  ?= ON
LTO        ?= ON
UNIVERSAL2 ?= OFF
TEXEL      ?= ON

# ---- Build directories ----
BUILD_DIR_ENGINE := build-engine
BUILD_DIR_UI     := build-ui

# ---- Common CMake configure flags ----
CMAKE_COMMON_FLAGS = -G "$(CMAKE_GEN)"   -DCMAKE_BUILD_TYPE=$(BUILD_TYPE)   -DLILIA_NATIVE=$(NATIVE)   -DLILIA_FAST_MATH=$(FAST_MATH)   -DLILIA_LTO=$(LTO)   -DLILIA_UNIVERSAL2=$(UNIVERSAL2)   -DLILIA_BUILD_TEXEL=$(TEXEL)

.PHONY: help engine texel tools app all clean configure-engine configure-ui

help:
	@echo "Platform: $(PLATFORM)"
	@echo ""
	@echo "Targets:"
	@echo "  make engine   -> configure+build lilia_engine (no UI/SFML)"
	@echo "  make texel    -> configure+build texel_tuner (no UI/SFML)"
	@echo "  make tools    -> configure+build lilia_engine + texel_tuner"
	@echo "  make app      -> configure+build lilia_app (+ engine + texel)"
	@echo "  make all      -> alias for app"
	@echo "  make clean    -> remove build dirs"
	@echo ""
	@echo "Overrides:"
	@echo "  BUILD_TYPE=Release|RelWithDebInfo|Debug"
	@echo "  NATIVE=ON/OFF FAST_MATH=ON/OFF LTO=ON/OFF"
	@echo "  UNIVERSAL2=ON/OFF (macOS only; disables NATIVE inside CMake)"
	@echo "  TEXEL=ON/OFF"
	@echo "  JOBS=<n>"

configure-engine:
	cmake -S . -B "$(BUILD_DIR_ENGINE)" $(CMAKE_COMMON_FLAGS) \
	  -DLILIA_BUILD_UI=OFF \
	  -DLILIA_PGO_GENERATE=OFF -DLILIA_PGO_USE=OFF

configure-ui:
	cmake -S . -B "$(BUILD_DIR_UI)" $(CMAKE_COMMON_FLAGS) \
	  -DLILIA_BUILD_UI=ON \
	  -DLILIA_PGO_GENERATE=OFF -DLILIA_PGO_USE=OFF

engine: configure-engine
	cmake --build "$(BUILD_DIR_ENGINE)" --target lilia_engine -- -j$(JOBS)

texel: configure-engine
	cmake --build "$(BUILD_DIR_ENGINE)" --target texel_tuner -- -j$(JOBS)

tools: configure-engine
	cmake --build "$(BUILD_DIR_ENGINE)" --target lilia_engine texel_tuner -- -j$(JOBS)

app: configure-ui
	cmake --build "$(BUILD_DIR_UI)" --target lilia_engine texel_tuner lilia_app -- -j$(JOBS)

all: app

clean:
	@cmake -E rm -rf "$(BUILD_DIR_ENGINE)" "$(BUILD_DIR_UI)"
