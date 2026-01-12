#pragma once
#include <string>
#include <unordered_map>
#include <vector>

#include "analysis_types.hpp"
#include "../move.hpp"

namespace lilia::model::analysis
{

  struct PlyRecord
  {
    model::Move move;     // your existing move type
    TimeView timeAfter{}; // optional; ok to leave zeros if absent
  };

  struct GameRecord
  {
    std::unordered_map<std::string, std::string> tags;
    std::string startFen; // empty or initial fen means normal start
    TimeView startTime{};
    std::vector<PlyRecord> plies; // ply order
    std::string result{"*"};      // "1-0", "0-1", "1/2-1/2", "*"
  };

} // namespace lilia::model
