cmake_minimum_required(VERSION 3.21)
project(ChessEngine_Lilia VERSION 1.0 LANGUAGES CXX)

# -------------------------------------------------
# C++ standard
# -------------------------------------------------
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# -------------------------------------------------
# Build options
# -------------------------------------------------
option(LILIA_NATIVE       "Enable native CPU optimizations (-march=native or /arch:AVX2)" ON)
option(LILIA_FAST_MATH    "Enable fast-math in Release" ON)
option(LILIA_LTO          "Enable Link-Time Optimization in Release" ON)
option(LILIA_PGO_GENERATE "Build with PGO generate (instrumentation)" OFF)
option(LILIA_PGO_USE      "Build with PGO use (optimized by profiles)" OFF)

option(LILIA_BUILD_APP    "Build the Lilia app" OFF)
option(LILIA_BUILD_TEXEL  "Build the Texel tuner tool" ON)
option(LILIA_BUILD_TESTS  "Build tests" OFF)

option(LILIA_BUNDLE_LILIA_ENGINE "Copy built-in Lilia engine beside runtime targets" ON)

option(LILIA_UNIVERSAL2 "Build universal2 binary on macOS (arm64 + x86_64)" OFF)
if (APPLE AND LILIA_UNIVERSAL2)
  set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
  set(LILIA_NATIVE OFF CACHE BOOL "" FORCE)
endif()

set(LILIA_PGO_PROFDATA "" CACHE FILEPATH "Path to merged LLVM .profdata for Clang/AppleClang PGO use")

# -------------------------------------------------
# Texel Stockfish provisioning
# -------------------------------------------------
option(LILIA_TEXEL_FETCH_STOCKFISH "Auto-download a Stockfish binary for texel_tuner when not bundled" ON)
set(LILIA_TEXEL_STOCKFISH_URL_BASE
    "https://github.com/official-stockfish/Stockfish/releases/latest/download"
    CACHE STRING "Base URL for Stockfish release assets")
set(LILIA_TEXEL_STOCKFISH_ASSET ""
    CACHE STRING "Override Stockfish asset filename")

# -------------------------------------------------
# Runtime assets
# -------------------------------------------------
set(LILIA_ASSETS_DIR "${PROJECT_SOURCE_DIR}/assets"
    CACHE PATH "Directory with runtime assets")

# -------------------------------------------------
# Dependencies
# -------------------------------------------------
find_package(Threads REQUIRED)

# -------------------------------------------------
# Sources
# -------------------------------------------------
file(GLOB_RECURSE CHESS_FILES CONFIGURE_DEPENDS
  "${PROJECT_SOURCE_DIR}/src/lilia/chess/*.cpp"
)

file(GLOB_RECURSE ENGINE_FILES CONFIGURE_DEPENDS
  "${PROJECT_SOURCE_DIR}/src/lilia/engine/*.cpp"
)

file(GLOB_RECURSE PROTOCOL_UCI_FILES CONFIGURE_DEPENDS
  "${PROJECT_SOURCE_DIR}/src/lilia/protocol/uci/*.cpp"
)

file(GLOB_RECURSE APP_FILES CONFIGURE_DEPENDS
  "${PROJECT_SOURCE_DIR}/src/lilia/app/*.cpp"
)

file(GLOB_RECURSE CHESS_TEST_FILES CONFIGURE_DEPENDS
  "${PROJECT_SOURCE_DIR}/tests/chess/*.cpp"
)

file(GLOB_RECURSE ENGINE_TEST_FILES CONFIGURE_DEPENDS
  "${PROJECT_SOURCE_DIR}/tests/engine/*.cpp"
)

file(GLOB_RECURSE APP_TEST_FILES CONFIGURE_DEPENDS
  "${PROJECT_SOURCE_DIR}/tests/app/*.cpp"
)

