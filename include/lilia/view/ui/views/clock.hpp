#pragma once

#include <SFML/Graphics/CircleShape.hpp>
#include <SFML/Graphics/Color.hpp>
#include <SFML/Graphics/RectangleShape.hpp>
#include <SFML/Graphics/RenderWindow.hpp>
#include <SFML/Graphics/Text.hpp>

#include "lilia/chess_types.hpp"

namespace lilia::view
{

  class Clock
  {
  public:
    Clock();
    ~Clock() = default;

    void setPlayerColor(core::Color color);
    void setPosition(const sf::Vector2f &pos);
    void setTime(float seconds);
    void setActive(bool active);
    void render(sf::RenderWindow &window);

    static constexpr float WIDTH = 144.f;
    static constexpr float HEIGHT = 40.f;

  private:
    void updateVisualState(); // derives colors from state + current palette

    sf::RectangleShape m_box;
    sf::RectangleShape m_overlay;
    sf::Text m_text;
    sf::Font m_font;

    bool m_low_time{false};
    bool m_active{false};
    core::Color m_playerColor{core::Color::Black};

    sf::CircleShape m_icon_circle;
    sf::RectangleShape m_icon_hand;
  };

} // namespace lilia::view
