#include "lilia/model/analysis/replay_info.hpp"

namespace lilia::model::analysis
{

  static std::string tagOr(const GameRecord &r, const char *k, const char *fallback = "")
  {
    auto it = r.tags.find(k);
    return (it != r.tags.end()) ? it->second : std::string(fallback);
  }

  ReplayInfo makeReplayInfo(const GameRecord &rec)
  {
    ReplayInfo info;

    info.event = tagOr(rec, "Event", "");
    info.site = tagOr(rec, "Site", "");
    info.date = tagOr(rec, "Date", "");
    info.round = tagOr(rec, "Round", "");

    info.white.name = tagOr(rec, "White", "White");
    info.black.name = tagOr(rec, "Black", "Black");

    info.white.elo = tagOr(rec, "WhiteElo", "");
    info.black.elo = tagOr(rec, "BlackElo", "");

    // Icons: leave empty here; controller/view applies a safe default.
    info.white.iconPath.clear();
    info.black.iconPath.clear();

    info.result = rec.result.empty() ? tagOr(rec, "Result", "*") : rec.result;

    info.whiteOutcome = outcome_for_white_result(info.result);
    info.blackOutcome = invert_outcome(info.whiteOutcome);

    info.eco = tagOr(rec, "ECO", "");
    // openingName filled via ECO lookup later
    return info;
  }

} // namespace lilia::model::analysis
