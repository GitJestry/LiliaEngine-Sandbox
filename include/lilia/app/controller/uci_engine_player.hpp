#pragma once
#include <atomic>
#include <future>
#include <memory>
#include <string>

#include "lilia/app/controller/player.hpp"
#include "lilia/chess/chess_game.hpp"
#include "lilia/app/domain/analysis/config/start_config.hpp"
#include "lilia/app/engines/uci_engine_process.hpp"

namespace lilia::app::controller
{
  class UciEnginePlayer final : public IPlayer
  {
  public:
    explicit UciEnginePlayer(domain::analysis::config::BotConfig cfg);
    ~UciEnginePlayer() override;

    bool isHuman() const override { return false; }

    std::future<chess::Move> requestMove(chess::ChessGame &game,
                                         std::atomic_bool &cancel) override;

  private:
    chess::Move bestmoveToMove(const std::string &bestLine, chess::ChessGame &game) const;

  private:
    domain::analysis::config::BotConfig m_cfg;
    engines::UciEngineProcess m_proc;
    bool m_ok{false};
  };
}
