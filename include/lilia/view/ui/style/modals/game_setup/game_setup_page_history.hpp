#pragma once

#include <SFML/Graphics.hpp>
#include <string>

#include "lilia/view/ui/render/layout.hpp"
#include "../../theme.hpp"
#include "game_setup_draw.hpp"

namespace lilia::view::ui::game_setup
{
  class PageHistory final
  {
  public:
    PageHistory(const sf::Font &font, const ui::Theme &theme) : m_font(font), m_theme(theme)
    {
      m_title.setFont(m_font);
      m_title.setCharacterSize(18);
      m_title.setFillColor(m_theme.text);
      m_title.setString("History");
    }

    void layout(const sf::FloatRect &bounds)
    {
      m_bounds = bounds;
      m_title.setPosition(ui::snap({bounds.left, bounds.top}));
      m_card = {bounds.left, bounds.top + 44.f, bounds.width, 140.f};
    }

    void updateHover(sf::Vector2f) {}
    bool handleEvent(const sf::Event &, sf::Vector2f) { return false; }

    void draw(sf::RenderTarget &rt) const
    {
      rt.draw(m_title);

      draw_section_card(rt, m_theme, m_card);

      sf::Text p(
          "History is currently a placeholder.\nRecommended: show saved positions + imported PGNs here with:\n- preview board\n- source indicator (FEN/PGN/Builder)\n- last used timestamp\n- one-click “Use Position”",
          m_font, 14);
      p.setFillColor(m_theme.subtle);
      p.setPosition(ui::snap({m_card.left + 12.f, m_card.top + 12.f}));
      rt.draw(p);
    }

  private:
    const sf::Font &m_font;
    const ui::Theme &m_theme;

    sf::FloatRect m_bounds{};
    sf::FloatRect m_card{};

    sf::Text m_title{};
  };

} // namespace lilia::view::ui::game_setup
