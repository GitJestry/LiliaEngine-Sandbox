#pragma once

#include <string_view>

namespace lilia::model::analysis
{

  enum class Outcome
  {
    Win,
    Loss,
    Draw,
    Unknown
  };

  // PGN result interpretation from White's perspective.
  inline Outcome outcome_for_white_result(std::string_view res) noexcept
  {
    if (res == "1-0")
      return Outcome::Win;
    if (res == "0-1")
      return Outcome::Loss;
    if (res == "1/2-1/2")
      return Outcome::Draw;
    return Outcome::Unknown;
  }

  inline Outcome invert_outcome(Outcome o) noexcept
  {
    switch (o)
    {
    case Outcome::Win:
      return Outcome::Loss;
    case Outcome::Loss:
      return Outcome::Win;
    case Outcome::Draw:
      return Outcome::Draw;
    default:
      return Outcome::Unknown;
    }
  }

} // namespace lilia::model::analysis
