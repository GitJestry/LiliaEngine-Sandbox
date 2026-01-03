#include "lilia/controller/time_controller.hpp"

namespace lilia::controller {

TimeController::TimeController(int baseSeconds, int incSeconds)
    : m_white_time(static_cast<float>(baseSeconds)),
      m_black_time(static_cast<float>(baseSeconds)),
      m_increment(incSeconds),
      m_active(core::Color::White) {}

void TimeController::start(core::Color sideToMove) {
  m_active = sideToMove;
  m_running = true;
  m_started = false;
  m_flagged.reset();
}

void TimeController::onMove(core::Color mover) {
  if (!m_running || m_flagged)
    return;
  float &t = (mover == core::Color::White) ? m_white_time : m_black_time;
  t += static_cast<float>(m_increment);
  m_active = ~mover;
  if (!m_started)
    m_started = true;
}

void TimeController::update(float dt) {
  if (!m_running || m_flagged || !m_started)
    return;
  float &t = (m_active == core::Color::White) ? m_white_time : m_black_time;
  t -= dt;
  if (t <= 0.f) {
    t = 0.f;
    m_flagged = m_active;
    m_running = false;
  }
}

void TimeController::stop() {
  m_running = false;
  m_started = false;
}

float TimeController::getTime(core::Color color) const {
  return (color == core::Color::White) ? m_white_time : m_black_time;
}

std::optional<core::Color> TimeController::getFlagged() const { return m_flagged; }

std::optional<core::Color> TimeController::getActive() const {
  return m_running ? std::optional<core::Color>(m_active) : std::nullopt;
}

}  // namespace lilia::controller

