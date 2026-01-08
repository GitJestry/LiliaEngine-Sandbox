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
    PageHistory(const sf::Font &font, const ui::Theme &theme);

    void layout(const sf::FloatRect &bounds);

    void updateHover(sf::Vector2f mouse);
    bool handleEvent(const sf::Event &e, sf::Vector2f mouse);

    void draw(sf::RenderTarget &rt) const;

  private:
    const sf::Font &m_font;
    const ui::Theme &m_theme;

    sf::FloatRect m_bounds{};
    sf::FloatRect m_card{};

    sf::Text m_title{};
  };

} // namespace lilia::view::ui::game_setup
