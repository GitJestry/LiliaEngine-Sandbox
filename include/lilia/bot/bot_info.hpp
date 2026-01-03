#pragma once

#include "lilia/player_info.hpp"

namespace lilia {
struct BotConfig {
  PlayerInfo info;
  int depth = 1;
  int thinkTimeMs = 1;
};
enum class BotType { Lilia };

BotConfig getBotConfig(BotType type);
}  // namespace lilia
