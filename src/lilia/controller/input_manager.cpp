
#include "lilia/controller/input_manager.hpp"

#include <SFML/Window/Event.hpp>
#include <cmath>

namespace lilia::controller {

void InputManager::setOnClick(ClickCallback cb) {
  m_on_click = std::move(cb);
}

void InputManager::setOnDrag(DragCallback cb) {
  m_on_drag = std::move(cb);
}

void InputManager::setOnDrop(DropCallback cb) {
  m_on_drop = std::move(cb);
}

void InputManager::processEvent(const sf::Event& event) {
  switch (event.type) {
    case sf::Event::MouseButtonPressed:
      if (event.mouseButton.button == sf::Mouse::Left) {
        m_drag_start = core::MousePos(event.mouseButton.x, event.mouseButton.y);
        m_dragging = true;
      }
      break;

    case sf::Event::MouseMoved:
      if (m_dragging && m_drag_start) {
        core::MousePos currentPos(event.mouseMove.x, event.mouseMove.y);
        if (m_on_drag) m_on_drag(m_drag_start.value(), currentPos);
      }
      break;
    case sf::Event::MouseButtonReleased:
      if (event.mouseButton.button == sf::Mouse::Left && m_dragging && m_drag_start) {
        core::MousePos dropPos(event.mouseButton.x, event.mouseButton.y);
        if (isClick(m_drag_start.value(), dropPos)) {
          if (m_on_click) m_on_click(dropPos);
        } else {
          if (m_on_drop) m_on_drop(m_drag_start.value(), dropPos);
        }
        m_drag_start.reset();
        m_dragging = false;
      }
      break;

    default:
      break;
  }
}

void InputManager::cancelDrag() {
  m_dragging = false;
  m_drag_start.reset();
}

[[nodiscard]] bool InputManager::isClick(const core::MousePos& start, const core::MousePos& end,
                                         int threshold) const {
  int dx = end.x - start.x;
  int dy = end.y - start.y;
  return (dx * dx + dy * dy) <= (threshold * threshold);
}

}  // namespace lilia::controller
