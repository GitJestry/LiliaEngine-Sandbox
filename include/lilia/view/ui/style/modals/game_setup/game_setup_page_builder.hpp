#pragma once

#include <SFML/Graphics.hpp>
#include <SFML/Window/Clipboard.hpp>

#include <string>

#include "lilia/constants.hpp"
#include "lilia/view/ui/render/layout.hpp"
#include "lilia/view/ui/widgets/button.hpp"
#include "lilia/view/ui/widgets/text_field.hpp"

#include "../../theme.hpp"
#include "position_builder.hpp"

#include "game_setup_validation.hpp"
#include "game_setup_draw.hpp"

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
      m_builder.resetToStart();

      m_builderFen.setTheme(&m_theme);
      m_builderFen.setFont(m_font);
      m_builderFen.setCharacterSize(14);
      m_builderFen.setReadOnly(true);
      m_builderFen.setPlaceholder("Builder FEN");

      setup_action(m_copyFen, "Copy", [this]
                   { sf::Clipboard::setString(m_builderFen.text()); });

      setup_action(m_reset, "Reset", [this]
                   {
        m_builder.resetToStart();
        m_builderFen.setText(m_builder.fen()); });

      // Note: true “paste into builder” requires a builder API to load from FEN.
      // We still provide a predictable behavior: Ctrl+V is handled in the modal orchestrator:
      // it auto-detects and routes paste into the PGN/FEN page.
      setup_action(m_hintBtn, "Hotkeys", [] {});

      m_builderFen.setText(m_builder.fen());
    }

    void layout(const sf::FloatRect &bounds)
    {
      m_bounds = bounds;

      // Modern layout: large board left, inspector panel right (no overlaps).
      const float gap = 14.f;

      sf::FloatRect left = bounds;
      sf::FloatRect right = bounds;

      // Prefer a square board ~520px but clamp to available.
      const float maxBoard = 520.f;
      const float boardSize = std::min(maxBoard, std::min(bounds.width * 0.62f, bounds.height - 110.f));

      left.width = boardSize;
      right.left = left.left + left.width + gap;
      right.width = std::max(200.f, bounds.width - left.width - gap);

      m_builder.setBounds({left.left, left.top, boardSize, boardSize});

      // Inspector card on the right
      m_inspector = {right.left, right.top, right.width, boardSize};

      // Bottom row under board for fen + buttons
      const float rowTop = left.top + boardSize + 14.f;
      const float fieldH = 36.f;

      m_builderFen.setBounds({bounds.left, rowTop, bounds.width - 92.f - 8.f - 92.f, fieldH});
      m_copyFen.setBounds({bounds.left + bounds.width - 92.f, rowTop, 92.f, fieldH});
      m_reset.setBounds({bounds.left + bounds.width - 92.f - 8.f - 92.f, rowTop, 92.f, fieldH});

      m_hintLinePos = ui::snap({bounds.left, rowTop - 18.f});
    }

    void update()
    {
      const std::string now = m_builder.fen();
      if (now != m_builderFen.text())
        m_builderFen.setText(now);
    }

    void updateHover(sf::Vector2f mouse)
    {
      m_builder.updateHover(mouse);
      m_builderFen.updateHover(mouse);
      m_copyFen.updateHover(mouse);
      m_reset.updateHover(mouse);
    }

    bool handleEvent(const sf::Event &e, sf::Vector2f mouse)
    {
      if (m_reset.handleEvent(e, mouse))
        return true;
      if (m_copyFen.handleEvent(e, mouse))
        return true;

      if (m_builder.handleEvent(e, mouse))
      {
        m_builderFen.setText(m_builder.fen());
        return true;
      }

      // read-only but focusable
      if (m_builderFen.handleEvent(e, mouse))
        return true;

      return false;
    }

    void draw(sf::RenderTarget &rt) const
    {
      // Board
      m_builder.draw(rt);

      // Hint line (stable, readable)
      sf::Text hint("Builder hotkeys: 1 Pawn  2 Bishop  3 Knight  4 Rook  5 Queen  6 King   |   Tab: color   |   Right click: clear",
                    m_font, 12);
      hint.setFillColor(m_theme.subtle);
      hint.setPosition(m_hintLinePos);
      rt.draw(hint);

      // Inspector card (right)
      draw_section_card(rt, m_theme, m_inspector);
      draw_label(rt, m_font, m_theme, m_inspector.left + 12.f, m_inspector.top + 10.f, "Builder");

      sf::Text p("Tip: Use Ctrl+V to paste a FEN/PGN at any time.\nIf it looks like a FEN, it will be routed to the FEN field.\nThen choose Source = FEN and press “Use Position”.",
                 m_font, 13);
      p.setFillColor(m_theme.subtle);
      p.setPosition(ui::snap({m_inspector.left + 12.f, m_inspector.top + 34.f}));
      rt.draw(p);

      // Controls under board
      m_builderFen.draw(rt);
      m_reset.draw(rt);
      m_copyFen.draw(rt);
    }

    std::string resolvedFen() const
    {
      const std::string bf = normalize_fen(m_builder.fen());
      if (!validate_fen_basic(bf).has_value())
        return bf;
      return core::START_FEN;
    }

  private:
    const sf::Font &m_font;
    const ui::Theme &m_theme;

    sf::FloatRect m_bounds{};
    sf::FloatRect m_inspector{};
    sf::Vector2f m_hintLinePos{};

    PositionBuilder m_builder{};
    ui::TextField m_builderFen{};
    ui::Button m_reset{};
    ui::Button m_copyFen{};
    ui::Button m_hintBtn{};

    void setup_action(ui::Button &b, const char *txt, std::function<void()> cb)
    {
      b.setTheme(&m_theme);
      b.setFont(m_font);
      b.setText(txt, 13);
      b.setOnClick(std::move(cb));
    }
  };

} // namespace lilia::view::ui::game_setup
