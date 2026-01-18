#pragma once
#include <string_view>

namespace lilia::view::ui::icons
{
  // Paths are relative to your runtime assets directory copy (the "assets" folder next to the exe).
  inline constexpr std::string_view LILIA = "assets/icons/lilia_transparent.png";
  inline constexpr std::string_view STOCKFISH = "assets/icons/stockfish.png";
  inline constexpr std::string_view EXTERNAL = "assets/icons/external.png";

  inline constexpr std::string_view DEFAULT_FALLBACK = "assets/icons/challenger.png";
}
