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
#include "lilia/model/analysis/game_record.hpp"
#include "lilia/model/chess_game.hpp"
#include "lilia/model/analysis/replay_info.hpp"
#include "lilia/model/analysis/replay_info.hpp"
#include "lilia/constants.hpp"

namespace lilia::controller
{

  namespace
  {
    void resignThunk(void *ctx)
    {
      static_cast<GameController *>(ctx)->handleEvent(sf::Event{});
    }
  } // namespace

  GameController::GameController(view::GameView &gView, model::ChessGame &game)
      : m_view(gView), m_game(game), m_input(), m_sfx(), m_selection(gView)
  {
    m_sfx.loadSounds();

    m_game_manager = std::make_unique<GameManager>(game);
    BotPlayer::setEvalCallback([this](int eval)
                               { m_eval_cp.store(eval); });

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

    m_ui->setResignHandler([](void *ctx)
                           { static_cast<GameController *>(ctx)->resign(); }, this);

    m_game_manager->setOnMoveExecuted([this](const model::Move &mv, bool isPlayerMove, bool onClick)
                                      {
    m_history->ensureHeadVisibleForLivePlay();

    m_move_exec->applyMove(mv, isPlayerMove, onClick);

    m_game.checkGameResult();
    if (m_game.getResult() != core::GameResult::ONGOING) {
      m_game_end->show(m_game.getResult(), m_game.getGameState().sideToMove, m_white_is_bot,
                       m_black_is_bot, *m_clock, *m_premove);
    } });

    m_game_manager->setOnPromotionRequested([this](core::Square sq)
                                            { m_view.playPromotionSelectAnim(sq, m_game.getGameState().sideToMove); });

    m_game_manager->setOnGameEnd([this](core::GameResult res)
                                 { m_game_end->show(res, m_game.getGameState().sideToMove, m_white_is_bot, m_black_is_bot,
                                                    *m_clock, *m_premove); });
  }

  GameController::~GameController() = default;

  void GameController::startGame(const std::string &fen, bool whiteIsBot, bool blackIsBot,
                                 int whiteThinkTimeMs, int whiteDepth, int blackThinkTimeMs,
                                 int blackDepth, bool useTimer, int baseSeconds,
                                 int incrementSeconds)
  {
    m_view.clearReplayHeader();
    m_replay_mode = false;
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
    if (useTimer)
      m_clock->start(stm);

    const model::analysis::TimeView tv =
        useTimer ? model::analysis::TimeView{static_cast<float>(baseSeconds), static_cast<float>(baseSeconds), stm}
                 : model::analysis::TimeView{0.f, 0.f, stm};

    m_history->reset(fen, tv);

    m_selection.reset();
    m_view.setDefaultCursor();

    m_next_action = NextAction::None;
  }

  void GameController::render()
  {
    m_view.render();
  }

  void GameController::update(float dt)
  {
    m_view.update(dt);
    m_history->updateEvalAtHead();

    if (m_replay_mode)
      return;

    if (m_game.getResult() != core::GameResult::ONGOING)
      return;

    if (m_clock->enabled())
    {
      m_clock->update(dt);
      if (auto flag = m_clock->flagged())
      {
        m_game.setResult(core::GameResult::TIMEOUT);
        if (m_game_manager)
          m_game_manager->stopGame();
        m_game_end->show(core::GameResult::TIMEOUT, *flag, m_white_is_bot, m_black_is_bot, *m_clock,
                         *m_premove);
        return;
      }

      if (m_history->atHead())
      {
        m_view.updateClock(core::Color::White, m_clock->time(core::Color::White));
        m_view.updateClock(core::Color::Black, m_clock->time(core::Color::Black));
        m_view.setClockActive(m_clock->active());
      }
    }

    if (m_game_manager)
      m_game_manager->update(dt);

    m_premove->tickAutoMove();
    m_board_input->refreshActiveHighlights();
  }

#include "lilia/model/analysis/game_record.hpp"

