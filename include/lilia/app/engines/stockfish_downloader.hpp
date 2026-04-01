#pragma once

#include <filesystem>
#include <optional>
#include <string>

#include "engine_registry.hpp"

namespace lilia::app::engines
{
  class StockfishDownloader
  {
  public:
    explicit StockfishDownloader(EngineRegistry &registry = EngineRegistry::instance());

    // Ensures Stockfish exists in the shared engine store and is registered.
    // Returns true on success or if already installed.
    bool ensureInstalledIfMissing(std::string *outError = nullptr);

    static bool isSupportedOnCurrentPlatform();

  private:
    struct DownloadSpec
    {
      std::string assetName;
      std::string url;
      std::string versionLabel;
    };

    static std::string currentPlatformTag();
    static std::optional<DownloadSpec> chooseDownloadSpec();

    static bool downloadFile(const std::string &url,
                             const std::filesystem::path &dst,
                             std::string *outError);

    static bool extractArchive(const std::filesystem::path &archivePath,
                               const std::filesystem::path &extractDir,
                               std::string *outError);

    static std::optional<std::filesystem::path> findExtractedStockfishBinary(
        const std::filesystem::path &extractDir);

    static bool copyFileIfDifferent(const std::filesystem::path &src,
                                    const std::filesystem::path &dst,
                                    std::string *outError);

    static bool setExecutableBitIfNeeded(const std::filesystem::path &p);

    std::filesystem::path downloadCacheDir() const;
    std::filesystem::path installedBinaryPath() const;

  private:
    EngineRegistry &m_registry;
  };
}
