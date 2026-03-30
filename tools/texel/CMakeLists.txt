cmake_minimum_required(VERSION 3.21)

# -------------------------------------------------
# Resolve core target from parent
# -------------------------------------------------
if (DEFINED LILIA_ENGINE_TARGET)
  set(_LILIA_ENGINE_TARGET "${LILIA_ENGINE_TARGET}")
else()
  set(_LILIA_ENGINE_TARGET "lilia_engine_core")
endif()

if (NOT TARGET ${_LILIA_ENGINE_TARGET})
  message(FATAL_ERROR "texel_tuner: engine target not found: ${_LILIA_ENGINE_TARGET}")
endif()

# -------------------------------------------------
# Target
# -------------------------------------------------
add_executable(texel_tuner
  src/main.cpp
  src/common.cpp
  src/dataset.cpp
  src/options.cpp
  src/prepared_cache.cpp
  src/texel_trainer.cpp
  src/uci_engine.cpp
)

target_compile_definitions(texel_tuner PRIVATE
  $<$<PLATFORM_ID:Windows>:NOMINMAX>
)

if (COMMAND lilia_set_common_includes)
  lilia_set_common_includes(texel_tuner)
else()
  target_include_directories(texel_tuner PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/include/lilia
  )
endif()

target_include_directories(texel_tuner PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(texel_tuner PRIVATE
  ${_LILIA_ENGINE_TARGET}
  Threads::Threads
)

if (COMMAND lilia_set_perf_flags)
  lilia_set_perf_flags(texel_tuner)
endif()

if (COMMAND lilia_set_output_dirs)
  lilia_set_output_dirs(texel_tuner)
endif()

# Bundle built-in Lilia beside texel_tuner from the same directory scope.
if (COMMAND lilia_bundle_lilia_engine)
  lilia_bundle_lilia_engine(texel_tuner)
endif()

# -------------------------------------------------
# Stockfish provisioning for texel_tuner
# -------------------------------------------------
function(_lilia_texel_choose_stockfish_asset out_asset out_supported)
  if (LILIA_TEXEL_STOCKFISH_ASSET)
    set(${out_asset} "${LILIA_TEXEL_STOCKFISH_ASSET}" PARENT_SCOPE)
    set(${out_supported} TRUE PARENT_SCOPE)
    return()
  endif()

  string(TOLOWER "${CMAKE_HOST_SYSTEM_PROCESSOR}" _host_proc)

  if (WIN32)
    if (_host_proc MATCHES "arm64|aarch64")
      set(${out_asset} "stockfish-windows-armv8.zip" PARENT_SCOPE)
      set(${out_supported} TRUE PARENT_SCOPE)
    else()
      set(${out_asset} "stockfish-windows-x86-64-avx2.zip" PARENT_SCOPE)
      set(${out_supported} TRUE PARENT_SCOPE)
    endif()
    return()
  endif()

  if (APPLE)
    if (_host_proc MATCHES "arm64|aarch64")
      set(${out_asset} "stockfish-macos-m1-apple-silicon.tar" PARENT_SCOPE)
    else()
      set(${out_asset} "stockfish-macos-x86-64-bmi2.tar" PARENT_SCOPE)
    endif()
    set(${out_supported} TRUE PARENT_SCOPE)
    return()
  endif()

  if (UNIX)
    if (_host_proc MATCHES "x86_64|amd64")
      set(${out_asset} "stockfish-ubuntu-x86-64-avx2.tar" PARENT_SCOPE)
      set(${out_supported} TRUE PARENT_SCOPE)
    else()
      set(${out_asset} "" PARENT_SCOPE)
      set(${out_supported} FALSE PARENT_SCOPE)
    endif()
    return()
  endif()

  set(${out_asset} "" PARENT_SCOPE)
  set(${out_supported} FALSE PARENT_SCOPE)
endfunction()

set(LILIA_TEXEL_STOCKFISH "")
set(LILIA_TEXEL_STOCKFISH_DIR "${PROJECT_SOURCE_DIR}/tools/texel")

# 1) Prefer a manually provided local Stockfish copy under tools/texel
if (EXISTS "${LILIA_TEXEL_STOCKFISH_DIR}")
  set(_lilia_stockfish_candidates)
  list(APPEND _lilia_stockfish_candidates
    "${LILIA_TEXEL_STOCKFISH_DIR}/stockfish${CMAKE_EXECUTABLE_SUFFIX}"
    "${LILIA_TEXEL_STOCKFISH_DIR}/stockfish"
    "${LILIA_TEXEL_STOCKFISH_DIR}/stockfish.exe"
  )

  foreach(candidate IN LISTS _lilia_stockfish_candidates)
    if (NOT LILIA_TEXEL_STOCKFISH AND EXISTS "${candidate}")
      set(LILIA_TEXEL_STOCKFISH "${candidate}")
      break()
    endif()
  endforeach()

  unset(_lilia_stockfish_candidates)
endif()

# 2) If not found locally: optionally fetch an official release asset
if (NOT LILIA_TEXEL_STOCKFISH AND LILIA_TEXEL_FETCH_STOCKFISH)
  _lilia_texel_choose_stockfish_asset(_sf_asset _sf_supported)

  if (NOT _sf_supported)
    message(STATUS "texel_tuner: automatic Stockfish download is not configured for this host platform.")
  else()
    set(_sf_url "${LILIA_TEXEL_STOCKFISH_URL_BASE}/${_sf_asset}")

    set(_sf_dir "${CMAKE_BINARY_DIR}/_deps/stockfish")
    set(_sf_archive "${_sf_dir}/${_sf_asset}")
    set(_sf_extract "${_sf_dir}/extract")
    set(_sf_out "${_sf_dir}/stockfish${CMAKE_EXECUTABLE_SUFFIX}")

    file(MAKE_DIRECTORY "${_sf_dir}")
    file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/cmake")

    # Prefer a committed template; if missing, auto-generate a fallback.
    set(_sf_in_src "${CMAKE_CURRENT_LIST_DIR}/cmake/fetch_stockfish.cmake.in")
    set(_sf_in_bin "${CMAKE_CURRENT_BINARY_DIR}/fetch_stockfish.cmake.in")

    if (EXISTS "${_sf_in_src}")
      file(READ "${_sf_in_src}" _sf_tpl)
    else()
      set(_sf_tpl [==[
cmake_minimum_required(VERSION 3.21)

set(URL "@SF_URL@")
set(ARCHIVE "@SF_ARCHIVE@")
set(EXTRACT_DIR "@SF_EXTRACT_DIR@")
set(OUT "@SF_OUT@")

get_filename_component(_out_dir "${OUT}" DIRECTORY)
file(MAKE_DIRECTORY "${_out_dir}")

if(NOT EXISTS "${ARCHIVE}")
  message(STATUS "Downloading Stockfish: ${URL}")
  file(DOWNLOAD "${URL}" "${ARCHIVE}" SHOW_PROGRESS STATUS st)
  list(GET st 0 code)
  if(NOT code EQUAL 0)
    list(GET st 1 msg)
    message(FATAL_ERROR "Stockfish download failed: ${msg}")
  endif()
endif()

execute_process(COMMAND "${CMAKE_COMMAND}" -E rm -rf "${EXTRACT_DIR}")
file(MAKE_DIRECTORY "${EXTRACT_DIR}")

message(STATUS "Extracting Stockfish archive")
execute_process(
  COMMAND "${CMAKE_COMMAND}" -E tar xvf "${ARCHIVE}"
  WORKING_DIRECTORY "${EXTRACT_DIR}"
  RESULT_VARIABLE rv
)
if(NOT rv EQUAL 0)
  message(FATAL_ERROR "Failed to extract Stockfish archive: ${ARCHIVE}")
endif()

file(GLOB_RECURSE candidates LIST_DIRECTORIES false "${EXTRACT_DIR}/*")

set(best "")
set(bestSize 0)

foreach(c IN LISTS candidates)
  get_filename_component(name "${c}" NAME)
  get_filename_component(ext  "${c}" EXT)
  string(TOLOWER "${name}" name_l)
  string(TOLOWER "${ext}"  ext_l)

  if(name_l STREQUAL "stockfish" OR name_l STREQUAL "stockfish.exe")
    set(best "${c}")
    break()
  endif()

  if(ext_l STREQUAL ".cpp" OR ext_l STREQUAL ".c" OR ext_l STREQUAL ".h" OR ext_l STREQUAL ".hpp"
     OR ext_l STREQUAL ".md" OR ext_l STREQUAL ".txt" OR ext_l STREQUAL ".cff"
     OR ext_l STREQUAL ".sh" OR ext_l STREQUAL ".mk" OR ext_l STREQUAL ".o" OR ext_l STREQUAL ".a")
    continue()
  endif()

  if(NOT name_l MATCHES "^stockfish")
    continue()
  endif()

  if(WIN32 AND NOT ext_l STREQUAL ".exe")
    continue()
  endif()

  file(SIZE "${c}" sz)
  if(sz GREATER bestSize)
    set(best "${c}")
    set(bestSize "${sz}")
  endif()
endforeach()

if(best STREQUAL "")
  set(_shown 0)
  message(STATUS "Stockfish extraction produced these top-level entries:")
  file(GLOB _top LIST_DIRECTORIES true "${EXTRACT_DIR}/*")
  foreach(t IN LISTS _top)
    if(_shown GREATER 25)
      message(STATUS "  ... (truncated)")
      break()
    endif()
    message(STATUS "  ${t}")
    math(EXPR _shown "${_shown}+1")
  endforeach()
  message(FATAL_ERROR "Could not locate a Stockfish binary in extracted archive. Consider setting -DLILIA_TEXEL_STOCKFISH_ASSET=<asset-name>.")
endif()

file(COPY_FILE "${best}" "${OUT}" ONLY_IF_DIFFERENT)
if(NOT WIN32)
  execute_process(COMMAND /bin/chmod +x "${OUT}")
endif()

message(STATUS "Stockfish binary selected: ${best}")
]==])
    endif()

    file(WRITE "${_sf_in_bin}" "${_sf_tpl}")

    set(_sf_script "${CMAKE_BINARY_DIR}/cmake/fetch_stockfish.cmake")
    set(SF_URL "${_sf_url}")
    set(SF_ARCHIVE "${_sf_archive}")
    set(SF_EXTRACT_DIR "${_sf_extract}")
    set(SF_OUT "${_sf_out}")
    configure_file("${_sf_in_bin}" "${_sf_script}" @ONLY)

    add_custom_command(
      OUTPUT "${_sf_out}"
      COMMAND "${CMAKE_COMMAND}" -P "${_sf_script}"
      COMMENT "Fetching Stockfish (${_sf_asset}) for texel_tuner"
      VERBATIM
    )

    add_custom_target(stockfish_download DEPENDS "${_sf_out}")
    add_dependencies(texel_tuner stockfish_download)

    set(LILIA_TEXEL_STOCKFISH "${_sf_out}")
  endif()
endif()

# 3) If we have Stockfish, copy it next to texel_tuner.
if (LILIA_TEXEL_STOCKFISH)
  set(_sf_dest "stockfish${CMAKE_EXECUTABLE_SUFFIX}")

  add_custom_command(TARGET texel_tuner POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${LILIA_TEXEL_STOCKFISH}"
            "$<TARGET_FILE_DIR:texel_tuner>/${_sf_dest}"
    COMMENT "Copying Stockfish beside texel_tuner (${_sf_dest})"
    VERBATIM
  )
else()
  message(STATUS "texel_tuner: Stockfish not found. Provide one in tools/texel, or set -DLILIA_TEXEL_FETCH_STOCKFISH=ON.")
endif()
