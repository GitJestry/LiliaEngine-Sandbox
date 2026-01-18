#pragma once

#include <atomic>
#include <future>

namespace lilia::model
{
  class ChessGame;
  struct Move;
} // namespace lilia::model

namespace lilia::controller
{

  struct IPlayer
  {
    virtual ~IPlayer() = default;

    virtual std::future<model::Move> requestMove(model::ChessGame &gameState,
                                                 std::atomic<bool> &cancelToken) = 0;
    virtual bool isHuman() const = 0;
  };

} // namespace lilia::controller
