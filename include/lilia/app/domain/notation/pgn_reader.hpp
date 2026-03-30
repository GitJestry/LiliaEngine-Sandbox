#pragma once
#include <string>
#include <string_view>

#include "lilia/app/domain/game_record.hpp"

namespace lilia::app::domain::notation
{
  bool parsePgnToRecord(std::string_view pgn, GameRecord &out, std::string *err = nullptr);
}
