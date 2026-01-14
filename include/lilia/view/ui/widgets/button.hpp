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

    void setEnabled(bool e)
    {
      m_enabled = e;
      if (!m_enabled)
      {
        m_hover = false;
        m_pressed = false;
        m_prevDown = false;
      }
    }

    void setAccent(bool a) { m_accent = a; }
    void setActive(bool a) { m_active = a; }
    void setHoverOutline(bool v) { m_hoverOutline = v; }

    [[nodiscard]] bool hovered() const noexcept { return m_hover; }
    [[nodiscard]] bool pressed() const noexcept { return m_pressed; }

    // Polling-style update (works without SFML events)
    // Returns true if it consumed a click (i.e., invoked onClick).
    bool updateInput(sf::Vector2f mouse, bool mouseDown, sf::Vector2f offset = {})
    {
      updateHover(mouse, offset);

      // Press begins on fresh down while hovering
      if (mouseDown && !m_prevDown && m_hover && m_enabled)
        m_pressed = true;

      bool clicked = false;

      // Release ends click (only if press started on this button and release is also on it)
      if (!mouseDown && m_prevDown)
      {
        const bool wasPressed = m_pressed;
        m_pressed = false;

        if (wasPressed && m_hover && m_enabled)
        {
          clicked = true;
          if (m_onClick)
            m_onClick();
        }
      }

      m_prevDown = mouseDown;

      // If the pointer leaves while holding, keep pressed state (visual “capture”),
      // but the click will only fire if it is released while hovering.
      if (!m_enabled)
        m_pressed = false;

      return clicked;
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

    // Event-style input (works with SFML events)
    // Returns true if it consumed the event.
    bool handleEvent(const sf::Event &e, sf::Vector2f mouse, sf::Vector2f offset = {})
    {
      if (!m_enabled)
        return false;

      const sf::FloatRect gb = offsetRect(m_bounds, offset);

      if (e.type == sf::Event::MouseMoved)
      {
        m_hover = gb.contains(mouse);
        return false;
      }

      if (e.type == sf::Event::MouseButtonPressed && e.mouseButton.button == sf::Mouse::Left)
      {
        if (gb.contains(mouse))
        {
          m_pressed = true;
          m_prevDown = true; // keep polling state consistent if both paths are used
          return true;
        }
      }

      if (e.type == sf::Event::MouseButtonReleased && e.mouseButton.button == sf::Mouse::Left)
      {
        const bool wasPressed = m_pressed;
        m_pressed = false;
        m_prevDown = false;

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

      // Disabled styling
      if (!m_enabled)
      {
        base = darken(base, 18);
        base.a = static_cast<sf::Uint8>(std::max(0, int(base.a) - 40));
      }

      base = mulA(base);

      // Visual pressed only when both pressed+hover (classic button behavior)
      const bool pressedVisual = m_pressed && m_hover && m_enabled;
      drawBevelButton(rt, gb, base, (m_hover && m_enabled), pressedVisual);

      // Text
      sf::Text t = m_text;

      sf::Color tc = m_theme->text;
      if (m_accent)
        tc = m_theme->onAccent;
      if (!m_enabled)
        tc = m_theme->subtle;

      t.setFillColor(mulA(tc));
      centerText(t, gb);
      rt.draw(t);

      // Active ring
      if (m_active)
        drawAccentInset(rt, gb, mulA(m_theme->accent));

      // Optional hover outline (for icon buttons / slots)
      if (m_hoverOutline && m_hover && m_enabled && !m_active)
        drawAccentInset(rt, gb, mulA(m_theme->accent));
    }

  private:
    const Theme *m_theme{nullptr};
    sf::FloatRect m_bounds{};
    sf::Text m_text{};
    OnClick m_onClick{};

    bool m_enabled{true};

    // input state
    mutable bool m_hover{false};
    mutable bool m_pressed{false};
    bool m_prevDown{false};

    // visuals
    bool m_accent{false};
    bool m_active{false};
    bool m_hoverOutline{false};
  };

} // namespace lilia::view::ui
