#pragma once

#include <SFML/Graphics.hpp>
#include <cstdint>
#include <string>

#include "lilia/view/ui/render/layout.hpp"
#include "../../theme.hpp"
#include "../../style.hpp"

namespace lilia::view::ui::game_setup
{
  static inline sf::Color withA(sf::Color c, std::uint8_t a)
  {
    c.a = a;
    return c;
  }

  static inline void draw_label(sf::RenderTarget &rt, const sf::Font &font, const ui::Theme &theme,
                                float x, float y, const std::string &txt, unsigned size = 13)
  {
    sf::Text t(txt, font, size);
    t.setFillColor(theme.subtle);
    t.setPosition(ui::snap({x, y}));
    rt.draw(t);
  }

  // kind: 0 neutral, 1 ok, 2 warn, 3 err
  static inline void draw_status_pill(sf::RenderTarget &rt, const sf::Font &font, const ui::Theme &theme,
                                      const sf::FloatRect &r, const std::string &txt, int kind)
  {
    sf::Color bg = withA(theme.panelBorder, 70);
    sf::Color fg = theme.subtle;

    if (kind == 1)
    {
      bg = withA(theme.accent, 70);
      fg = theme.text;
    }
    else if (kind == 2)
    {
      bg = withA(sf::Color(255, 170, 0), 70);
      fg = theme.text;
    }
    else if (kind == 3)
    {
      bg = withA(sf::Color(220, 80, 80), 80);
      fg = theme.text;
    }

    sf::RectangleShape box({r.width, r.height});
    box.setPosition(ui::snap({r.left, r.top}));
    box.setFillColor(bg);
    box.setOutlineThickness(1.f);
    box.setOutlineColor(withA(sf::Color::Black, 60));
    rt.draw(box);

    sf::Text t(txt, font, 12);
    t.setFillColor(fg);
    t.setPosition(ui::snap({r.left + 8.f, r.top + 2.f}));
    rt.draw(t);
  }

  static inline void draw_section_card(sf::RenderTarget &rt, const ui::Theme &theme, const sf::FloatRect &r)
  {
    sf::RectangleShape box({r.width, r.height});
    box.setPosition(ui::snap({r.left, r.top}));
    box.setFillColor(withA(theme.panelBorder, 30));
    box.setOutlineThickness(1.f);
    box.setOutlineColor(withA(sf::Color::Black, 40));
    rt.draw(box);
  }

} // namespace lilia::view::ui::game_setup
