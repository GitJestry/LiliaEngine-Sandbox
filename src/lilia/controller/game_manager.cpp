#include "lilia/controller/game_manager.hpp"

#include <chrono>
#include <future>

#include "lilia/controller/bot_player.hpp"
#include "lilia/controller/player.hpp"
#include "lilia/model/chess_game.hpp"

namespace lilia::controller {

GameManager::GameManager(model::ChessGame &model) : m_game(model) {}

GameManager::~GameManager() {
  stopGame();
}

void GameManager::startGame(const std::string &fen, bool whiteIsBot, bool blackIsBot,
                            int whiteThinkTimeMs, int whiteDepth, int blackThinkTimeMs,
                            int blackDepth) {
  std::lock_guard lock(m_mutex);
  m_game.setPosition(fen);
  m_cancel_bot.store(false);
  m_waiting_promotion = false;

  if (whiteIsBot)
    m_white_player = std::make_unique<BotPlayer>(whiteThinkTimeMs, whiteDepth);
  else
    m_white_player.reset();

  if (blackIsBot)
    m_black_player = std::make_unique<BotPlayer>(blackThinkTimeMs, blackDepth);
  else
    m_black_player.reset();

  startBotIfNeeded();
}

void GameManager::stopGame() {
  std::future<model::Move> botFuture;
  {
    std::lock_guard lock(m_mutex);
    m_cancel_bot.store(true);
    if (m_bot_future.valid()) {
      botFuture = std::move(m_bot_future);
    }
  }

  if (botFuture.valid()) botFuture.wait();
}

void GameManager::update([[maybe_unused]] float dt) {
  std::lock_guard lock(m_mutex);
  using namespace std::chrono_literals;
  if (m_bot_future.valid()) {
    if (m_bot_future.wait_for(1ms) == std::future_status::ready) {
      model::Move mv = m_bot_future.get();

      m_bot_future = std::future<model::Move>();
      if (!(mv.from() == core::NO_SQUARE && mv.to() == core::NO_SQUARE)) {
        applyMoveAndNotify(mv, false);
      }
      startBotIfNeeded();
    }
  }
}

bool GameManager::requestUserMove(core::Square from, core::Square to, bool onClick,
                                  core::PieceType promotion) {
  std::lock_guard lock(m_mutex);
  if (m_waiting_promotion) return false;  // waiting on previous promotion
  if (!isHuman(m_game.getGameState().sideToMove)) return false;

  const auto &moves = m_game.generateLegalMoves();
  for (const auto &m : moves) {
    if (m.from() == from && m.to() == to) {
      if (m.promotion() != core::PieceType::None) {
        // If caller already provided promotion piece, apply immediately.
        if (promotion != core::PieceType::None && promotion == m.promotion()) {
          applyMoveAndNotify(m, onClick);
          startBotIfNeeded();
          return true;
        }
        // Otherwise, request UI selection as before.
        m_waiting_promotion = true;
        m_promotion_from = from;
        m_promotion_to = to;
        if (onPromotionRequested_) onPromotionRequested_(to);
        return false;
      }

      applyMoveAndNotify(m, onClick);

      startBotIfNeeded();
      return true;
    }
  }
  return false;
}

void GameManager::completePendingPromotion(core::PieceType promotion) {
  std::lock_guard lock(m_mutex);
  if (!m_waiting_promotion) return;

  const auto &moves = m_game.generateLegalMoves();
  for (const auto &m : moves) {
    if (m.from() == m_promotion_from && m.to() == m_promotion_to && m.promotion() == promotion) {
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

void GameManager::applyMoveAndNotify(const model::Move &mv, bool onClick) {
  const core::Color mover = m_game.getGameState().sideToMove;
  if (!m_game.doMove(mv.from(), mv.to(), mv.promotion())) {
    return;
  }

  bool wasPlayerMove = isHuman(mover);

  if (onMoveExecuted_) onMoveExecuted_(mv, wasPlayerMove, onClick);

  auto result = m_game.getResult();
  if (result != core::GameResult::ONGOING) {
    if (onGameEnd_) onGameEnd_(result);
    // cancel any running bot
    m_cancel_bot.store(true);
  }
}

void GameManager::startBotIfNeeded() {
  core::Color stm = m_game.getGameState().sideToMove;
  IPlayer *p = nullptr;
  if (stm == core::Color::White)
    p = m_white_player.get();
  else
    p = m_black_player.get();

  if (p && !p->isHuman()) {
    // cancel any running bot
    m_cancel_bot.store(true);
    // small window to allow previous future to see cancel and exit
    m_cancel_bot.store(false);

    m_pending_bot_player = p;
    m_bot_future = p->requestMove(m_game, m_cancel_bot);
  }
}

void GameManager::setBotForColor(core::Color color, std::unique_ptr<IPlayer> bot) {
  std::lock_guard lock(m_mutex);
  if (color == core::Color::White)
    m_white_player = std::move(bot);
  else
    m_black_player = std::move(bot);
}

bool GameManager::isHuman(core::Color color) const {
  const IPlayer *p = (color == core::Color::White) ? m_white_player.get() : m_black_player.get();
  return !p || p->isHuman();
}

bool GameManager::isHumanTurn() const {
  return isHuman(m_game.getGameState().sideToMove);
}

}  // namespace lilia::controller
