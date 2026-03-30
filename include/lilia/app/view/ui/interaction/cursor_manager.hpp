#pragma once

#include <SFML/Window/Cursor.hpp>

namespace sf
{
  class RenderWindow;
}

namespace lilia::app::view::ui
{

  class CursorManager
  {
  public:
    explicit CursorManager(sf::RenderWindow &window);

    void setDefaultCursor();
    void setHandOpenCursor();
    void setHandClosedCursor();

  private:
    sf::RenderWindow &m_window;
    sf::Cursor m_cursor_default;
    sf::Cursor m_cursor_hand_open;
    sf::Cursor m_cursor_hand_closed;
  };

} // namespace lilia::view
