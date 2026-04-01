#include "lilia/app/controller/time_controller.hpp"

namespace lilia::app::controller
{

  TimeController::TimeController(int baseSeconds, int incSeconds)
      : m_white_time(static_cast<float>(baseSeconds)),
        m_black_time(static_cast<float>(baseSeconds)),
        m_increment(incSeconds),
        m_active(chess::Color::White) {}

  void TimeController::start(chess::Color sideToMove)
  {
    m_active = sideToMove;
    m_running = true;
    m_started = false;
    m_flagged.reset();
  }

  void TimeController::onMove(chess::Color mover)
  {
    if (!m_running || m_flagged)
      return;
    float &t = (mover == chess::Color::White) ? m_white_time : m_black_time;
    t += static_cast<float>(m_increment);
    m_active = ~mover;
    if (!m_started)
      m_started = true;
  }

  void TimeController::update(float dt)
  {
    if (!m_running || m_flagged || !m_started)
      return;
    float &t = (m_active == chess::Color::White) ? m_white_time : m_black_time;
    t -= dt;
    if (t <= 0.f)
    {
      t = 0.f;
      m_flagged = m_active;
      m_running = false;
    }
  }

  void TimeController::stop()
  {
    m_running = false;
    m_started = false;
  }

  float TimeController::getTime(chess::Color color) const
  {
    return (color == chess::Color::White) ? m_white_time : m_black_time;
  }

  std::optional<chess::Color> TimeController::getFlagged() const { return m_flagged; }

  std::optional<chess::Color> TimeController::getActive() const
  {
    return m_running ? std::optional<chess::Color>(m_active) : std::nullopt;
  }

}
