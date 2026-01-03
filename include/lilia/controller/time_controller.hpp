#pragma once

#include "lilia/chess_types.hpp"
#include <optional>

namespace lilia::controller {

class TimeController {
 public:
  TimeController(int baseSeconds = 0, int incSeconds = 0);

  void start(core::Color sideToMove);
  void onMove(core::Color mover);
  void update(float dt);

  void stop();

  [[nodiscard]] float getTime(core::Color color) const;
  [[nodiscard]] std::optional<core::Color> getFlagged() const;
  [[nodiscard]] std::optional<core::Color> getActive() const;

 private:
  float m_white_time;
  float m_black_time;
  int m_increment;
  core::Color m_active;
  bool m_running{false};
  bool m_started{false};
  std::optional<core::Color> m_flagged;
};

}  // namespace lilia::controller

