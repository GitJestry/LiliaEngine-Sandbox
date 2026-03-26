#pragma once

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "lilia/uci/uci_engine_process.hpp"
#include "lilia/model/analysis/config/start_config.hpp"

namespace lilia::uci
{
  struct EngineEntry
  {
    lilia::config::EngineRef ref;
    UciEngineProcess::Id id;
    std::vector<lilia::config::UciOption> options;

    std::string iconKey;
    std::string artifactId;
    std::filesystem::path workingDirectory;
    bool builtin{false};
  };

  class EngineRegistry
  {
  public:
    static EngineRegistry &instance();

    void load();
    void save() const;

    // Built-ins are logical engines with a stable app-defined id.
    // At startup they are provisioned from app-local bundled binaries into the
    // shared user engine store, then registered here for the current platform.
    void ensureBuiltin(const std::string &engineId,
                       const std::string &displayName,
                       const std::string &version,
                       const std::filesystem::path &entryExecutable,
                       const std::string &iconKey = {});

    // Installs a user-supplied engine executable into a content-addressed artifact folder.
    // The engine keeps a stable id for that artifact even if the original source path changes.
    std::optional<EngineEntry> installExternal(const std::filesystem::path &sourceEntryExecutable,
                                               std::string *outError = nullptr);

    std::vector<EngineEntry> list() const;
    std::optional<EngineEntry> get(const std::string &engineId) const;
    lilia::config::BotConfig makeDefaultBotConfig(const std::string &engineId) const;

    std::filesystem::path userDataDir() const;
    std::filesystem::path catalogDir() const;
    std::filesystem::path artifactsDir() const;
    std::filesystem::path enginesDir() const; // compatibility alias for artifactsDir()

  private:
    EngineRegistry() = default;

    struct EngineInstallRecord
    {
      std::string platformTag;
      std::string artifactId;
      std::filesystem::path entryExecutable;
      std::filesystem::path workingDirectory;
      bool builtin{false};
    };

    struct EngineRecord
    {
      std::string engineId;
      std::string displayName;
      std::string version;
      std::string iconKey;
      bool builtin{false};

      UciEngineProcess::Id uciId;
      std::vector<lilia::config::UciOption> options;
      std::map<std::string, EngineInstallRecord> installs; // keyed by platformTag
    };

    static std::string currentPlatformTag();
    static std::string chooseIconKey(const std::string &displayName, bool builtin);
    static std::string slugify(const std::string &s);
    static std::string fingerprintFile(const std::filesystem::path &p);
    static std::string fingerprintPathQuick(const std::filesystem::path &p);

    static bool probeEngine(const std::filesystem::path &entryExecutable,
                            const std::filesystem::path &workingDirectory,
                            UciEngineProcess::Id &outId,
                            std::vector<lilia::config::UciOption> &outOptions,
                            std::string *outError);

    static std::optional<EngineEntry> makeEntryForPlatform(const EngineRecord &record, const std::string &platformTag);

    void saveRecord(const EngineRecord &record) const;
    void ensureDirectories() const;
    std::filesystem::path engineCatalogPath(const std::string &engineId) const;

  private:
    std::map<std::string, EngineRecord> m_records;
  };
} // namespace lilia::engine::uci
