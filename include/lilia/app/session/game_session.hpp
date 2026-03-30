#pragma once
#include <SFML/Graphics/RenderWindow.hpp>

#include "lilia/app/controller/game_controller_types.hpp"
#include "lilia/app/domain/analysis/config/start_config.hpp"

namespace lilia::app::session
{
  /// @brief Runs exactly one session (a game or replay)
  /// @param window
  /// @param cfg
  /// @return returns the next controller Action
  lilia::app::controller::NextAction runSession(sf::RenderWindow &window,
                                                const domain::analysis::config::StartConfig &cfg);
} // namespace lilia::app
