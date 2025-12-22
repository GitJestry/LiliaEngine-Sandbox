#include "lilia/controller/subsystems/clock_system.hpp"

#include "lilia/model/chess_game.hpp"

namespace lilia::controller {

ClockSystem::ClockSystem(view::GameView& view, model::ChessGame& game)
    : m_view(view), m_game(game) {}

void ClockSystem::reset(bool enabled, int baseSeconds, int incrementSeconds) {
  if (!enabled) {
    m_time.reset();
    m_view.setClocksVisible(false);
    m_view.setClockActive(std::nullopt);
    return;
  }

  m_time = std::make_unique<TimeController>(baseSeconds, incrementSeconds);
  m_view.setClocksVisible(true);
  m_view.updateClock(core::Color::White, static_cast<float>(baseSeconds));
  m_view.updateClock(core::Color::Black, static_cast<float>(baseSeconds));
}

void ClockSystem::start(core::Color sideToMove) {
  if (!m_time) return;
  m_time->start(sideToMove);
  m_view.setClockActive(m_time->getActive());
}

void ClockSystem::stop() {
  if (!m_time) return;
  m_time->stop();
  m_view.setClockActive(std::nullopt);
}

void ClockSystem::update(float dt) {
  if (!m_time) return;
  m_time->update(dt);
}

void ClockSystem::onMove(core::Color mover) {
  if (!m_time) return;
  m_time->onMove(mover);
}

std::optional<core::Color> ClockSystem::flagged() const {
  if (!m_time) return std::nullopt;
  return m_time->getFlagged();
}

float ClockSystem::time(core::Color c) const {
  if (!m_time) return 0.f;
  return m_time->getTime(c);
}

std::optional<core::Color> ClockSystem::active() const {
  if (!m_time) return std::nullopt;
  return m_time->getActive();
}

TimeView ClockSystem::snapshot(core::Color activeSideFallback) const {
  TimeView tv{};
  tv.white = time(core::Color::White);
  tv.black = time(core::Color::Black);
  tv.active = active().value_or(activeSideFallback);
  return tv;
}

}  // namespace lilia::controller
