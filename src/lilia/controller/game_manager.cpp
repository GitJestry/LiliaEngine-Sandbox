#include "lilia/controller/game_manager.hpp"

#include <chrono>
#include "lilia/controller/player.hpp"
#include "lilia/model/chess_game.hpp"
#include "lilia/model/analysis/config/start_config.hpp"
#include "lilia/controller/uci_engine_player.hpp"
#include "lilia/engine/uci/engine_registry.hpp"

namespace lilia::controller
{

  GameManager::GameManager(model::ChessGame &model) : m_game(model) {}

  GameManager::~GameManager()
  {
    stopGame();
  }

  void GameManager::startGame(const lilia::config::StartConfig &cfg)
  {
    std::lock_guard lock(m_mutex);

    m_game.setPosition(cfg.game.startFen);
    m_cancel_bot.store(false);
    m_waiting_promotion = false;

    auto makeSide = [&](const lilia::config::SideConfig &sc) -> std::unique_ptr<IPlayer>
    {
      if (sc.kind == lilia::config::SideKind::Human || !sc.bot.has_value())
        return {};

      // If UI did not populate uciValues yet, fill defaults from registry cache
      auto bc = *sc.bot;
      if (bc.uciValues.empty() && !bc.engine.engineId.empty())
      {
        bc = lilia::engine::uci::EngineRegistry::instance().makeDefaultBotConfig(bc.engine.engineId);
        bc.limits = sc.bot->limits; // keep chosen limits if set
      }
      return std::make_unique<UciEnginePlayer>(std::move(bc));
    };

    m_white_player = makeSide(cfg.white);
    m_black_player = makeSide(cfg.black);

    m_game.checkGameResult();
    if (m_game.getResult() != core::GameResult::ONGOING)
    {
      m_cancel_bot.store(true);
      if (onGameEnd_)
        onGameEnd_(m_game.getResult());
      return;
    }

    startBotIfNeeded();
  }

  void GameManager::stopGame()
  {
    std::lock_guard lock(m_mutex);
    m_cancel_bot.store(true);
  }

  void GameManager::update([[maybe_unused]] float dt)
  {
    std::lock_guard lock(m_mutex);
    using namespace std::chrono_literals;
    if (m_bot_future.valid())
    {
      if (m_bot_future.wait_for(1ms) == std::future_status::ready)
      {
        model::Move mv = m_bot_future.get();

        m_bot_future = std::future<model::Move>();
        if (!(mv.from() == core::NO_SQUARE && mv.to() == core::NO_SQUARE))
        {
          applyMoveAndNotify(mv, false);
        }
        startBotIfNeeded();
      }
    }
  }

  bool GameManager::requestUserMove(core::Square from, core::Square to, bool onClick,
                                    core::PieceType promotion)
  {
    std::lock_guard lock(m_mutex);
    if (m_waiting_promotion)
      return false; // waiting on previous promotion
    if (!isHuman(m_game.getGameState().sideToMove))
      return false;

    const auto &moves = m_game.generateLegalMoves();
    for (const auto &m : moves)
    {
      if (m.from() == from && m.to() == to)
      {
        if (m.promotion() != core::PieceType::None)
        {
          // If caller already provided promotion piece, apply immediately.
          if (promotion != core::PieceType::None && promotion == m.promotion())
          {
            applyMoveAndNotify(m, onClick);
            startBotIfNeeded();
            return true;
          }
          // Otherwise, request UI selection as before.
          m_waiting_promotion = true;
          m_promotion_from = from;
          m_promotion_to = to;
          if (onPromotionRequested_)
            onPromotionRequested_(to);
          return false;
        }

        applyMoveAndNotify(m, onClick);

        startBotIfNeeded();
        return true;
      }
    }
    return false;
  }

  void GameManager::completePendingPromotion(core::PieceType promotion)
  {
    std::lock_guard lock(m_mutex);
    if (!m_waiting_promotion)
      return;

    const auto &moves = m_game.generateLegalMoves();
    for (const auto &m : moves)
    {
      if (m.from() == m_promotion_from && m.to() == m_promotion_to && m.promotion() == promotion)
      {
        applyMoveAndNotify(m, true);
        m_waiting_promotion = false;
        startBotIfNeeded();
        return;
      }
    }

    // if we reach here, the promotion selection did not match available moves ->
    // cancel
    m_waiting_promotion = false;
  }

  void GameManager::applyMoveAndNotify(const model::Move &mv, bool onClick)
  {
    const core::Color mover = m_game.getGameState().sideToMove;
    if (!m_game.doMove(mv.from(), mv.to(), mv.promotion()))
    {
      return;
    }

    bool wasPlayerMove = isHuman(mover);

    if (onMoveExecuted_)
      onMoveExecuted_(mv, wasPlayerMove, onClick);

    auto result = m_game.getResult();
    if (result != core::GameResult::ONGOING)
    {
      if (onGameEnd_)
        onGameEnd_(result);
      // cancel any running bot
      m_cancel_bot.store(true);
    }
  }

  void GameManager::startBotIfNeeded()
  {
    core::Color stm = m_game.getGameState().sideToMove;
    IPlayer *p = nullptr;
    if (stm == core::Color::White)
      p = m_white_player.get();
    else
      p = m_black_player.get();

    if (p && !p->isHuman())
    {
      // cancel any running bot
      m_cancel_bot.store(true);
      // small window to allow previous future to see cancel and exit
      m_cancel_bot.store(false);

      m_pending_bot_player = p;
      m_bot_future = p->requestMove(m_game, m_cancel_bot);
    }
  }

  void GameManager::setBotForColor(core::Color color, std::unique_ptr<IPlayer> bot)
  {
    std::lock_guard lock(m_mutex);
    if (color == core::Color::White)
      m_white_player = std::move(bot);
    else
      m_black_player = std::move(bot);
  }

  bool GameManager::isHuman(core::Color color) const
  {
    const IPlayer *p = (color == core::Color::White) ? m_white_player.get() : m_black_player.get();
    return !p || p->isHuman();
  }

  bool GameManager::isHumanTurn() const
  {
    return isHuman(m_game.getGameState().sideToMove);
  }

} // namespace lilia::controller
