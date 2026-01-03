#pragma once

#include <functional>

#include "player.hpp"

namespace lilia::controller {

class BotPlayer : public IPlayer {
 public:
  explicit BotPlayer(int thinkMillis = 300, int depth = 8)
      : m_think_millis(thinkMillis), m_depth(depth) {}
  ~BotPlayer() override = default;

  bool isHuman() const override { return false; }
  std::future<model::Move> requestMove(model::ChessGame& gameState,
                                       std::atomic<bool>& cancelToken) override;

  using EvalCallback = std::function<void(int)>;
  static void setEvalCallback(EvalCallback cb);

 private:
  int m_depth;
  int m_think_millis;
  static EvalCallback s_eval_callback;
};

}  // namespace lilia::controller
