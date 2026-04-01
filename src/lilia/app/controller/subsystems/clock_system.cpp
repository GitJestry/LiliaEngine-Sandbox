#include "lilia/app/controller/subsystems/clock_system.hpp"

#include "lilia/chess/chess_game.hpp"

namespace lilia::app::controller
{

  ClockSystem::ClockSystem(view::ui::GameView &view)
      : m_view(view) {}

  void ClockSystem::reset(bool enabled, int baseSeconds, int incrementSeconds)
  {
    if (!enabled)
    {
      m_time.reset();
      m_view.setClocksVisible(false);
      m_view.setClockActive(std::nullopt);
      return;
    }

    m_time = std::make_unique<TimeController>(baseSeconds, incrementSeconds);
    m_view.setClocksVisible(true);
    m_view.updateClock(chess::Color::White, static_cast<float>(baseSeconds));
    m_view.updateClock(chess::Color::Black, static_cast<float>(baseSeconds));
  }

  void ClockSystem::start(chess::Color sideToMove)
  {
    if (!m_time)
      return;
    m_time->start(sideToMove);
    m_view.setClockActive(m_time->getActive());
  }

  void ClockSystem::stop()
  {
    if (!m_time)
      return;
    m_time->stop();
    m_view.setClockActive(std::nullopt);
  }

  void ClockSystem::update(float dt)
  {
    if (!m_time)
      return;
    m_time->update(dt);
  }

  void ClockSystem::onMove(chess::Color mover)
  {
    if (!m_time)
      return;
    m_time->onMove(mover);
  }

  std::optional<chess::Color> ClockSystem::flagged() const
  {
    if (!m_time)
      return std::nullopt;
    return m_time->getFlagged();
  }

  float ClockSystem::time(chess::Color c) const
  {
    if (!m_time)
      return 0.f;
    return m_time->getTime(c);
  }

  std::optional<chess::Color> ClockSystem::active() const
  {
    if (!m_time)
      return std::nullopt;
    return m_time->getActive();
  }

  domain::analysis::TimeView ClockSystem::snapshot(chess::Color activeSideFallback) const
  {
    domain::analysis::TimeView tv{};
    tv.white = time(chess::Color::White);
    tv.black = time(chess::Color::Black);
    tv.active = active().value_or(activeSideFallback);
    return tv;
  }

}
