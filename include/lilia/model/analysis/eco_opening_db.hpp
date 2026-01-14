#pragma once

#include <string>
#include <string_view>

namespace lilia::model::analysis
{
  // Lightweight ECO -> Opening name lookup.
  // - Built-in minimal table (covers common codes + requested B28).
  // - Optional extension via TSV file if you want "full database" without recompiling.
  //
  // TSV format:
  //   B28<TAB>Sicilian Defense: O'Kelly Variation
  //
  class EcoOpeningDb final
  {
  public:
    // Returns a human name for an ECO code, or empty string if unknown.
    static std::string nameForEco(std::string_view eco);

    // Convenience: if 'openingTag' is empty or looks like an ECO code,
    // returns a better display title using the database; otherwise returns openingTag.
    static std::string resolveOpeningTitle(std::string_view eco, std::string_view openingTag);

    // Optional: call once at startup if you want to load a larger DB from disk.
    // Safe to call multiple times; later loads overwrite/extend earlier mappings.
    static bool loadFromTsvFile(const std::string &path);

  private:
    static std::string normalizeEco(std::string_view eco);
    static bool looksLikeEco(std::string_view s);
  };

} // namespace lilia::model::analysis
