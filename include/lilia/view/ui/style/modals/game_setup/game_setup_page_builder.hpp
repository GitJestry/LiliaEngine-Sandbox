#pragma once

#include <SFML/Graphics.hpp>

#include <string>

#include "lilia/constants.hpp"
#include "lilia/view/ui/render/layout.hpp"

#include "../../theme.hpp"
#include "position_builder.hpp"

#include "game_setup_validation.hpp"

namespace lilia::view::ui::game_setup
{
  class PageBuilder final
  {
  public:
    PageBuilder(const sf::Font &font, const ui::Theme &theme)
        : m_font(font), m_theme(theme)
    {
      m_builder.setTheme(&m_theme);
      m_builder.setFont(&m_font);
      m_builder.onOpen();
    }

    void onOpen()
    {
      m_builder.onOpen();
    }

    void layout(const sf::FloatRect &bounds)
    {
      m_bounds = bounds;
      m_builder.setBounds(bounds);
    }

    void update() {}

    void updateHover(sf::Vector2f mouse)
    {
      m_builder.updateHover(mouse);
    }

    bool handleEvent(const sf::Event &e, sf::Vector2f mouse)
    {
      return m_builder.handleEvent(e, mouse);
    }

    void draw(sf::RenderTarget &rt) const
    {
      m_builder.draw(rt);
    }

    std::string resolvedFen() const
    {
      const std::string raw = m_builder.fenForUse();
      if (raw.empty())
        return {};

      const std::string norm = normalize_fen(raw);
      if (validate_fen_basic(norm).has_value())
        return {};

      return norm;
    }

  private:
    const sf::Font &m_font;
    const ui::Theme &m_theme;

    sf::FloatRect m_bounds{};
    PositionBuilder m_builder{};
  };

} // namespace lilia::view::ui::game_setup
