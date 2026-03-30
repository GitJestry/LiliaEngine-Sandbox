#pragma once
#include <string>

#include "lilia/engine/config.hpp"
#include "lilia/chess/chess_game.hpp"

namespace lilia::protocol::uci
{

  class UCI
  {
  public:
    UCI() = default;
    int run();

  private:
    void showOptions();
    void setOption(const std::string &line);

    struct Options
    {
      engine::EngineConfig cfg{};
      bool ponder = false;
      int moveOverhead = 10;
      engine::EngineConfig toEngineConfig() const { return cfg; }
    } m_options;

    std::string m_name = "LiliaEngine";
    std::string m_version = "1.0";

    chess::ChessGame m_game;
  };

} // namespace lilia
