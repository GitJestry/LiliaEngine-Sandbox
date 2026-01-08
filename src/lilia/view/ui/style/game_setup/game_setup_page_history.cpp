#include "lilia/view/ui/style/modals/game_setup/game_setup_page_history.hpp"

namespace lilia::view::ui::game_setup
{
  PageHistory::PageHistory(const sf::Font &font, const ui::Theme &theme)
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
    m_title.setPosition(ui::snap({bounds.left, bounds.top}));
    m_card = {bounds.left, bounds.top + 44.f, bounds.width, 140.f};
  }

  void PageHistory::updateHover(sf::Vector2f) {}

  bool PageHistory::handleEvent(const sf::Event &, sf::Vector2f) { return false; }

  void PageHistory::draw(sf::RenderTarget &rt) const
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

} // namespace lilia::view::ui::game_setup
