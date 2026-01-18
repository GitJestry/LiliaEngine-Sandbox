#pragma once
#include <filesystem>

namespace lilia::engine::uci
{
  // Locates bundled built-in engines near the executable and registers them.
  // Expected layout:
  //   <exe_dir>/engines/lilia_engine[.exe]
  //   <exe_dir>/engines/stockfish[.exe]
  //
  // You can adjust these paths or make them configurable later.
  void bootstrapBuiltinEngines();
}
