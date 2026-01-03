#pragma once

#include <SFML/Graphics.hpp>

#include <algorithm>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "../color_palette_manager.hpp"
#include "modal.hpp"
#include "../style.hpp"
#include "../theme.hpp"

namespace lilia::view::ui
{

  // A lightweight drop-down modal that lets the user switch registered palettes.
  // Design goals:
  // - no global background dim (feels like a menu)
  // - deterministic list during interaction (snapshot palette names on open)
  // - no dependency on the game/controller layer; emits callbacks only
  class PalettePickerModal final : public Modal
  {
  public:
    struct Params
    {
      const Theme *theme{};
      const sf::Font *font{};
      sf::FloatRect anchorButton{}; // screen-space bounds of the button that spawned the picker
      std::function<void(const std::string &name)> onPick{};
      std::function<void()> onClose{};
    };

    void open(sf::Vector2u ws, Params p)
    {
      m_open = true;
      m_dismissed = false;
      m_closing = false;
      m_anim = 0.f;

      m_ws = ws;
      m_theme = p.theme;
      m_font = p.font;
      m_anchor = p.anchorButton;
      m_onPick = std::move(p.onPick);
      m_onClose = std::move(p.onClose);

      // Snapshot names at open-time (stable list during interaction)
      m_names = ColorPaletteManager::get().paletteNames();

      m_selected = 0;
      const auto &active = ColorPaletteManager::get().activePalette();
      for (std::size_t i = 0; i < m_names.size(); ++i)
      {
        if (m_names[i] == active)
          m_selected = int(i);
      }

      layout(ws);
    }

    void close()
    {
      m_open = false;
      m_dismissed = true;
    }

    bool isOpen() const { return m_open; }

    // Modal
    void layout(sf::Vector2u ws) override
    {
      m_ws = ws;
      if (!m_open)
        return;

      const float itemH = 30.f;
      const float width = 200.f;
      const float totalH = itemH * float(m_names.size());

      const float below = float(ws.y) - (m_anchor.top + m_anchor.height);
      const bool dropUp = below < totalH + 16.f;

      const float x = m_anchor.left;
      const float y = dropUp ? (m_anchor.top - totalH - 6.f) : (m_anchor.top + m_anchor.height + 6.f);

      m_listRect = {snapf(x), snapf(y), width, totalH};
    }

    void update(float dt) override
    {
      if (!m_open)
        return;

      const float speed = 12.f;
      const float target = m_closing ? 0.f : 1.f;
      if (m_anim < target)
        m_anim = std::min(target, m_anim + speed * dt);
      if (m_anim > target)
        m_anim = std::max(target, m_anim - speed * dt);

      if (m_closing && m_anim <= 0.01f)
        close();
    }

    void updateInput(sf::Vector2f mouse, bool /*mouseDown*/) override
    {
      // We don't use Button widgets here; hover is computed per-row.
      m_mouse = mouse;
    }

    void drawOverlay(sf::RenderWindow & /*win*/) const override
    {
      // Dropdown feel: no global dim.
    }

    void drawPanel(sf::RenderWindow &win) const override
    {
      if (!m_open || !m_theme || !m_font)
        return;

      // Subtle shadow only (no global dim)
      drawPanelShadow(win, m_listRect);

      sf::RectangleShape panel({m_listRect.width, m_listRect.height});
      panel.setPosition(snap({m_listRect.left, m_listRect.top}));
      sf::Color p = m_theme->panel;
      p.a = sf::Uint8(float(p.a) * m_anim);
      panel.setFillColor(p);
      panel.setOutlineThickness(1.f);
      sf::Color ob = m_theme->panelBorder;
      ob.a = sf::Uint8(float(ob.a) * m_anim);
      panel.setOutlineColor(ob);
      win.draw(panel);

      const float itemH = 30.f;
      for (std::size_t i = 0; i < m_names.size(); ++i)
      {
        sf::FloatRect r{m_listRect.left, m_listRect.top + float(i) * itemH, m_listRect.width, itemH};
        const bool hov = r.contains(m_mouse);
        const bool sel = int(i) == m_selected;

        sf::Color base = sel ? m_theme->buttonActive : m_theme->button;
        base.a = sf::Uint8(float(base.a) * m_anim);
        drawBevelButton(win, r, base, hov, sel);

        sf::Text t(m_names[i], *m_font, 15);
        sf::Color tc = m_theme->text;
        tc.a = sf::Uint8(float(tc.a) * m_anim);
        t.setFillColor(tc);
        leftCenterText(t, r, 10.f);
        win.draw(t);

        if (sel)
        {
          sf::Color ac = m_theme->accent;
          ac.a = sf::Uint8(float(ac.a) * m_anim);
          drawAccentInset(win, r, ac);
        }
      }
    }

    bool handleEvent(const sf::Event &e, sf::Vector2f mouse) override
    {
      if (!m_open)
        return false;

      m_mouse = mouse;

      if (e.type == sf::Event::KeyPressed && e.key.code == sf::Keyboard::Escape)
      {
        if (m_onClose)
          m_onClose();
        m_closing = true;
        return true;
      }

      if (e.type == sf::Event::MouseButtonPressed && e.mouseButton.button == sf::Mouse::Left)
      {
        if (!m_listRect.contains(mouse))
        {
          if (m_onClose)
            m_onClose();
          m_closing = true;
          return true;
        }

        const float itemH = 30.f;
        const int idx = int((mouse.y - m_listRect.top) / itemH);
        if (idx >= 0 && idx < int(m_names.size()))
        {
          m_selected = idx;
          m_picked = idx;

          // Apply immediately.
          ColorPaletteManager::get().setPalette(m_names[std::size_t(idx)]);

          if (m_onPick)
            m_onPick(m_names[std::size_t(idx)]);

          m_closing = true;
          return true;
        }
      }

      return false;
    }

    bool dismissed() const override { return m_dismissed; }

  private:
    bool m_open{false};
    bool m_dismissed{false};
    bool m_closing{false};
    float m_anim{0.f};

    sf::Vector2u m_ws{};
    sf::FloatRect m_anchor{};
    sf::FloatRect m_listRect{};
    sf::Vector2f m_mouse{};

    const Theme *m_theme{nullptr};
    const sf::Font *m_font{nullptr};

    std::vector<std::string> m_names;
    int m_selected{0};
    std::optional<int> m_picked{};

    std::function<void(const std::string &)> m_onPick{};
    std::function<void()> m_onClose{};
  };

} // namespace lilia::view::ui

// Backwards-compatibility alias (older call sites may use lilia::view::PalettePickerModal).
namespace lilia::view
{
  using PalettePickerModal = ui::PalettePickerModal;
}