# -------------------------------------------------
# Helpers
# -------------------------------------------------
function(lilia_runtime_resource_dir tgt out_var)
  get_target_property(_is_bundle ${tgt} MACOSX_BUNDLE)
  if (APPLE AND _is_bundle)
    set(${out_var} "$<TARGET_BUNDLE_CONTENT_DIR:${tgt}>/Resources" PARENT_SCOPE)
  else()
    set(${out_var} "$<TARGET_FILE_DIR:${tgt}>" PARENT_SCOPE)
  endif()
endfunction()

function(lilia_set_common_includes tgt)
  target_include_directories(${tgt} PUBLIC
    ${PROJECT_SOURCE_DIR}/include
  )
endfunction()

function(lilia_set_output_dirs tgt)
  set_target_properties(${tgt} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
  )
endfunction()

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
        message(STATUS "IPO/LTO not supported for ${tgt}: ${_ipo_out}")
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
      set(_lilia_is_gcc TRUE)
      set(_lilia_is_clang FALSE)
    elseif (CMAKE_CXX_COMPILER_ID MATCHES "Clang|AppleClang")
      set(_lilia_is_gcc FALSE)
      set(_lilia_is_clang TRUE)
    else()
      set(_lilia_is_gcc FALSE)
      set(_lilia_is_clang FALSE)
    endif()

    target_compile_options(${tgt} PRIVATE
      $<$<CONFIG:Release>:-O3 -DNDEBUG -fomit-frame-pointer>
      $<$<AND:$<CONFIG:Release>,$<BOOL:${LILIA_NATIVE}>>:-march=native -mtune=native>
      $<$<AND:$<CONFIG:Release>,$<BOOL:${LILIA_FAST_MATH}>>:-ffast-math>
      $<$<AND:$<CONFIG:Release>,$<BOOL:${_lilia_is_gcc}>>:-fno-plt>
      $<$<AND:$<CONFIG:Release>,$<BOOL:${_lilia_is_clang}>>:-fstrict-aliasing>
      $<$<CONFIG:Release>:-fvisibility=hidden -fvisibility-inlines-hidden>
    )

    if (LILIA_LTO)
      target_compile_options(${tgt} PRIVATE $<$<CONFIG:Release>:-flto>)
      target_link_options(${tgt} PRIVATE $<$<CONFIG:Release>:-flto>)
      if (_lilia_is_gcc)
        target_link_options(${tgt} PRIVATE $<$<CONFIG:Release>:-Wl,-O2>)
      endif()
    endif()

    if (LILIA_PGO_GENERATE)
      if (_lilia_is_clang)
        target_compile_options(${tgt} PRIVATE $<$<CONFIG:Release>:-fprofile-instr-generate>)
        target_link_options(${tgt}    PRIVATE $<$<CONFIG:Release>:-fprofile-instr-generate>)
      else()
        target_compile_options(${tgt} PRIVATE $<$<CONFIG:Release>:-fprofile-generate>)
        target_link_options(${tgt}    PRIVATE $<$<CONFIG:Release>:-fprofile-generate>)
      endif()
    endif()

    if (LILIA_PGO_USE)
      if (_lilia_is_clang)
        if (NOT LILIA_PGO_PROFDATA)
          message(FATAL_ERROR "LILIA_PGO_USE is ON, but LILIA_PGO_PROFDATA is empty.")
        endif()
        target_compile_options(${tgt} PRIVATE
          $<$<CONFIG:Release>:-fprofile-instr-use=${LILIA_PGO_PROFDATA}>
        )
        target_link_options(${tgt} PRIVATE
          $<$<CONFIG:Release>:-fprofile-instr-use=${LILIA_PGO_PROFDATA}>
        )
      else()
        target_compile_options(${tgt} PRIVATE $<$<CONFIG:Release>:-fprofile-use -fprofile-correction>)
        target_link_options(${tgt} PRIVATE $<$<CONFIG:Release>:-fprofile-use>)
      endif()
    endif()
  endif()