  model::analysis::GameRecord GameController::buildGameRecord() const
  {
    model::analysis::GameRecord rec = m_history->toRecord();

    // Tags (you can extend these later)
    rec.tags["Event"] = "Lilia Sandbox";
    rec.tags["White"] = m_white_is_bot ? "Bot" : "Human";
    rec.tags["Black"] = m_black_is_bot ? "Bot" : "Human";
    rec.tags["Result"] = rec.result;

    if (!rec.startFen.empty() && rec.startFen != core::START_FEN)
    {
      rec.tags["SetUp"] = "1";
      rec.tags["FEN"] = rec.startFen;
    }

    return rec;
  }

#include "lilia/model/analysis/game_record.hpp"

  void GameController::startReplay(const model::analysis::GameRecord &rec)
  {
    m_legal->invalidate();

    m_view.hideResignPopup();
    m_view.hideGameOverPopup();
    m_view.setGameOver(false);

    const std::string startFen = rec.startFen.empty() ? core::START_FEN : rec.startFen;

    // No bots/game manager in replay mode.
    m_white_is_bot = false;
    m_black_is_bot = false;
    m_replay_mode = true;

    if (m_game_manager)
      m_game_manager->stopGame();

    m_view.init(startFen);
    m_view.setBotMode(false);

    m_premove->clearAll();

    // Disable clock updates in replay.
    m_clock->reset(false, 0, 0);

    // Build replay metadata from record tags/result.
    const model::analysis::ReplayInfo ri = model::analysis::makeReplayInfo(rec);

    model::analysis::ReplayInfo hdr;
    hdr.event = ri.event;
    hdr.site = ri.site;
    hdr.date = ri.date;
    hdr.round = ri.round;

    hdr.white = ri.white;
    hdr.black = ri.black;

    hdr.result = ri.result;
    hdr.whiteOutcome = ri.whiteOutcome;
    hdr.blackOutcome = ri.blackOutcome;

    hdr.eco = ri.eco;
    hdr.openingName = ri.openingName;

    // Ensure icon paths are valid to avoid TextureTable lookups on empty strings.
    // Adjust the namespace if your ICON_CHALLENGER constant lives elsewhere.
    if (hdr.white.iconPath.empty())
      hdr.white.iconPath = std::string{lilia::view::constant::path::ICON_CHALLENGER};
    if (hdr.black.iconPath.empty())
      hdr.black.iconPath = std::string{lilia::view::constant::path::ICON_CHALLENGER};

    m_view.setReplayHeader(std::move(hdr));

    // Reset and load history (this should fill move list using SAN if you implemented it there).
    m_history->loadFromRecord(rec, /*populateMoveListWithSan=*/true);

    // If record contains a final result, show it in move list + mark game over visuals.
    if (!rec.result.empty() && rec.result != "*")
    {
      m_view.addResult(rec.result);
      m_view.setGameOver(true);
    }

    m_selection.reset();
    m_view.setDefaultCursor();
    m_next_action = NextAction::None;
  }

  void GameController::handleEvent(const sf::Event &event)
  {
    if (m_ui->handleEvent(event))
      return;

    if (m_replay_mode)
      return;

    if (!m_history->atHead())
      return;

    if (m_game.getResult() != core::GameResult::ONGOING)
      return;

    switch (event.type)
    {
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

  void GameController::resign()
  {
    m_game_manager->stopGame();
    m_game.setResult(core::GameResult::CHECKMATE);
    m_view.clearAllHighlights();
    m_selection.highlightLastMove();

    core::Color loser = m_game.getGameState().sideToMove;
    if (m_game_manager && !m_game_manager->isHuman(loser))
      loser = ~loser;

    m_game_end->show(core::GameResult::CHECKMATE, loser, m_white_is_bot, m_black_is_bot, *m_clock,
                     *m_premove);
  }

} // namespace lilia::controller
