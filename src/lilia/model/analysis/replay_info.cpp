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

    info.white_info.name = tagOr(rec, "White", "White");
    info.black_info.name = tagOr(rec, "Black", "Black");

    info.white_info.elo = tagOr(rec, "WhiteElo", "");
    info.black_info.elo = tagOr(rec, "BlackElo", "");

    info.white_info.icon_name.clear();
    info.black_info.icon_name.clear();

    info.result = rec.result.empty() ? tagOr(rec, "Result", "*") : rec.result;

    info.whiteOutcome = outcome_for_white_result(info.result);
    info.blackOutcome = invert_outcome(info.whiteOutcome);

    info.eco = tagOr(rec, "ECO", "");
    // openingName filled via ECO lookup later
    return info;
  }

} // namespace lilia::model::analysis
