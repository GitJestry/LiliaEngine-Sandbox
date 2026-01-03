#include "lilia/controller/game_controller.hpp"

#include <SFML/Window/Event.hpp>
#include <SFML/Window/Mouse.hpp>

#include "lilia/controller/bot_player.hpp"
#include "lilia/controller/game_manager.hpp"
#include "lilia/controller/subsystems/attack_system.hpp"
#include "lilia/controller/subsystems/board_input_system.hpp"
#include "lilia/controller/subsystems/clock_system.hpp"
#include "lilia/controller/subsystems/game_end_system.hpp"
#include "lilia/controller/subsystems/history_system.hpp"
#include "lilia/controller/subsystems/legal_move_cache.hpp"
#include "lilia/controller/subsystems/move_execution_system.hpp"
#include "lilia/controller/subsystems/premove_system.hpp"
#include "lilia/controller/subsystems/ui_event_system.hpp"
#include "lilia/model/chess_game.hpp"

namespace lilia::controller {

namespace {
void resignThunk(void* ctx) {
  static_cast<GameController*>(ctx)->handleEvent(sf::Event{});
}
}  // namespace

GameController::GameController(view::GameView& gView, model::ChessGame& game)
    : m_view(gView), m_game(game), m_input(), m_sfx(), m_selection(gView) {
  m_sfx.loadSounds();

  m_game_manager = std::make_unique<GameManager>(game);
  BotPlayer::setEvalCallback([this](int eval) { m_eval_cp.store(eval); });

  m_legal = std::make_unique<LegalMoveCache>(m_game);
  m_attacks = std::make_unique<AttackSystem>(m_view, m_game, *m_legal);
  m_premove = std::make_unique<PremoveSystem>(m_view, m_game, m_sfx, *m_legal);
  m_history = std::make_unique<HistorySystem>(m_view, m_game, m_selection, m_sfx, m_eval_cp);
  m_clock = std::make_unique<ClockSystem>(m_view, m_game);
  m_move_exec = std::make_unique<MoveExecutionSystem>(m_view, m_game, m_sfx, m_eval_cp, *m_legal,
                                                      *m_history, *m_clock, *m_premove);
  m_game_end = std::make_unique<GameEndSystem>(m_view, m_game, m_sfx);
  m_board_input = std::make_unique<BoardInputSystem>(m_view, m_game, m_input, m_selection, m_sfx,
                                                     *m_attacks, *m_premove, *m_legal);
  m_ui = std::make_unique<UiEventSystem>(m_view, m_game, *m_history, *m_premove, m_next_action);

  m_premove->setGameManager(m_game_manager.get());
  m_board_input->setGameManager(m_game_manager.get());
  m_board_input->bindInputCallbacks();

  m_ui->setResignHandler([](void* ctx) { static_cast<GameController*>(ctx)->resign(); }, this);

  m_game_manager->setOnMoveExecuted([this](const model::Move& mv, bool isPlayerMove, bool onClick) {
    m_history->ensureHeadVisibleForLivePlay();

    m_move_exec->applyMove(mv, isPlayerMove, onClick);

    m_game.checkGameResult();
    if (m_game.getResult() != core::GameResult::ONGOING) {
      m_game_end->show(m_game.getResult(), m_game.getGameState().sideToMove, m_white_is_bot,
                       m_black_is_bot, *m_clock, *m_premove);
    }
  });

  m_game_manager->setOnPromotionRequested([this](core::Square sq) {
    m_view.playPromotionSelectAnim(sq, m_game.getGameState().sideToMove);
  });

  m_game_manager->setOnGameEnd([this](core::GameResult res) {
    m_game_end->show(res, m_game.getGameState().sideToMove, m_white_is_bot, m_black_is_bot,
                     *m_clock, *m_premove);
  });
}

GameController::~GameController() = default;

void GameController::startGame(const std::string& fen, bool whiteIsBot, bool blackIsBot,
                               int whiteThinkTimeMs, int whiteDepth, int blackThinkTimeMs,
                               int blackDepth, bool useTimer, int baseSeconds,
                               int incrementSeconds) {
  m_legal->invalidate();

  m_sfx.playEffect(view::sound::Effect::GameBegins);

  m_view.hideResignPopup();
  m_view.hideGameOverPopup();
  m_view.setGameOver(false);

  m_view.init(fen);
  m_view.setBotMode(whiteIsBot || blackIsBot);

  m_white_is_bot = whiteIsBot;
  m_black_is_bot = blackIsBot;

  m_game_manager->startGame(fen, whiteIsBot, blackIsBot, whiteThinkTimeMs, whiteDepth,
                            blackThinkTimeMs, blackDepth);

  m_premove->clearAll();

  m_clock->reset(useTimer, baseSeconds, incrementSeconds);
  const core::Color stm = m_game.getGameState().sideToMove;
  if (useTimer) m_clock->start(stm);

  const TimeView tv =
      useTimer ? TimeView{static_cast<float>(baseSeconds), static_cast<float>(baseSeconds), stm}
               : TimeView{0.f, 0.f, stm};

  m_history->reset(fen, m_eval_cp.load(), tv);

  m_selection.reset();
  m_view.setDefaultCursor();

  m_next_action = NextAction::None;
}

void GameController::render() {
  m_view.render();
}

void GameController::update(float dt) {
  m_view.update(dt);
  m_history->updateEvalAtHead();

  if (m_game.getResult() != core::GameResult::ONGOING) return;

  if (m_clock->enabled()) {
    m_clock->update(dt);
    if (auto flag = m_clock->flagged()) {
      m_game.setResult(core::GameResult::TIMEOUT);
      if (m_game_manager) m_game_manager->stopGame();
      m_game_end->show(core::GameResult::TIMEOUT, *flag, m_white_is_bot, m_black_is_bot, *m_clock,
                       *m_premove);
      return;
    }

    if (m_history->atHead()) {
      m_view.updateClock(core::Color::White, m_clock->time(core::Color::White));
      m_view.updateClock(core::Color::Black, m_clock->time(core::Color::Black));
      m_view.setClockActive(m_clock->active());
    }
  }

  if (m_game_manager) m_game_manager->update(dt);

  m_premove->tickAutoMove();
  m_board_input->refreshActiveHighlights();
}

void GameController::handleEvent(const sf::Event& event) {
  if (m_ui->handleEvent(event)) return;

  if (!m_history->atHead()) return;

  if (m_game.getResult() != core::GameResult::ONGOING) return;

  switch (event.type) {
    case sf::Event::MouseMoved:
      m_board_input->onMouseMove(core::MousePos(event.mouseMove.x, event.mouseMove.y));
      break;
    case sf::Event::MouseButtonPressed:
      if (event.mouseButton.button == sf::Mouse::Left)
        m_board_input->onMousePressed(core::MousePos(event.mouseButton.x, event.mouseButton.y));
      else if (event.mouseButton.button == sf::Mouse::Right)
        m_board_input->onRightPressed(core::MousePos(event.mouseButton.x, event.mouseButton.y));
      break;
    case sf::Event::MouseButtonReleased:
      if (event.mouseButton.button == sf::Mouse::Left)
        m_board_input->onMouseReleased(core::MousePos(event.mouseButton.x, event.mouseButton.y));
      else if (event.mouseButton.button == sf::Mouse::Right)
        m_board_input->onRightReleased(core::MousePos(event.mouseButton.x, event.mouseButton.y));
      break;
    case sf::Event::MouseEntered:
      m_board_input->onMouseEntered();
      break;
    case sf::Event::LostFocus:
      m_board_input->onLostFocus();
      break;
    default:
      break;
  }

  m_input.processEvent(event);
}

void GameController::resign() {
  m_game_manager->stopGame();
  m_game.setResult(core::GameResult::CHECKMATE);
  m_view.clearAllHighlights();
  m_selection.highlightLastMove();

  core::Color loser = m_game.getGameState().sideToMove;
  if (m_game_manager && !m_game_manager->isHuman(loser)) loser = ~loser;

  m_game_end->show(core::GameResult::CHECKMATE, loser, m_white_is_bot, m_black_is_bot, *m_clock,
                   *m_premove);
}

}  // namespace lilia::controller
