#pragma once

#include <SFML/Graphics.hpp>
#include <algorithm>
#include <functional>
#include <string>

#include "lilia/view/ui/style/style.hpp"
#include "lilia/view/ui/style/theme.hpp"

namespace lilia::view::ui
{

  class Button
  {
  public:
    using OnClick = std::function<void()>;

    void setTheme(const Theme *t) { m_theme = t; }
    void setFont(const sf::Font &f) { m_text.setFont(f); }

    void setText(std::string s, unsigned size)
    {
      m_text.setString(std::move(s));
      m_text.setCharacterSize(size);
    }

    void setBounds(sf::FloatRect r) { m_bounds = r; }
    const sf::FloatRect &bounds() const { return m_bounds; }

    void setOnClick(OnClick fn) { m_onClick = std::move(fn); }
    void setEnabled(bool e) { m_enabled = e; }
    void setAccent(bool a) { m_accent = a; }
    void setActive(bool a) { m_active = a; }

    void updateInput(sf::Vector2f mouse, bool mouseDown, sf::Vector2f offset = {})
    {
      updateHover(mouse, offset);
      if (!mouseDown)
        m_pressed = false;
    }

    void updateHover(sf::Vector2f mouse, sf::Vector2f offset = {})
    {
      if (!m_enabled)
      {
        m_hover = false;
        return;
      }
      m_hover = offsetRect(m_bounds, offset).contains(mouse);
    }

    bool handleEvent(const sf::Event &e, sf::Vector2f mouse, sf::Vector2f offset = {})
    {
      if (!m_enabled)
        return false;

      const sf::FloatRect gb = offsetRect(m_bounds, offset);

      if (e.type == sf::Event::MouseButtonPressed && e.mouseButton.button == sf::Mouse::Left)
      {
        if (gb.contains(mouse))
        {
          m_pressed = true;
          return true;
        }
      }

      if (e.type == sf::Event::MouseButtonReleased && e.mouseButton.button == sf::Mouse::Left)
      {
        bool wasPressed = m_pressed;
        m_pressed = false;
        if (wasPressed && gb.contains(mouse))
        {
          if (m_onClick)
            m_onClick();
          return true;
        }
      }

      return false;
    }

    void draw(sf::RenderTarget &rt, sf::Vector2f offset = {}, float alphaMul = 1.f) const
    {
      if (!m_theme)
        return;

      const float a = std::clamp(alphaMul, 0.f, 1.f);
      auto mulA = [&](sf::Color c)
      {
        c.a = static_cast<sf::Uint8>(static_cast<float>(c.a) * a);
        return c;
      };

      const sf::FloatRect gb = offsetRect(m_bounds, offset);

      // Base color selection
      sf::Color base = m_theme->button;
      if (m_accent)
        base = m_theme->accent;
      if (m_active)
        base = m_theme->buttonActive;

      // Disabled styling: keep geometry identical, but soften.
      if (!m_enabled)
      {
        base = darken(base, 18);
        base.a = static_cast<sf::Uint8>(std::max(0, int(base.a) - 40));
      }

      base = mulA(base);

      const bool pressedVisual = m_pressed && m_hover;
      drawBevelButton(rt, gb, base, (m_hover && m_enabled), pressedVisual);

      sf::Text t = m_text;

      // Correct theme-based "onColor" usage (fixes contrast on accent buttons).
      sf::Color tc = m_theme->text;
      if (m_accent)
        tc = m_theme->onAccent;
      if (!m_enabled)
        tc = m_theme->subtle;

      t.setFillColor(mulA(tc));
      centerText(t, gb);
      rt.draw(t);

      if (m_active)
        drawAccentInset(rt, gb, mulA(m_theme->accent));
    }

  private:
    const Theme *m_theme{nullptr};
    sf::FloatRect m_bounds{};
    sf::Text m_text{};
    OnClick m_onClick{};

    bool m_enabled{true};
    mutable bool m_hover{false};
    mutable bool m_pressed{false};
    bool m_accent{false};
    bool m_active{false};
  };

} // namespace lilia::view::ui
