#include "lilia/app/controller/subsystems/game_end_system.hpp"

#include "lilia/app/controller/subsystems/clock_system.hpp"
#include "lilia/app/controller/subsystems/premove_system.hpp"
#include "lilia/app/domain/result_utils.hpp"

namespace lilia::app::controller
{

  GameEndSystem::GameEndSystem(view::ui::GameView &view,
                               view::audio::SoundManager &sfx)
      : m_view(view), m_sfx(sfx) {}

  void GameEndSystem::show(chess::GameResult res, chess::Color sideToMove, bool whiteIsBot,
                           bool blackIsBot, ClockSystem &clock, PremoveSystem &premove)
  {
    premove.clearAll();

    if (clock.enabled())
    {
      clock.stop();
      m_view.setClockActive(std::nullopt);
      m_view.updateClock(chess::Color::White, clock.time(chess::Color::White));
      m_view.updateClock(chess::Color::Black, clock.time(chess::Color::Black));
    }

    m_sfx.playEffect(view::audio::Effect::GameEnds);

    // Use shared model utility (single source of truth)
    const std::string resultStr =
        domain::result_string(res, sideToMove, /*forPgn=*/false);

    const domain::Outcome whiteOut = domain::outcome_for_white_result(resultStr);
    const domain::Outcome blackOut = domain::invert_outcome(whiteOut);

    auto opt = [](domain::Outcome o) -> std::optional<domain::Outcome>
    {
      return (o == domain::Outcome::Unknown) ? std::nullopt : std::optional<domain::Outcome>(o);
    };
    m_view.setOutcomeBadges(opt(whiteOut), opt(blackOut));

    // Popup text (still view-facing; keep it here)
    switch (res)
    {
    case chess::GameResult::Checkmate:
    {
      const chess::Color winner =
          (sideToMove == chess::Color::White) ? chess::Color::Black : chess::Color::White;
      const bool humanWinner = (winner == chess::Color::White && !whiteIsBot) ||
                               (winner == chess::Color::Black && !blackIsBot);

      m_view.showGameOverPopup(sideToMove == chess::Color::White ? "Black won" : "White won",
                               humanWinner);
      break;
    }

    case chess::GameResult::Timeout:
    {
      const chess::Color winner =
          (sideToMove == chess::Color::White) ? chess::Color::Black : chess::Color::White;
      const bool humanWinner = (winner == chess::Color::White && !whiteIsBot) ||
                               (winner == chess::Color::Black && !blackIsBot);

      m_view.showGameOverPopup(
          sideToMove == chess::Color::White ? "Black wins on time" : "White wins on time",
          humanWinner);
      break;
    }

    case chess::GameResult::Repetition:
      m_view.showGameOverPopup("Draw by repetition", false);
      break;
    case chess::GameResult::MoveRule:
      m_view.showGameOverPopup("Draw by 50 move rule", false);
      break;
    case chess::GameResult::Stalemate:
      m_view.showGameOverPopup("Stalemate", false);
      break;
    case chess::GameResult::InsufficientMaterial:
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
