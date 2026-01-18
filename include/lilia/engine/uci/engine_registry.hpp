#pragma once
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "lilia/model/analysis/config/start_config.hpp"
#include "lilia/engine/uci/uci_engine_process.hpp"

namespace lilia::engine::uci
{
  struct EngineEntry
  {
    lilia::config::EngineRef ref;
    UciEngineProcess::Id id;
    std::vector<lilia::config::UciOption> options;
    bool builtin{false};
  };

  class EngineRegistry
  {
  public:
    static EngineRegistry &instance();

    void load();
    void save() const;

    // Ensure built-ins exist in registry (e.g. Stockfish + Lilia downloaded by CMake)
    void ensureBuiltin(const std::string &engineId,
                       const std::string &displayName,
                       const std::string &version,
                       const std::filesystem::path &exePath);

    // Upload/install arbitrary UCI engine executable
    // Copies into per-user engine dir, probes UCI, stores schema.
    std::optional<EngineEntry> installExternal(const std::filesystem::path &sourceExePath,
                                               std::string *outError = nullptr);

    std::vector<EngineEntry> list() const;
    std::optional<EngineEntry> get(const std::string &engineId) const;

    // Returns a BotConfig with default values populated from cached UCI options
    lilia::config::BotConfig makeDefaultBotConfig(const std::string &engineId) const;

    std::filesystem::path enginesDir() const;

  private:
    EngineRegistry() = default;

    std::filesystem::path userDataDir() const;
    std::filesystem::path dbPath() const;

    static std::string makeStableIdFromPath(const std::filesystem::path &p);

    std::map<std::string, EngineEntry> m_entries;
  };
} // namespace lilia::engine::uci
