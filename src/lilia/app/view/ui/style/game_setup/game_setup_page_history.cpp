#include "lilia/app/view/ui/style/modals/game_setup/game_setup_page_history.hpp"

namespace lilia::app::view::ui::game_setup
{
  PageHistory::PageHistory(const sf::Font &font, const Theme &theme)
      : m_font(font), m_theme(theme)
  {
    m_title.setFont(m_font);
    m_title.setCharacterSize(18);
    m_title.setFillColor(m_theme.text);
    m_title.setString("History");
  }

  void PageHistory::layout(const sf::FloatRect &bounds)
  {
    m_bounds = bounds;
    m_title.setPosition(snap({bounds.left, bounds.top}));
    m_card = {bounds.left, bounds.top + 44.f, bounds.width, 140.f};
  }

  void PageHistory::updateHover(MousePos) {}

  bool PageHistory::handleEvent(const sf::Event &, MousePos) { return false; }

  void PageHistory::draw(sf::RenderTarget &rt) const
  {
    rt.draw(m_title);

    draw_section_card(rt, m_theme, m_card);

    sf::Text p(
        "History is currently a placeholder",
        m_font, 14);
    p.setFillColor(m_theme.subtle);
    p.setPosition(snap({m_card.left + 12.f, m_card.top + 12.f}));
    rt.draw(p);
  }

} // namespace lilia::view::ui::game_setup
