#pragma once
#include <string>
#include <unordered_map>
#include <vector>

#include "analysis/analysis_types.hpp"
#include "lilia/chess/move.hpp"

namespace lilia::app::domain
{

  struct PlyRecord
  {
    chess::Move move;               // your existing move type
    analysis::TimeView timeAfter{}; // optional; ok to leave zeros if absent
  };

  struct GameRecord
  {
    std::unordered_map<std::string, std::string> tags;
    std::string startFen; // empty or initial fen means normal start
    analysis::TimeView startTime{};
    std::vector<PlyRecord> plies; // ply order
    std::string result{"*"};      // "1-0", "0-1", "1/2-1/2", "*"
  };

} // namespace lilia::model
