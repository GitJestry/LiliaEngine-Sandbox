# -------------------------------------------------
# Project & toolchain
# -------------------------------------------------
cmake_minimum_required(VERSION 3.21) # for $<TARGET_RUNTIME_DLLS:...>
project(ChessEngine_Lilia VERSION 1.0 LANGUAGES CXX)

# C++ standard
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Default build type for single-config generators (Make/Ninja)
if (NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# -------------------------------------------------
# Performance toggles (ON by default)
# -------------------------------------------------
option(LILIA_NATIVE       "Enable native CPU optimizations (-march=native or /arch:AVX2)" ON)
option(LILIA_FAST_MATH    "Enable fast-math in Release" ON)
option(LILIA_LTO          "Enable Link-Time Optimization/IPO in Release" ON)
option(LILIA_PGO_GENERATE "Build with PGO generate (instrumentation)" OFF)
option(LILIA_PGO_USE      "Build with PGO use (optimized by profiles)" OFF)

option(LILIA_BUILD_UI     "Build the graphical UI application" OFF)
option(LILIA_BUILD_TEXEL  "Build the Texel tuner tool" ON)

# macOS: optional universal2 build (arm64 + x86_64)
option(LILIA_UNIVERSAL2   "Build universal2 binary on macOS (arm64 + x86_64)" OFF)
if(APPLE AND LILIA_UNIVERSAL2)
  set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
  # Native CPU flags do not make sense for universal builds
  set(LILIA_NATIVE OFF CACHE BOOL "" FORCE)
endif()

# Clang/AppleClang PGO: path to merged .profdata when using profiles
set(LILIA_PGO_PROFDATA "" CACHE FILEPATH "Path to merged LLVM .profdata for Clang/AppleClang PGO use")

# -------------------------------------------------
# Texel: Stockfish provisioning config (used by src/lilia/tools/texel)
# -------------------------------------------------
option(LILIA_TEXEL_FETCH_STOCKFISH "Auto-download a Stockfish binary for texel_tuner when not bundled" ON)
set(LILIA_TEXEL_STOCKFISH_URL_BASE "https://github.com/official-stockfish/Stockfish/releases/latest/download"
    CACHE STRING "Base URL for Stockfish release assets")
set(LILIA_TEXEL_STOCKFISH_ASSET "" CACHE STRING "Override Stockfish asset filename (e.g. stockfish-macos-m1-apple-silicon.tar)")

# -------------------------------------------------
# Runtime assets path (shipped next to the exe)
# -------------------------------------------------
set(LILIA_ASSETS_DIR "${PROJECT_SOURCE_DIR}/assets"
    CACHE PATH "Directory with runtime assets (textures, sounds, fonts, etc.)")

# -------------------------------------------------
# Threads
# -------------------------------------------------
find_package(Threads REQUIRED)

# -------------------------------------------------
# Sources
# -------------------------------------------------
file(GLOB_RECURSE MODEL_FILES CONFIGURE_DEPENDS
  "${PROJECT_SOURCE_DIR}/src/lilia/model/*.cpp"
)
file(GLOB_RECURSE ENGINE_FILES CONFIGURE_DEPENDS
  "${PROJECT_SOURCE_DIR}/src/lilia/engine/*.cpp"
)
file(GLOB_RECURSE UCI_FILES CONFIGURE_DEPENDS
  "${PROJECT_SOURCE_DIR}/src/lilia/uci/*.cpp"
)
file(GLOB_RECURSE CONTROLLER_FILES CONFIGURE_DEPENDS
  "${PROJECT_SOURCE_DIR}/src/lilia/controller/*.cpp"
)
file(GLOB_RECURSE UI_FILES CONFIGURE_DEPENDS
  "${PROJECT_SOURCE_DIR}/src/lilia/view/*.cpp"
)
file(GLOB_RECURSE APP_FILES CONFIGURE_DEPENDS
  "${PROJECT_SOURCE_DIR}/src/lilia/app/*.cpp"
)

set(CORE_FILES
  ${MODEL_FILES}
  ${ENGINE_FILES}
  ${UCI_FILES}
)

# -------------------------------------------------
# Helper: performance flags per target
# -------------------------------------------------
function(lilia_set_perf_flags tgt)
  if (MSVC)
    target_compile_options(${tgt} PRIVATE
      /MP
      $<$<CONFIG:Release>:/O2 /Ot /Oi /DNDEBUG /Gy /Gw>
      $<$<AND:$<CONFIG:Release>,$<BOOL:${LILIA_FAST_MATH}>>:/fp:fast>
      $<$<AND:$<CONFIG:Release>,$<BOOL:${LILIA_NATIVE}>>:/arch:AVX2>
    )
    if (LILIA_LTO)
      include(CheckIPOSupported)
      check_ipo_supported(RESULT _ipo_ok OUTPUT _ipo_out)
      if (_ipo_ok)
        set_property(TARGET ${tgt} PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
      else()
        message(STATUS "IPO/LTO not supported: ${_ipo_out}")
      endif()
    endif()
    if (LILIA_PGO_GENERATE)
      target_link_options(${tgt} PRIVATE $<$<CONFIG:Release>:/GENPROFILE>)
    endif()
    if (LILIA_PGO_USE)
      target_link_options(${tgt} PRIVATE $<$<CONFIG:Release>:/USEPROFILE>)
    endif()
  else()
    if (CMAKE_CXX_COMPILER_ID MATCHES "GNU")
      set(IS_GCC TRUE)
      set(IS_CLANG FALSE)
    elseif (CMAKE_CXX_COMPILER_ID MATCHES "Clang|AppleClang")
      set(IS_GCC FALSE)
      set(IS_CLANG TRUE)
    else()
      set(IS_GCC FALSE)
      set(IS_CLANG FALSE)
    endif()

    target_compile_options(${tgt} PRIVATE
      $<$<CONFIG:Release>:-O3 -DNDEBUG -fomit-frame-pointer>
      $<$<AND:$<CONFIG:Release>,$<BOOL:${LILIA_NATIVE}>>:-march=native -mtune=native>
      $<$<AND:$<CONFIG:Release>,$<BOOL:${LILIA_FAST_MATH}>>:-ffast-math>
      $<$<AND:$<CONFIG:Release>,$<BOOL:${IS_GCC}>>:-fno-plt>
      $<$<AND:$<CONFIG:Release>,$<BOOL:${IS_CLANG}>>:-fstrict-aliasing>
      $<$<CONFIG:Release>:-fvisibility=hidden -fvisibility-inlines-hidden>
    )

    if (LILIA_LTO)
      target_link_options(${tgt} PRIVATE $<$<CONFIG:Release>:-flto>)
      if (IS_GCC)
        target_link_options(${tgt} PRIVATE -Wl,-O2)
      endif()
    endif()

    # PGO
    if (LILIA_PGO_GENERATE)
      if (IS_CLANG)
        target_compile_options(${tgt} PRIVATE $<$<CONFIG:Release>:-fprofile-instr-generate>)
        target_link_options(${tgt}    PRIVATE $<$<CONFIG:Release>:-fprofile-instr-generate>)
      else()
        target_compile_options(${tgt} PRIVATE $<$<CONFIG:Release>:-fprofile-generate>)
        target_link_options(${tgt}    PRIVATE $<$<CONFIG:Release>:-fprofile-generate>)
      endif()
    endif()

    if (LILIA_PGO_USE)
      if (IS_CLANG)
        if (NOT LILIA_PGO_PROFDATA)
          message(FATAL_ERROR "LILIA_PGO_USE is ON, but LILIA_PGO_PROFDATA is empty (need merged .profdata).")
        endif()
        target_compile_options(${tgt} PRIVATE $<$<CONFIG:Release>:-fprofile-instr-use=${LILIA_PGO_PROFDATA}>)
        target_link_options(${tgt}    PRIVATE $<$<CONFIG:Release>:-fprofile-instr-use=${LILIA_PGO_PROFDATA}>)
      else()
        target_compile_options(${tgt} PRIVATE $<$<CONFIG:Release>:-fprofile-use -fprofile-correction>)
        target_link_options(${tgt}    PRIVATE $<$<CONFIG:Release>:-fprofile-use>)
      endif()
    endif()
  endif()
endfunction()

# -------------------------------------------------
# Core library
# -------------------------------------------------
add_library(lilia_core STATIC ${CORE_FILES})
target_compile_definitions(lilia_core PUBLIC
  LILIA_ENGINE
  $<$<PLATFORM_ID:Windows>:NOMINMAX>
)
target_include_directories(lilia_core PUBLIC
  ${PROJECT_SOURCE_DIR}/include
  ${PROJECT_SOURCE_DIR}/include/lilia
)
target_link_libraries(lilia_core PUBLIC Threads::Threads)

lilia_set_perf_flags(lilia_core)

# -------------------------------------------------
# CLI engine target
# -------------------------------------------------
add_executable(lilia_engine examples/main.cpp)
target_link_libraries(lilia_engine PRIVATE lilia_core)
lilia_set_perf_flags(lilia_engine)

# -------------------------------------------------
# Texel tuner (optional)
# -------------------------------------------------
if (LILIA_BUILD_TEXEL)
  # Tell the texel subproject which core target to link to.
  set(LILIA_TARGET lilia_core)
  add_subdirectory(src/lilia/tools/texel)
endif()

# -------------------------------------------------
# UI target (optional)
# -------------------------------------------------
if(LILIA_BUILD_UI)
  add_executable(lilia_app
    examples/main.cpp
    ${UI_FILES}
    ${APP_FILES}
    ${CONTROLLER_FILES}
  )
  target_compile_definitions(lilia_app PRIVATE
    LILIA_UI
    $<$<PLATFORM_ID:Windows>:NOMINMAX>
  )
  target_include_directories(lilia_app PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/include/lilia
  )
  target_link_libraries(lilia_app PRIVATE lilia_core)

  # macOS: build as .app bundle (optional but convenient)
  if(APPLE)
    set_target_properties(lilia_app PROPERTIES MACOSX_BUNDLE TRUE)
  endif()

  # -------------------------------------------------
  # SFML for UI (shared, built from source)
  # -------------------------------------------------
  include(FetchContent)

  # Minimize global side effects: temporarily force BUILD_SHARED_LIBS for SFML only.
  if(DEFINED BUILD_SHARED_LIBS)
    set(_lilia_old_build_shared_libs "${BUILD_SHARED_LIBS}")
    set(_lilia_had_build_shared_libs TRUE)
  else()
    set(_lilia_had_build_shared_libs FALSE)
  endif()

  set(BUILD_SHARED_LIBS ON CACHE BOOL "" FORCE)

  # Keep SFML minimal
  set(SFML_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(SFML_BUILD_TESTING  OFF CACHE BOOL "" FORCE)
  set(SFML_USE_STATIC_STD_LIBS OFF CACHE BOOL "" FORCE)

  FetchContent_Declare(
    SFML
    GIT_REPOSITORY https://github.com/SFML/SFML.git
    GIT_TAG 2.6.x
  )
  FetchContent_MakeAvailable(SFML)

  # Restore previous BUILD_SHARED_LIBS cache value
  if(_lilia_had_build_shared_libs)
    set(BUILD_SHARED_LIBS "${_lilia_old_build_shared_libs}" CACHE BOOL "" FORCE)
  else()
    unset(BUILD_SHARED_LIBS CACHE)
  endif()

  unset(_lilia_old_build_shared_libs)
  unset(_lilia_had_build_shared_libs)

  target_link_libraries(lilia_app PRIVATE
    sfml-graphics
    sfml-window
    sfml-system
    sfml-audio
  )

  lilia_set_perf_flags(lilia_app)
endif()

# -------------------------------------------------
# Tests
# -------------------------------------------------
file(GLOB_RECURSE TEST_FILES ${PROJECT_SOURCE_DIR}/tests/*.cpp)
if(TEST_FILES)
  add_executable(engine_tests ${TEST_FILES})
  target_compile_definitions(engine_tests PRIVATE
    LILIA_ENGINE
    $<$<PLATFORM_ID:Windows>:NOMINMAX>
  )
  target_include_directories(engine_tests PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/include/lilia
  )
  target_link_libraries(engine_tests PRIVATE lilia_core)
  enable_testing()
  add_test(NAME engine_tests COMMAND engine_tests)
  lilia_set_perf_flags(engine_tests)
endif()

# -------------------------------------------------
# Global IPO fallback (non-MSVC)
# -------------------------------------------------
if (LILIA_LTO AND NOT MSVC)
  include(CheckIPOSupported)
  check_ipo_supported(RESULT ipo_ok OUTPUT ipo_out)
  if (ipo_ok)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
  else()
    message(STATUS "IPO/LTO not supported: ${ipo_out}")
  endif()
endif()

# -------------------------------------------------
# Windows: copy runtime DLLs beside the GUI exe + copy assets
# -------------------------------------------------
if (WIN32 AND LILIA_BUILD_UI)
  add_custom_command(TARGET lilia_app POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      $<TARGET_RUNTIME_DLLS:lilia_app>
      $<TARGET_FILE_DIR:lilia_app>
    COMMAND_EXPAND_LISTS
  )

  if (EXISTS "${sfml_SOURCE_DIR}/extlibs/bin/x64/openal32.dll")
    add_custom_command(TARGET lilia_app POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${sfml_SOURCE_DIR}/extlibs/bin/x64/openal32.dll"
        $<TARGET_FILE_DIR:lilia_app>
    )
  endif()
endif()

# Copy assets next to the EXE after build (dev/CI convenience)
function(lilia_copy_assets tgt)
  if (EXISTS "${LILIA_ASSETS_DIR}")
    add_custom_command(TARGET ${tgt} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${tgt}>/assets"
      COMMAND ${CMAKE_COMMAND} -E copy_directory
              "${LILIA_ASSETS_DIR}"
              "$<TARGET_FILE_DIR:${tgt}>/assets"
      COMMENT "Copying runtime assets to $<TARGET_FILE_DIR:${tgt}>/assets"
    )
  else()
    message(WARNING "LILIA_ASSETS_DIR not found: ${LILIA_ASSETS_DIR} — assets won't be copied.")
  endif()
endfunction()
if(LILIA_BUILD_UI)
  lilia_copy_assets(lilia_app)
endif()

# -------------------------------------------------
# Output dirs
# -------------------------------------------------
set(_targets lilia_engine)
if (TARGET texel_tuner)
  list(APPEND _targets texel_tuner)
endif()
if(LILIA_BUILD_UI)
  list(APPEND _targets lilia_app)
endif()
if(TARGET engine_tests)
  list(APPEND _targets engine_tests)
endif()

foreach(target IN LISTS _targets)
  set_target_properties(${target} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
  )
endforeach()

# Developer QoL
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
