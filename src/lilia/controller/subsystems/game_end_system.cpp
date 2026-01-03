#include "lilia/controller/subsystems/game_end_system.hpp"

#include "lilia/model/chess_game.hpp"
#include "lilia/controller/subsystems/clock_system.hpp"
#include "lilia/controller/subsystems/premove_system.hpp"

namespace lilia::controller {

GameEndSystem::GameEndSystem(view::GameView& view, model::ChessGame& game,
                             view::sound::SoundManager& sfx)
    : m_view(view), m_game(game), m_sfx(sfx) {}

void GameEndSystem::show(core::GameResult res, core::Color sideToMove, bool whiteIsBot,
                         bool blackIsBot, ClockSystem& clock, PremoveSystem& premove) {
  premove.clearAll();

  if (clock.enabled()) {
    clock.stop();
    m_view.setClockActive(std::nullopt);
    m_view.updateClock(core::Color::White, clock.time(core::Color::White));
    m_view.updateClock(core::Color::Black, clock.time(core::Color::Black));
  }

  m_sfx.playEffect(view::sound::Effect::GameEnds);

  std::string resultStr;
  const core::Color winner =
      (sideToMove == core::Color::White) ? core::Color::Black : core::Color::White;
  const bool humanWinner = (winner == core::Color::White && !whiteIsBot) ||
                           (winner == core::Color::Black && !blackIsBot);

  switch (res) {
    case core::GameResult::CHECKMATE:
      resultStr = (sideToMove == core::Color::White) ? "0-1" : "1-0";
      m_view.showGameOverPopup(sideToMove == core::Color::White ? "Black won" : "White won",
                               humanWinner);
      break;
    case core::GameResult::TIMEOUT:
      resultStr = (sideToMove == core::Color::White) ? "0-1" : "1-0";
      m_view.showGameOverPopup(
          sideToMove == core::Color::White ? "Black wins on time" : "White wins on time",
          humanWinner);
      break;
    case core::GameResult::REPETITION:
      resultStr = "1/2-1/2";
      m_view.showGameOverPopup("Draw by repetition", false);
      break;
    case core::GameResult::MOVERULE:
      resultStr = "1/2-1/2";
      m_view.showGameOverPopup("Draw by 50 move rule", false);
      break;
    case core::GameResult::STALEMATE:
      resultStr = "1/2-1/2";
      m_view.showGameOverPopup("Stalemate", false);
      break;
    case core::GameResult::INSUFFICIENT:
      resultStr = "1/2-1/2";
      m_view.showGameOverPopup("Insufficient material", false);
      break;
    default:
      resultStr = "error";
      m_view.showGameOverPopup("result is not correct", false);
      break;
  }

  m_view.addResult(resultStr);
  m_view.setGameOver(true);
}

}  // namespace lilia::controller
