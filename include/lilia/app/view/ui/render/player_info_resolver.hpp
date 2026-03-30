#pragma once
#include <string>

#include "lilia/app/domain/analysis/config/start_config.hpp"
#include "lilia/chess/chess_types.hpp"
#include "lilia/app/domain/replay_info.hpp"

namespace lilia::app::view::ui
{
  // Builds UI-facing player info from a side config (human or engine).
  // - Uses engine displayName/version from EngineRef.
  // - Selects icon by engineId policy.
  domain::PlayerInfo makePlayerInfo(const domain::analysis::config::SideConfig &side, chess::Color color);
}
