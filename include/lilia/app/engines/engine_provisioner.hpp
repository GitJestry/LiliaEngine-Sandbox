#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "builtin_engine_locator.hpp"
#include "engine_registry.hpp"

namespace lilia::app::engines
{
  class EngineProvisioner
  {
  public:
    explicit EngineProvisioner(EngineRegistry &registry = EngineRegistry::instance());

    // Copies bundled built-ins into a shared user-owned folder if missing/outdated,
    // then registers them in the EngineRegistry for the current platform.
    void ensureBuiltinsInstalled(const std::vector<BundledEngineInfo> &bundledEngines);

    std::filesystem::path builtinStoreDir() const;

  private:
    static std::string currentPlatformTag();
    static std::string fingerprintFile(const std::filesystem::path &p);
    static bool setExecutableBitIfNeeded(const std::filesystem::path &p);
    static bool copyFileIfChanged(const std::filesystem::path &src,
                                  const std::filesystem::path &dst);

  private:
    EngineRegistry &m_registry;
  };
} // namespace lilia::uci
