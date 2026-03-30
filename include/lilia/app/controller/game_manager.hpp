#pragma once

#include <atomic>
#include <functional>
#include <future>
#include <memory>
#include <mutex>

#include "lilia/chess/chess_types.hpp"
#include "lilia/chess/move.hpp"
#include "lilia/app/domain/analysis/config/start_config.hpp"

namespace lilia::chess
{
  class ChessGame;
}

namespace lilia::app::controller
{

  struct IPlayer;

  class GameManager
  {
  public:
    using MoveCallback = std::function<void(const chess::Move &mv,
                                            bool isPlayerMove, bool onClick)>;
    using PromotionCallback = std::function<void(chess::Square promotionSquare)>;
    using EndCallback = std::function<void(chess::GameResult)>;

    explicit GameManager(chess::ChessGame &model);
    ~GameManager();

    void startGame(const domain::analysis::config::StartConfig &cfg);
    void stopGame();

    void update(float dt);

    bool requestUserMove(chess::Square from, chess::Square to, bool onClick,
                         chess::PieceType promotion = chess::PieceType::None);

    void completePendingPromotion(chess::PieceType promotion);

    void setOnMoveExecuted(MoveCallback cb)
    {
      onMoveExecuted_ = std::move(cb);
    }
    void setOnPromotionRequested(PromotionCallback cb)
    {
      onPromotionRequested_ = std::move(cb);
    }
    void setOnGameEnd(EndCallback cb)
    {
      onGameEnd_ = std::move(cb);
    }

    void setBotForColor(chess::Color color, std::unique_ptr<IPlayer> bot);

    [[nodiscard]] bool isHuman(chess::Color color) const;
    [[nodiscard]] bool isHumanTurn() const;

  private:
    chess::ChessGame &m_game;
    // Players: nullptr means human player
    std::unique_ptr<IPlayer> m_white_player;
    std::unique_ptr<IPlayer> m_black_player;

    // Bot future & cancel token
    std::future<chess::Move> m_bot_future;
    IPlayer *m_pending_bot_player =
        nullptr; // raw pointer on active player
    std::atomic<bool> m_cancel_bot{false};

    // pending promotion info
    bool m_waiting_promotion = false;
    chess::Square m_promotion_from = chess::NO_SQUARE;
    chess::Square m_promotion_to = chess::NO_SQUARE;

    // consistent transactions
    std::mutex m_mutex;

    // what to do when
    MoveCallback onMoveExecuted_;
    PromotionCallback onPromotionRequested_;
    EndCallback onGameEnd_;

    void applyMoveAndNotify(const chess::Move &mv, bool onClick);
    void startBotIfNeeded();
  };

}
