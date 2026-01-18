#include "lilia/engine/uci/builtin_bootstrap.hpp"

#include "lilia/engine/uci/engine_registry.hpp"

#include <filesystem>

#if defined(_WIN32)
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#else
#include <unistd.h>
#endif

namespace lilia::engine::uci
{
  namespace fs = std::filesystem;

  static fs::path executableDir()
  {
#if defined(_WIN32)
    wchar_t buf[MAX_PATH];
    DWORD len = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    fs::path p(buf, buf + len);
    return p.parent_path();
#elif defined(__APPLE__)
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::string tmp(size, '\0');
    if (_NSGetExecutablePath(tmp.data(), &size) == 0)
    {
      fs::path p(tmp.c_str());
      return fs::weakly_canonical(p).parent_path();
    }
    return fs::current_path();
#else
    char buf[4096];
    ssize_t len = ::readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (len > 0)
    {
      buf[len] = '\0';
      fs::path p(buf);
      return fs::weakly_canonical(p).parent_path();
    }
    return fs::current_path();
#endif
  }

  static fs::path withExeSuffix(fs::path p)
  {
#if defined(_WIN32)
    if (p.extension() != ".exe")
      p += ".exe";
#endif
    return p;
  }

  void bootstrapBuiltinEngines()
  {
    auto &reg = EngineRegistry::instance();
    reg.load();

    const fs::path dir = executableDir();
    const fs::path engines = dir / "engines";

    const fs::path liliaExe = withExeSuffix(engines / "lilia_engine");
    const fs::path sfExe = withExeSuffix(engines / "stockfish");

    if (fs::exists(liliaExe))
      reg.ensureBuiltin("lilia", "Lilia", "1.0", liliaExe);

    if (fs::exists(sfExe))
      reg.ensureBuiltin("stockfish", "Stockfish", "latest", sfExe);
  }
} // namespace lilia::engine::uci
