#pragma once

#include <memory>
#include <optional>

#include "lilia/app/view/ui/screens/game_view.hpp"
#include "lilia/app/controller/game_controller_types.hpp"
#include "lilia/app/controller/time_controller.hpp"

namespace lilia::app::controller
{

  class ClockSystem
  {
  public:
    ClockSystem(view::ui::GameView &view);

    void reset(bool enabled, int baseSeconds, int incrementSeconds);
    void start(chess::Color sideToMove);
    void stop();

    void update(float dt);

    void onMove(chess::Color mover);

    bool enabled() const { return static_cast<bool>(m_time); }
    std::optional<chess::Color> flagged() const;

    float time(chess::Color c) const;
    std::optional<chess::Color> active() const;

    domain::analysis::TimeView snapshot(chess::Color activeSideFallback) const;

  private:
    view::ui::GameView &m_view;
    std::unique_ptr<TimeController> m_time;
  };

} // namespace lilia::controller
