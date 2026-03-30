#pragma once

#include <SFML/Graphics.hpp>
#include <string>

#include "lilia/app/view/ui/render/layout.hpp"
#include "../../theme.hpp"

#include "position_builder.hpp"
#include "game_setup_validation.hpp"

namespace lilia::app::view::ui::game_setup
{
  class PageBuilder final
  {
  public:
    PageBuilder(const sf::Font &font, const Theme &theme);

    void onOpen();
    void layout(const sf::FloatRect &bounds);

    void update();
    void updateHover(MousePos mouse);
    bool handleEvent(const sf::Event &e, MousePos mouse);

    void draw(sf::RenderTarget &rt) const;

    std::string resolvedFen() const;

  private:
    const sf::Font &m_font;
    const Theme &m_theme;

    sf::FloatRect m_bounds{};
    PositionBuilder m_builder{};
  };

} // namespace lilia::view::game_setup
