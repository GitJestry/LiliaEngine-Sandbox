#pragma once
#include <string>
#include <string_view>

#include "lilia/model/analysis/game_record.hpp"

namespace lilia::model::analysis
{
  bool parsePgnToRecord(std::string_view pgn, GameRecord &out, std::string *err = nullptr);
}
