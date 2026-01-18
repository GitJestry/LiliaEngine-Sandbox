#pragma once
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace lilia::config
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
    // One of:
    bool builtin{false};
    std::string engineId;       // stable id in registry (recommended)
    std::string executablePath; // resolved path to binary
    std::string displayName;
    std::string version;
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

} // namespace lilia::config
