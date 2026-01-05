#pragma once
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>

#ifdef _WIN32
  #include <windows.h>
#else
  #include <unistd.h>
#endif

namespace lilia::tools::texel {

namespace fs = std::filesystem;

struct DefaultPaths {
  fs::path dataFile;
  fs::path weightsFile;
  std::optional<fs::path> stockfish;
};

// FEN de-dup key: board + stm + castling + ep (ignore clocks)
inline std::string fen_key(std::string_view fen) {
  // Copy up to 4 space-separated fields.
  std::string out;
  out.reserve(fen.size());
  int fields = 0;
  for (size_t i = 0; i < fen.size(); ++i) {
    char c = fen[i];
    if (c == ' ') {
      ++fields;
      if (fields >= 4) break;
    }
    out.push_back(c);
  }
  return out;
}

inline fs::path locate_project_root(fs::path start) {
  std::error_code ec;
  if (!start.is_absolute()) start = fs::absolute(start, ec);
  while (true) {
    if (fs::exists(start / "CMakeLists.txt", ec)) return start;
    const auto parent = start.parent_path();
    if (parent.empty() || parent == start) return fs::current_path();
    start = parent;
  }
}

inline fs::path default_user_texel_dir() {
#ifdef _WIN32
  if (const char* appData = std::getenv("APPDATA"); appData && *appData)
    return fs::path(appData) / "Lilia" / "texel";
  if (const char* userProfile = std::getenv("USERPROFILE"); userProfile && *userProfile)
    return fs::path(userProfile) / "AppData" / "Roaming" / "Lilia" / "texel";
#else
  if (const char* xdg = std::getenv("XDG_DATA_HOME"); xdg && *xdg)
    return fs::path(xdg) / "lilia" / "texel";
  if (const char* home = std::getenv("HOME"); home && *home)
    return fs::path(home) / ".local" / "share" / "lilia" / "texel";
#endif
  return fs::current_path() / "texel_data";
}

std::optional<fs::path> find_stockfish_in_dir(const fs::path& dir);

DefaultPaths compute_default_paths(const char* argv0);

}  // namespace lilia::tools::texel
