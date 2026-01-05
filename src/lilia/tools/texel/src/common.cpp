#include "lilia/tools/texel/common.hpp"

#include <array>
#include <system_error>

namespace lilia::tools::texel {

std::optional<fs::path> find_stockfish_in_dir(const fs::path& dir) {
  if (dir.empty()) return std::nullopt;
  std::error_code ec;
  if (!fs::exists(dir, ec)) return std::nullopt;

  const std::array<const char*, 2> names = {"stockfish", "stockfish.exe"};
  for (const auto* name : names) {
    const fs::path candidate = dir / name;
    std::error_code e2;
    if (fs::exists(candidate, e2) && fs::is_regular_file(candidate, e2)) return candidate;
  }
  for (fs::directory_iterator it{dir, ec}; !ec && it != fs::directory_iterator{}; ++it) {
    std::error_code rf, sl;
    bool isFile = it->is_regular_file(rf) || it->is_symlink(sl);
    if (!isFile) continue;
    const auto stem = it->path().stem().string();
    if (stem.rfind("stockfish", 0) == 0) return it->path();
  }
  return std::nullopt;
}

DefaultPaths compute_default_paths(const char* argv0) {
  fs::path exePath;
#ifdef _WIN32
  wchar_t buffer[MAX_PATH];
  DWORD len = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
  if (len > 0) exePath.assign(buffer, buffer + len);
  if (exePath.empty() && argv0 && *argv0) exePath = fs::path(argv0);
#else
  std::error_code ec;
  exePath = fs::read_symlink("/proc/self/exe", ec);
  if (ec && argv0 && *argv0) exePath = fs::absolute(fs::path(argv0), ec);
  if (ec) exePath.clear();
#endif
  if (exePath.empty()) exePath = fs::current_path();
  fs::path exeDir = exePath.has_filename() ? exePath.parent_path() : exePath;
  if (exeDir.empty()) exeDir = fs::current_path();

  const fs::path projectRoot = locate_project_root(exeDir);
  const bool hasProjectRoot = fs::exists(projectRoot / "CMakeLists.txt");

  const fs::path texelDir = hasProjectRoot ? projectRoot / "texel_data" : default_user_texel_dir();

  DefaultPaths defaults;
  defaults.dataFile = texelDir / "texel_dataset.txt";
  defaults.weightsFile = texelDir / "texel_weights.txt";
  defaults.stockfish = find_stockfish_in_dir(exeDir);
  if (!defaults.stockfish) defaults.stockfish = find_stockfish_in_dir(projectRoot / "tools" / "texel");
  return defaults;
}

}  // namespace lilia::tools::texel
