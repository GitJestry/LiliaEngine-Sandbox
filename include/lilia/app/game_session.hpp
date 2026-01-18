#pragma once
#include <SFML/Graphics/RenderWindow.hpp>

#include "lilia/controller/game_controller_types.hpp"
#include "lilia/model/analysis/config/start_config.hpp"

namespace lilia::app
{
  // Runs exactly one session (a game or replay) and returns the next action.
  lilia::controller::NextAction runSession(sf::RenderWindow &window,
                                           const lilia::config::StartConfig &cfg);
} // namespace lilia::app
