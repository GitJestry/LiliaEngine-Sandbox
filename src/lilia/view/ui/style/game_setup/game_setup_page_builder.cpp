#include "lilia/view/ui/style/modals/game_setup/game_setup_page_builder.hpp"

namespace lilia::view::ui::game_setup
{
  PageBuilder::PageBuilder(const sf::Font &font, const ui::Theme &theme)
      : m_font(font), m_theme(theme)
  {
    m_builder.setTheme(&m_theme);
    m_builder.setFont(&m_font);
    m_builder.onOpen();
  }

  void PageBuilder::onOpen()
  {
    m_builder.onOpen();
  }

  void PageBuilder::layout(const sf::FloatRect &bounds)
  {
    m_bounds = bounds;
    m_builder.setBounds(bounds);
  }

  void PageBuilder::update() {}

  void PageBuilder::updateHover(sf::Vector2f mouse)
  {
    m_builder.updateHover(mouse);
  }

  bool PageBuilder::handleEvent(const sf::Event &e, sf::Vector2f mouse)
  {
    return m_builder.handleEvent(e, mouse);
  }

  void PageBuilder::draw(sf::RenderTarget &rt) const
  {
    m_builder.draw(rt);
  }

  std::string PageBuilder::resolvedFen() const
  {
    const std::string raw = m_builder.fenForUse();
    if (raw.empty())
      return {};

    // Unified resolution: sanitize using shared rules (castling/EP consistency).
    return sanitize_fen_playable(raw);
  }

} // namespace lilia::view::ui::game_setup
