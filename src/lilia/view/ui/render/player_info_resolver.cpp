#include "lilia/view/ui/render/player_info_resolver.hpp"

#include "lilia/view/ui/render/engine_icons.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>

namespace lilia::view
{
  static std::string lowerCopy(std::string s)
  {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c)
                   { return char(std::tolower(c)); });
    return s;
  }

  // If id looks like "name_1234567890", strip the trailing "_<digits>".
  static std::string stripTrailingTimestamp(std::string s)
  {
    const auto pos = s.rfind('_');
    if (pos == std::string::npos || pos + 1 >= s.size())
      return s;

    bool allDigits = true;
    for (std::size_t i = pos + 1; i < s.size(); ++i)
    {
      if (!std::isdigit(static_cast<unsigned char>(s[i])))
      {
        allDigits = false;
        break;
      }
    }

    if (allDigits)
      s.resize(pos);

    return s;
  }

  static bool containsToken(const std::string &haystack, const std::string &tokenLower)
  {
    return haystack.find(tokenLower) != std::string::npos;
  }

  static std::string iconForEngine(const lilia::config::EngineRef &ref)
  {
    const std::string id = stripTrailingTimestamp(lowerCopy(ref.engineId));
    const std::string dn = lowerCopy(ref.displayName);

    std::string exeStem;
    if (!ref.executablePath.empty())
    {
      std::error_code ec;
      std::filesystem::path p(ref.executablePath);
      // stem() removes extension (e.g., stockfish.exe -> stockfish)
      exeStem = stripTrailingTimestamp(lowerCopy(p.stem().string()));
      (void)ec;
    }

    // Lilia detection
    if (id == "lilia" || id == "lilia_engine" ||
        containsToken(dn, "lilia") ||
        exeStem == "lilia_engine" || containsToken(exeStem, "lilia"))
    {
      return std::string(ui::icons::LILIA);
    }

    // Stockfish detection
    if (id == "stockfish" ||
        containsToken(dn, "stockfish") ||
        exeStem == "stockfish" || containsToken(exeStem, "stockfish"))
    {
      return std::string(ui::icons::STOCKFISH);
    }

    return std::string(ui::icons::EXTERNAL);
  }

  model::analysis::PlayerInfo makePlayerInfo(const lilia::config::SideConfig &side, core::Color /*color*/)
  {
    model::analysis::PlayerInfo info{};
    info.elo = "";

    if (side.kind == lilia::config::SideKind::Human || !side.bot.has_value() ||
        side.bot->engine.engineId.empty())
    {
      info.name = "Challenger";
      info.icon_name = std::string(ui::icons::DEFAULT_FALLBACK);
      return info;
    }

    const auto &ref = side.bot->engine;
    info.name = ref.displayName.empty() ? ref.engineId : ref.displayName;
    info.icon_name = iconForEngine(ref);
    return info;
  }
} // namespace lilia::view
