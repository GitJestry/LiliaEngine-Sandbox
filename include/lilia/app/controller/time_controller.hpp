#pragma once

#include "lilia/chess/chess_types.hpp"
#include <optional>

namespace lilia::app::controller
{

  class TimeController
  {
  public:
    TimeController(int baseSeconds = 0, int incSeconds = 0);

    void start(chess::Color sideToMove);
    void onMove(chess::Color mover);
    void update(float dt);

    void stop();

    [[nodiscard]] float getTime(chess::Color color) const;
    [[nodiscard]] std::optional<chess::Color> getFlagged() const;
    [[nodiscard]] std::optional<chess::Color> getActive() const;

  private:
    float m_white_time;
    float m_black_time;
    int m_increment;
    chess::Color m_active;
    bool m_running{false};
    bool m_started{false};
    std::optional<chess::Color> m_flagged;
  };

} // namespace lilia::controller
