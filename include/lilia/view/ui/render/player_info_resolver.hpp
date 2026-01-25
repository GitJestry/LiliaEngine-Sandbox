#pragma once
#include <string>

#include "lilia/model/analysis/config/start_config.hpp"
#include "lilia/chess_types.hpp"
#include "lilia/model/analysis/replay_info.hpp"

namespace lilia::view
{
  // Builds UI-facing player info from a side config (human or engine).
  // - Uses engine displayName/version from EngineRef.
  // - Selects icon by engineId policy.
  model::analysis::PlayerInfo makePlayerInfo(const lilia::config::SideConfig &side, core::Color color);
}
