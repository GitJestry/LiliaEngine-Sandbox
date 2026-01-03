#include "lilia/controller/bot_player.hpp"

#include <atomic>
#include <future>
#include <iostream>

#include "lilia/engine/bot_engine.hpp"
#include "lilia/model/chess_game.hpp"
#include "lilia/model/move_generator.hpp" 
#include "lilia/uci/uci_helper.hpp"
#include "lilia/bot/bot_info.hpp"

namespace lilia::controller {

BotPlayer::EvalCallback BotPlayer::s_eval_callback = nullptr;
void BotPlayer::setEvalCallback(EvalCallback cb) {
  s_eval_callback = std::move(cb);
}
std::future<model::Move> BotPlayer::requestMove(model::ChessGame& gameState,
                                                std::atomic<bool>& cancelToken) {
  int requestedDepth = m_depth;
  int thinkMs = m_think_millis;

  return std::async(std::launch::async,
                    [this, &gameState, &cancelToken, requestedDepth, thinkMs]() -> model::Move {
                      lilia::engine::BotEngine engine;

                      lilia::engine::SearchResult res =
                          engine.findBestMove(gameState, requestedDepth, thinkMs, &cancelToken);

                      if (BotPlayer::s_eval_callback) {
                        int eval = res.stats.bestScore;
                        if (gameState.getGameState().sideToMove == core::Color::Black) eval = -eval;
                        BotPlayer::s_eval_callback(eval);
                      }
                      if (cancelToken.load()) {
                        return model::Move{};
                      }

                      if (!res.bestMove.has_value()) {
                        model::MoveGenerator mg;
                        thread_local std::vector<model::Move> moveBuf;
                        auto pos = gameState.getPositionRefForBot();
                        mg.generatePseudoLegalMoves(pos.getBoard(), pos.getState(), moveBuf);
                        for (auto& m : moveBuf) {
                          if (pos.doMove(m)) {
                            pos.undoMove();
                            std::cout << "[BotPlayer] fallback move chosen: " << move_to_uci(m)
                                      << "\n";
                            return m;
                          }
                        }

                        std::cout << "[BotPlayer] returning invalid move (no legal moves)\n";
                        return model::Move{};
                      }

                      return res.bestMove.value_or(model::Move{});
                    });
}
}  // namespace lilia::controller