endfunction()

function(lilia_apply_target_defaults tgt)
  target_compile_definitions(${tgt} PUBLIC
    $<$<PLATFORM_ID:Windows>:NOMINMAX>
  )
  lilia_set_common_includes(${tgt})
  lilia_set_perf_flags(${tgt})
  lilia_set_output_dirs(${tgt})
endfunction()

function(lilia_bundle_lilia_engine tgt)
  if (NOT LILIA_BUNDLE_LILIA_ENGINE)
    return()
  endif()

  if (NOT TARGET ${tgt})
    message(FATAL_ERROR "lilia_bundle_lilia_engine called for missing target: ${tgt}")
  endif()

  if (NOT TARGET lilia_engine)
    message(FATAL_ERROR "lilia_bundle_lilia_engine requires target lilia_engine")
  endif()

  lilia_runtime_resource_dir(${tgt} _runtime_root)
  set(_engine_dst_dir "${_runtime_root}/engines")

  add_custom_command(TARGET ${tgt} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${_engine_dst_dir}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:lilia_engine>"
            "${_engine_dst_dir}/$<TARGET_FILE_NAME:lilia_engine>"
    COMMENT "Bundling built-in Lilia engine for ${tgt}"
  )
endfunction()

function(lilia_copy_assets tgt)
  if (NOT EXISTS "${LILIA_ASSETS_DIR}")
    message(WARNING "LILIA_ASSETS_DIR not found: ${LILIA_ASSETS_DIR} — assets won't be copied.")
    return()
  endif()

  lilia_runtime_resource_dir(${tgt} _runtime_root)
  set(_assets_dst_dir "${_runtime_root}/assets")

  add_custom_command(TARGET ${tgt} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${_assets_dst_dir}"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${LILIA_ASSETS_DIR}"
            "${_assets_dst_dir}"
    COMMENT "Copying runtime assets for ${tgt}"
  )
endfunction()

