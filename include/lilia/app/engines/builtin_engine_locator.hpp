#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace lilia::app::engines
{
  struct BundledEngineInfo
  {
    std::string engineId;
    std::string displayName;
    std::string version;
    std::string iconKey;
    std::filesystem::path entryExecutable;
  };

  class BuiltinEngineLocator
  {
  public:
    // Finds bundled built-in engines near the current executable / app bundle.
    static std::vector<BundledEngineInfo> findBundledEngines();

  private:
    static std::filesystem::path executableDir();
    static std::vector<std::filesystem::path> searchRoots();
    static std::filesystem::path withPlatformExecutableSuffix(std::filesystem::path p);
  };
}
