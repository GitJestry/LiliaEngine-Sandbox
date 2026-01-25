#include "lilia/controller/subsystems/game_end_system.hpp"

#include "lilia/controller/subsystems/clock_system.hpp"
#include "lilia/controller/subsystems/premove_system.hpp"
#include "lilia/model/analysis/result_utils.hpp"

namespace lilia::controller
{

  GameEndSystem::GameEndSystem(view::GameView &view,
                               view::sound::SoundManager &sfx)
      : m_view(view), m_sfx(sfx) {}

  void GameEndSystem::show(core::GameResult res, core::Color sideToMove, bool whiteIsBot,
                           bool blackIsBot, ClockSystem &clock, PremoveSystem &premove)
  {
    premove.clearAll();

    if (clock.enabled())
    {
      clock.stop();
      m_view.setClockActive(std::nullopt);
      m_view.updateClock(core::Color::White, clock.time(core::Color::White));
      m_view.updateClock(core::Color::Black, clock.time(core::Color::Black));
    }

    m_sfx.playEffect(view::sound::Effect::GameEnds);

    // Use shared model utility (single source of truth)
    const std::string resultStr =
        model::analysis::result_string(res, sideToMove, /*forPgn=*/false);

    // Compute and display outcome badges for both players
    using model::analysis::Outcome;
    const Outcome whiteOut = model::analysis::outcome_for_white_result(resultStr);
    const Outcome blackOut = model::analysis::invert_outcome(whiteOut);

    auto opt = [](Outcome o) -> std::optional<Outcome>
    {
      return (o == Outcome::Unknown) ? std::nullopt : std::optional<Outcome>(o);
    };
    m_view.setOutcomeBadges(opt(whiteOut), opt(blackOut));

    // Popup text (still view-facing; keep it here)
    switch (res)
    {
    case core::GameResult::CHECKMATE:
    {
      const core::Color winner =
          (sideToMove == core::Color::White) ? core::Color::Black : core::Color::White;
      const bool humanWinner = (winner == core::Color::White && !whiteIsBot) ||
                               (winner == core::Color::Black && !blackIsBot);

      m_view.showGameOverPopup(sideToMove == core::Color::White ? "Black won" : "White won",
                               humanWinner);
      break;
    }

    case core::GameResult::TIMEOUT:
    {
      const core::Color winner =
          (sideToMove == core::Color::White) ? core::Color::Black : core::Color::White;
      const bool humanWinner = (winner == core::Color::White && !whiteIsBot) ||
                               (winner == core::Color::Black && !blackIsBot);

      m_view.showGameOverPopup(
          sideToMove == core::Color::White ? "Black wins on time" : "White wins on time",
          humanWinner);
      break;
    }

    case core::GameResult::REPETITION:
      m_view.showGameOverPopup("Draw by repetition", false);
      break;
    case core::GameResult::MOVERULE:
      m_view.showGameOverPopup("Draw by 50 move rule", false);
      break;
    case core::GameResult::STALEMATE:
      m_view.showGameOverPopup("Stalemate", false);
      break;
    case core::GameResult::INSUFFICIENT:
      m_view.showGameOverPopup("Insufficient material", false);
      break;

    default:
      m_view.showGameOverPopup("result is not correct", false);
      break;
    }

    m_view.addResult(resultStr);
    m_view.setGameOver(true);
  }

} // namespace lilia::controller
