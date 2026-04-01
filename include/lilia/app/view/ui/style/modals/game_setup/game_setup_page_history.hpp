#pragma once

#include <SFML/Graphics.hpp>

#include <string>

#include "lilia/app/view/ui/render/layout.hpp"
#include "../../theme.hpp"
#include "game_setup_draw.hpp"
#include "lilia/app/view/mousepos.hpp"

namespace lilia::app::view::ui::game_setup
{
  class PageHistory final
  {
  public:
    PageHistory(const sf::Font &font, const Theme &theme);

    void layout(const sf::FloatRect &bounds);

    void updateHover(MousePos mouse);
    bool handleEvent(const sf::Event &e, MousePos mouse);

    void draw(sf::RenderTarget &rt) const;

  private:
    const sf::Font &m_font;
    const Theme &m_theme;

    sf::FloatRect m_bounds{};
    sf::FloatRect m_card{};

    sf::Text m_title{};
  };

}