function(lilia_enable_sfml)
  if (TARGET sfml-system)
    return()
  endif()

  include(FetchContent)

  if (DEFINED BUILD_SHARED_LIBS)
    set(_lilia_old_build_shared_libs "${BUILD_SHARED_LIBS}")
    set(_lilia_had_build_shared_libs TRUE)
  else()
    set(_lilia_had_build_shared_libs FALSE)
  endif()

  set(BUILD_SHARED_LIBS ON CACHE BOOL "" FORCE)
  set(SFML_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(SFML_BUILD_TESTING  OFF CACHE BOOL "" FORCE)
  set(SFML_USE_STATIC_STD_LIBS OFF CACHE BOOL "" FORCE)

  FetchContent_Declare(
    SFML
    GIT_REPOSITORY https://github.com/SFML/SFML.git
    GIT_TAG 2.6.x
  )
  FetchContent_MakeAvailable(SFML)

  if (_lilia_had_build_shared_libs)
    set(BUILD_SHARED_LIBS "${_lilia_old_build_shared_libs}" CACHE BOOL "" FORCE)
  else()
    unset(BUILD_SHARED_LIBS CACHE)
  endif()

  unset(_lilia_old_build_shared_libs)
  unset(_lilia_had_build_shared_libs)
endfunction()

function(lilia_copy_windows_runtime_dlls tgt)
  if (NOT WIN32)
    return()
  endif()

  add_custom_command(TARGET ${tgt} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:${tgt}>
            $<TARGET_FILE_DIR:${tgt}>
    COMMAND_EXPAND_LISTS
  )

  if (EXISTS "${sfml_SOURCE_DIR}/extlibs/bin/x64/openal32.dll")
    add_custom_command(TARGET ${tgt} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
              "${sfml_SOURCE_DIR}/extlibs/bin/x64/openal32.dll"
              $<TARGET_FILE_DIR:${tgt}>
    )
  endif()
endfunction()

# -------------------------------------------------
# Build graph decision: do we need app components?
# -------------------------------------------------
set(LILIA_NEEDS_APP_COMPONENTS OFF)
if (LILIA_BUILD_APP)
  set(LILIA_NEEDS_APP_COMPONENTS ON)
endif()
if (LILIA_BUILD_TESTS AND APP_TEST_FILES)
  set(LILIA_NEEDS_APP_COMPONENTS ON)
endif()

# -------------------------------------------------
# Libraries
# -------------------------------------------------
add_library(lilia_chess STATIC ${CHESS_FILES})
lilia_apply_target_defaults(lilia_chess)

add_library(lilia_engine_core STATIC ${ENGINE_FILES})
target_link_libraries(lilia_engine_core PUBLIC lilia_chess Threads::Threads)
lilia_apply_target_defaults(lilia_engine_core)

add_library(lilia_protocol_uci STATIC ${PROTOCOL_UCI_FILES})
target_link_libraries(lilia_protocol_uci PUBLIC lilia_engine_core)
lilia_apply_target_defaults(lilia_protocol_uci)

if (LILIA_NEEDS_APP_COMPONENTS)
  lilia_enable_sfml()

  add_library(lilia_app_core STATIC ${APP_FILES})
  target_link_libraries(lilia_app_core
    PUBLIC
      lilia_chess
      lilia_engine_core
      sfml-graphics
      sfml-window
      sfml-system
      sfml-audio
  )
  lilia_apply_target_defaults(lilia_app_core)
endif()

# -------------------------------------------------
# Executables
# -------------------------------------------------
add_executable(lilia_engine apps/lilia_engine/main.cpp)
target_link_libraries(lilia_engine PRIVATE lilia_protocol_uci)
lilia_apply_target_defaults(lilia_engine)

if (LILIA_BUILD_APP)
  add_executable(lilia_app apps/lilia_app/main.cpp)
  target_link_libraries(lilia_app PRIVATE lilia_app_core)

  if (APPLE)
    set_target_properties(lilia_app PROPERTIES MACOSX_BUNDLE TRUE)
  endif()

  lilia_apply_target_defaults(lilia_app)
  lilia_bundle_lilia_engine(lilia_app)
  lilia_copy_assets(lilia_app)
  lilia_copy_windows_runtime_dlls(lilia_app)
endif()

# -------------------------------------------------
# Texel tuner
# -------------------------------------------------
if (LILIA_BUILD_TEXEL)
  set(LILIA_CHESS_TARGET lilia_chess)
  set(LILIA_ENGINE_TARGET lilia_engine_core)
  add_subdirectory(tools/texel)
endif()

# -------------------------------------------------
# Tests
# -------------------------------------------------
if (LILIA_BUILD_TESTS)
  enable_testing()

  if (CHESS_TEST_FILES)
    add_executable(chess_tests ${CHESS_TEST_FILES})
    target_link_libraries(chess_tests PRIVATE lilia_chess)
    lilia_apply_target_defaults(chess_tests)
    add_test(NAME chess_tests COMMAND chess_tests)
  endif()

  if (ENGINE_TEST_FILES)
    add_executable(engine_tests ${ENGINE_TEST_FILES})
    target_link_libraries(engine_tests PRIVATE lilia_engine_core lilia_protocol_uci)
    lilia_apply_target_defaults(engine_tests)
    add_test(NAME engine_tests COMMAND engine_tests)
  endif()

  if (APP_TEST_FILES)
    add_executable(app_tests ${APP_TEST_FILES})
    target_link_libraries(app_tests PRIVATE lilia_app_core)
    lilia_apply_target_defaults(app_tests)
    lilia_bundle_lilia_engine(app_tests)
    add_test(NAME app_tests COMMAND app_tests)

    if (WIN32)
      lilia_copy_windows_runtime_dlls(app_tests)
    endif()
  endif()
endif()

# -------------------------------------------------
# Developer QoL
# -------------------------------------------------
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
