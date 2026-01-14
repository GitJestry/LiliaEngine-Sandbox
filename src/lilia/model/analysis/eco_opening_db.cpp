#include "lilia/model/analysis/eco_opening_db.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace lilia::model::analysis
{
  namespace
  {
    std::once_flag g_once;
    std::unordered_map<std::string, std::string> g_map;

    void initBuiltin()
    {
      // Minimal built-in set. Extend freely or load a TSV for full coverage.
      // IMPORTANT: includes B28 as requested.
      const std::pair<const char *, const char *> builtin[] = {
          {"A00", "Uncommon Opening"},
          {"A04", "Reti Opening"},
          {"A10", "English Opening"},
          {"A40", "Queen's Pawn"},
          {"B00", "King's Pawn Opening"},
          {"B01", "Scandinavian Defense"},
          {"B07", "Pirc Defense"},
          {"B10", "Caro-Kann Defense"},
          {"B12", "Caro-Kann: Advance Variation"},
          {"B20", "Sicilian Defense"},
          {"B22", "Sicilian Defense: Alapin Variation"},
          {"B23", "Sicilian Defense: Closed"},
          {"B27", "Sicilian Defense"},
          {"B28", "Sicilian Defense: O'Kelly Variation"}, // requested
          {"B30", "Sicilian Defense: Rossolimo Variation"},
          {"B40", "Sicilian Defense: Kan Variation"},
          {"B50", "Sicilian Defense"},
          {"B70", "Sicilian Defense: Dragon Variation"},
          {"B90", "Sicilian Defense: Najdorf Variation"},
          {"C00", "French Defense"},
          {"C10", "French Defense"},
          {"C20", "King's Pawn Game"},
          {"C30", "King's Gambit"},
          {"C40", "King's Knight Opening"},
          {"C50", "Italian Game"},
          {"C60", "Ruy Lopez"},
          {"D00", "Queen's Pawn Game"},
          {"D10", "Slav Defense"},
          {"D30", "Queen's Gambit Declined"},
          {"D40", "Queen's Gambit Declined"},
          {"E00", "Catalan Opening"},
          {"E20", "Nimzo-Indian Defense"},
      };

      for (const auto &kv : builtin)
        g_map.emplace(kv.first, kv.second);
    }

    static inline std::string trimCopy(std::string s)
    {
      auto isSpace = [](unsigned char c)
      { return std::isspace(c) != 0; };
      while (!s.empty() && isSpace((unsigned char)s.front()))
        s.erase(s.begin());
      while (!s.empty() && isSpace((unsigned char)s.back()))
        s.pop_back();
      return s;
    }
  } // namespace

  std::string EcoOpeningDb::normalizeEco(std::string_view eco)
  {
    std::string s(eco);
    s = trimCopy(std::move(s));
    // Accept variants like "eco B28" or "ECO:B28"
    for (char &c : s)
      c = char(std::toupper((unsigned char)c));

    // Extract the first pattern [A-E][0-9][0-9]
    for (std::size_t i = 0; i + 2 < s.size(); ++i)
    {
      char a = s[i];
      char b = s[i + 1];
      char c = s[i + 2];
      if (a >= 'A' && a <= 'E' && std::isdigit((unsigned char)b) && std::isdigit((unsigned char)c))
      {
        return std::string{s.substr(i, 3)};
      }
    }
    return {};
  }

  bool EcoOpeningDb::looksLikeEco(std::string_view sv)
  {
    std::string s = normalizeEco(sv);
    return s.size() == 3 && (s[0] >= 'A' && s[0] <= 'E') &&
           std::isdigit((unsigned char)s[1]) && std::isdigit((unsigned char)s[2]);
  }

  std::string EcoOpeningDb::nameForEco(std::string_view eco)
  {
    std::call_once(g_once, []()
                   { initBuiltin(); });

    const std::string key = normalizeEco(eco);
    if (key.empty())
      return {};

    auto it = g_map.find(key);
    if (it == g_map.end())
      return {};
    return it->second;
  }

  std::string EcoOpeningDb::resolveOpeningTitle(std::string_view eco, std::string_view openingTag)
  {
    // If PGN Opening tag is missing or is actually just an ECO code, resolve via DB.
    if (openingTag.empty() || looksLikeEco(openingTag))
    {
      const std::string nm = nameForEco(eco);
      if (!nm.empty())
        return nm;
      // Fall back to normalized ECO if we have it.
      const std::string key = normalizeEco(eco);
      return key.empty() ? std::string{} : ("ECO " + key);
    }

    return std::string(openingTag);
  }

  bool EcoOpeningDb::loadFromTsvFile(const std::string &path)
  {
    std::call_once(g_once, []()
                   { initBuiltin(); });

    std::ifstream in(path);
    if (!in)
      return false;

    std::string line;
    std::size_t added = 0;

    while (std::getline(in, line))
    {
      line = trimCopy(std::move(line));
      if (line.empty() || line[0] == '#')
        continue;

      const auto tabPos = line.find('\t');
      if (tabPos == std::string::npos)
        continue;

      std::string eco = line.substr(0, tabPos);
      std::string name = line.substr(tabPos + 1);

      eco = normalizeEco(eco);
      name = trimCopy(std::move(name));

      if (eco.size() != 3 || name.empty())
        continue;

      g_map[eco] = name;
      ++added;
    }

    return added > 0;
  }

} // namespace lilia::model::analysis
