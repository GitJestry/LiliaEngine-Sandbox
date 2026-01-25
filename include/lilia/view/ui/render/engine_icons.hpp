#pragma once
#include <string_view>

namespace lilia::view::ui::icons
{
  // Paths are relative to your runtime assets directory copy (the "assets" folder next to the exe).
  inline constexpr std::string_view LILIA = "lilia_transparent.png";
  inline constexpr std::string_view STOCKFISH = "stockfish.png";
  inline constexpr std::string_view EXTERNAL = "external.png";

  inline constexpr std::string_view DEFAULT_FALLBACK = "challenger.png";
}
