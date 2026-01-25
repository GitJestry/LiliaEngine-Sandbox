#pragma once
#include <optional>
#include <string>

#include "lilia/model/analysis/game_record.hpp"
#include "lilia/model/analysis/outcome.hpp"

namespace lilia::model::analysis
{

  struct PlayerInfo
  {
    std::string name;
    std::string elo;
    std::string icon_name;
  };

  struct ReplayInfo
  {
    std::string event;
    std::string site;
    std::string date;
    std::string round;

    PlayerInfo white_info;
    PlayerInfo black_info;

    std::string result; // "1-0", "0-1", "1/2-1/2", "*"
    Outcome whiteOutcome = Outcome::Unknown;
    Outcome blackOutcome = Outcome::Unknown;

    std::string eco;         // "B28"
    std::string openingName; // optional
  };

  ReplayInfo makeReplayInfo(const GameRecord &rec);

} // namespace lilia::model::analysis
