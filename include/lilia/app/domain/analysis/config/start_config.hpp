#pragma once
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace lilia::app::domain::analysis::config
{

  enum class SideKind
  {
    Human,
    Engine
  };

  struct TimeControl
  {
    bool enabled{false};
    int baseSeconds{300};
    int incrementSeconds{0};
  };

  struct GameConfig
  {
    std::string startFen;
    TimeControl tc;
  };

  struct ReplayConfig
  {
    bool enabled{false};
    std::string pgnText;
    std::string pgnFilename;
    std::string pgnPath;
  };

  struct EngineRef
  {
    // Logical engine identity in the registry.
    // This should be the primary way the app remembers an engine.
    bool builtin{false};
    std::string engineId;       // stable logical id, e.g. "lilia", "stockfish", "ext_xxx"
    std::string executablePath; // resolved executable for the current platform/install
    std::string displayName;
    std::string version;

    // New fields for the redesigned registry/install model.
    std::string iconKey;          // UI icon selection key
    std::string artifactId;       // installed artifact id / fingerprint
    std::string workingDirectory; // directory the engine should run from
  };

  struct SearchLimits
  {
    // Choose one primary mode; keep it simple for now.
    // If tc.enabled => go wtime/btime; else use movetime OR depth.
    std::optional<int> movetimeMs;
    std::optional<int> depth;
  };

  struct UciOption
  {
    enum class Type
    {
      Check,
      Spin,
      Combo,
      String,
      Button
    };
    std::string name;
    Type type{Type::String};
    std::string defaultStr;
    int defaultInt{0};
    bool defaultBool{false};
    int min{0}, max{0};
    std::vector<std::string> vars; // combo options
  };

  using UciValue = std::variant<bool, int, std::string>;

  struct BotConfig
  {
    EngineRef engine;
    SearchLimits limits;
    std::map<std::string, UciValue> uciValues; // by option name
  };

  struct SideConfig
  {
    SideKind kind{SideKind::Human};
    std::optional<BotConfig> bot; // only if kind==Engine
  };

  struct StartConfig
  {
    GameConfig game;
    ReplayConfig replay;
    SideConfig white;
    SideConfig black;
  };

}
