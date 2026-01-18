#pragma once
#include <atomic>
#include <future>
#include <memory>
#include <string>

#include "lilia/controller/player.hpp" // IPlayer
#include "lilia/model/chess_game.hpp"
#include "lilia/model/analysis/config/start_config.hpp"
#include "lilia/engine/uci/uci_engine_process.hpp"

namespace lilia::controller
{
  class UciEnginePlayer final : public IPlayer
  {
  public:
    explicit UciEnginePlayer(lilia::config::BotConfig cfg);
    ~UciEnginePlayer() override;

    bool isHuman() const override { return false; }

    std::future<model::Move> requestMove(model::ChessGame &game,
                                         std::atomic_bool &cancel) override;

  private:
    model::Move bestmoveToMove(const std::string &bestLine, model::ChessGame &game) const;

  private:
    lilia::config::BotConfig m_cfg;
    UciEngineProcess m_proc;
    bool m_ok{false};
  };
} // namespace lilia::controller
