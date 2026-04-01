#include "lilia/app/engines/builtin_engine_locator.hpp"

#include <algorithm>
#include <set>
#include <system_error>

#if defined(_WIN32)
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#else
#include <unistd.h>
#endif

namespace lilia::app::engines
{
  namespace fs = std::filesystem;

  fs::path BuiltinEngineLocator::executableDir()
  {
#if defined(_WIN32)
    wchar_t buf[MAX_PATH];
    const DWORD len = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    if (len == 0)
      return fs::current_path();

    fs::path p(buf, buf + len);
    return p.parent_path();
#elif defined(__APPLE__)
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);

    std::string raw(size, '\0');
    if (_NSGetExecutablePath(raw.data(), &size) != 0)
      return fs::current_path();

    std::error_code ec;
    const fs::path p = fs::weakly_canonical(fs::path(raw.c_str()), ec);
    if (ec)
      return fs::path(raw.c_str()).parent_path();
    return p.parent_path();
#else
    char buf[4096];
    const ssize_t len = ::readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (len <= 0)
      return fs::current_path();

    buf[len] = '\0';
    std::error_code ec;
    const fs::path p = fs::weakly_canonical(fs::path(buf), ec);
    if (ec)
      return fs::path(buf).parent_path();
    return p.parent_path();
#endif
  }

  fs::path BuiltinEngineLocator::withPlatformExecutableSuffix(fs::path p)
  {
#if defined(_WIN32)
    if (p.extension() != ".exe")
      p += ".exe";
#endif
    return p;
  }

  std::vector<fs::path> BuiltinEngineLocator::searchRoots()
  {
    const fs::path exeDir = executableDir();
    std::vector<fs::path> roots;

    // Normal flat runtime layout:
    //   <target_dir>/engines/...
    roots.push_back(exeDir / "engines");

    // Some packagers / dev layouts may use lowercase resources.
    roots.push_back(exeDir / "resources" / "engines");

#if defined(__APPLE__)
    // macOS app bundle layout:
    //   <App>.app/Contents/MacOS/<app>
    //   <App>.app/Contents/Resources/engines/...
    if (exeDir.filename() == "MacOS")
      roots.push_back(exeDir.parent_path() / "Resources" / "engines");
#endif

    // Development fallback when running from IDEs / unusual working dirs.
    roots.push_back(fs::current_path() / "engines");

    // Deduplicate while preserving order.
    std::vector<fs::path> unique;
    std::set<std::string> seen;
    for (const auto &r : roots)
    {
      std::error_code ec;
      const fs::path norm = fs::weakly_canonical(r, ec);
      const std::string key = ec ? r.lexically_normal().string() : norm.string();
      if (seen.insert(key).second)
        unique.push_back(ec ? r.lexically_normal() : norm);
    }

    return unique;
  }

  std::vector<BundledEngineInfo> BuiltinEngineLocator::findBundledEngines()
  {
    std::vector<BundledEngineInfo> out;
    const auto roots = searchRoots();

    struct Wanted
    {
      const char *engineId;
      const char *displayName;
      const char *version;
      const char *iconKey;
      const char *fileStem;
    };

    const Wanted wanted[] = {
        {"lilia", "Lilia", "builtin", "lilia", "lilia_engine"},
        {"stockfish", "Stockfish", "builtin", "stockfish", "stockfish"},
    };

    for (const auto &w : wanted)
    {
      bool found = false;
      for (const auto &root : roots)
      {
        const fs::path candidate = withPlatformExecutableSuffix(root / w.fileStem);
        std::error_code ec;
        if (!fs::exists(candidate, ec) || ec)
          continue;
        if (!fs::is_regular_file(candidate, ec) || ec)
          continue;

        BundledEngineInfo info{};
        info.engineId = w.engineId;
        info.displayName = w.displayName;
        info.version = w.version;
        info.iconKey = w.iconKey;
        info.entryExecutable = candidate;

        out.push_back(std::move(info));
        found = true;
        break;
      }

      (void)found;
    }

    return out;
  }
}
