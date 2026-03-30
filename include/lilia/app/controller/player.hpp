#pragma once

#include <atomic>
#include <future>

namespace lilia::chess
{
  class ChessGame;
  struct Move;
} // namespace lilia::model

namespace lilia::app::controller
{

  struct IPlayer
  {
    virtual ~IPlayer() = default;

    virtual std::future<chess::Move> requestMove(chess::ChessGame &gameState,
                                                 std::atomic<bool> &cancelToken) = 0;
    virtual bool isHuman() const = 0;
  };

} // namespace lilia::controller
