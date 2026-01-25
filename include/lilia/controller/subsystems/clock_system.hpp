#pragma once

#include <memory>
#include <optional>

#include "lilia/view/ui/screens/game_view.hpp"
#include "lilia/controller/game_controller_types.hpp"
#include "lilia/controller/time_controller.hpp"

namespace lilia::controller
{

  class ClockSystem
  {
  public:
    ClockSystem(view::GameView &view);

    void reset(bool enabled, int baseSeconds, int incrementSeconds);
    void start(core::Color sideToMove);
    void stop();

    void update(float dt);

    void onMove(core::Color mover);

    bool enabled() const { return static_cast<bool>(m_time); }
    std::optional<core::Color> flagged() const;

    float time(core::Color c) const;
    std::optional<core::Color> active() const;

    model::analysis::TimeView snapshot(core::Color activeSideFallback) const;

  private:
    view::GameView &m_view;
    std::unique_ptr<TimeController> m_time;
  };

} // namespace lilia::controller
