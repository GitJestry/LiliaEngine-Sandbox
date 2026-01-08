#pragma once

#include <SFML/Graphics.hpp>
#include <string>

#include "lilia/view/ui/render/layout.hpp"
#include "../../theme.hpp"

#include "position_builder.hpp"
#include "game_setup_validation.hpp"

namespace lilia::view::ui::game_setup
{
  class PageBuilder final
  {
  public:
    PageBuilder(const sf::Font &font, const ui::Theme &theme);

    void onOpen();
    void layout(const sf::FloatRect &bounds);

    void update();
    void updateHover(sf::Vector2f mouse);
    bool handleEvent(const sf::Event &e, sf::Vector2f mouse);

    void draw(sf::RenderTarget &rt) const;

    std::string resolvedFen() const;

  private:
    const sf::Font &m_font;
    const ui::Theme &m_theme;

    sf::FloatRect m_bounds{};
    PositionBuilder m_builder{};
  };

} // namespace lilia::view::ui::game_setup
