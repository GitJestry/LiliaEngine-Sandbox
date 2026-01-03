#pragma once

#include <memory>
#include <optional>

#include "../../chess_types.hpp"
#include "lilia/view/ui/screens/game_view.hpp"
#include "../game_controller_types.hpp"
#include "../time_controller.hpp"

namespace lilia::model
{
  class ChessGame;
}

namespace lilia::controller
{

  class ClockSystem
  {
  public:
    ClockSystem(view::GameView &view, model::ChessGame &game);

    void reset(bool enabled, int baseSeconds, int incrementSeconds);
    void start(core::Color sideToMove);
    void stop();

    void update(float dt);

    void onMove(core::Color mover);

    bool enabled() const { return static_cast<bool>(m_time); }
    std::optional<core::Color> flagged() const;

    float time(core::Color c) const;
    std::optional<core::Color> active() const;

    TimeView snapshot(core::Color activeSideFallback) const;

  private:
    view::GameView &m_view;
    model::ChessGame &m_game;
    std::unique_ptr<TimeController> m_time;
  };

} // namespace lilia::controller
