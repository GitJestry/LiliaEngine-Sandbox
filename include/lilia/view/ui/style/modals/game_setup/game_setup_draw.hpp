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
  // Visual policy (requested):
  // - ok: green
  // - err: red
  // - neutral: subdued
  static inline void draw_status_pill(sf::RenderTarget &rt, const sf::Font &font, const ui::Theme &theme,
                                      const sf::FloatRect &r, const std::string &txt, int kind)
  {
    // Explicit "valid/invalid" colors (matches your palette defaults)
    const sf::Color kOk = sf::Color(122, 205, 164);
    const sf::Color kErr = sf::Color(220, 70, 70);

    sf::Color fill = withA(theme.panelBorder, 45);
    sf::Color outline = withA(theme.panelBorder, 120);
    sf::Color text = theme.text;

    if (kind == 1)
      fill = withA(kOk, 110);
    else if (kind == 3)
      fill = withA(kErr, 120);

    sf::RectangleShape box({r.width, r.height});
    box.setPosition(ui::snap({r.left, r.top}));
    box.setFillColor(fill);
    box.setOutlineThickness(1.f);
    box.setOutlineColor(outline);
    rt.draw(box);

    sf::Text t(txt, font, 12);
    t.setFillColor(text);

    // vertically centered text
    const auto b = t.getLocalBounds();
    t.setPosition(ui::snap({r.left + 10.f, r.top + (r.height - b.height) * 0.5f - b.top - 1.f}));
    rt.draw(t);
  }

  static inline void draw_section_card(sf::RenderTarget &rt, const ui::Theme &theme, const sf::FloatRect &r)
  {
    // Cleaner, less “double-outline” look: soft fill + single border.
    sf::RectangleShape box({r.width, r.height});
    box.setPosition(ui::snap({r.left, r.top}));
    box.setFillColor(withA(theme.panelBorder, 22));
    box.setOutlineThickness(1.f);
    box.setOutlineColor(withA(theme.panelBorder, 110));
    rt.draw(box);
  }

} // namespace lilia::view::ui::game_setup
