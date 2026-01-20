#pragma once

#include <SFML/Graphics.hpp>

#include <algorithm>
#include <functional>
#include <string>

#include "lilia/view/ui/style/style.hpp"
#include "lilia/view/ui/style/theme.hpp"

namespace lilia::view::ui
{
  class TogglePill final
  {
  public:
    using OnToggle = std::function<void(bool)>;

    void setTheme(const Theme *t) { m_theme = t; }

    void setFont(const sf::Font &f)
    {
      m_label.setFont(f);
      m_label.setCharacterSize(14);

      m_state.setFont(f);
      m_state.setCharacterSize(12);
    }

    void setBounds(sf::FloatRect r) { m_bounds = r; }
    const sf::FloatRect &bounds() const { return m_bounds; }

    void setLabel(std::string s, unsigned size = 14)
    {
      m_label.setString(std::move(s));
      m_label.setCharacterSize(size);
    }

    void setValue(bool v)
    {
      m_value = v;
      m_state.setString(m_value ? "ON" : "OFF");
    }

    bool value() const { return m_value; }

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

    void setOnToggle(OnToggle fn) { m_onToggle = std::move(fn); }

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
          m_prevDown = true;
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
          m_value = !m_value;
          m_state.setString(m_value ? "ON" : "OFF");
          if (m_onToggle)
            m_onToggle(m_value);
          return true;
        }
      }

      return false;
    }

    bool updateInput(sf::Vector2f mouse, bool mouseDown, sf::Vector2f offset = {})
    {
      updateHover(mouse, offset);

      if (mouseDown && !m_prevDown && m_hover && m_enabled)
        m_pressed = true;

      bool toggled = false;

      if (!mouseDown && m_prevDown)
      {
        const bool wasPressed = m_pressed;
        m_pressed = false;

        if (wasPressed && m_hover && m_enabled)
        {
          m_value = !m_value;
          m_state.setString(m_value ? "ON" : "OFF");
          toggled = true;
          if (m_onToggle)
            m_onToggle(m_value);
        }
      }

      m_prevDown = mouseDown;

      if (!m_enabled)
        m_pressed = false;

      return toggled;
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

      const sf::FloatRect r = offsetRect(m_bounds, offset);

      // Subtle pill surface (neutral, not accent-filled)
      sf::Color surface = m_theme->button;
      if (m_hover && m_enabled && !(m_pressed && m_hover))
        surface = lighten(surface, 6);
      if (m_pressed && m_hover && m_enabled)
        surface = darken(surface, 6);

      if (!m_enabled)
      {
        surface = darken(surface, 14);
        surface.a = static_cast<sf::Uint8>(std::max(0, int(surface.a) - 40));
      }

      drawSoftShadowRect(rt, r, sf::Color(0, 0, 0, 70), 1, 2.f);

      // Outline + fill
      drawPill(rt, r, mulA(darken(m_theme->inputBorder, 8)));
      sf::FloatRect inner = {r.left + 1.f, r.top + 1.f, r.width - 2.f, r.height - 2.f};
      drawPill(rt, inner, mulA(surface));

      const float pad = 14.f;

      // Switch on the right
      const float swW = 50.f;
      const float swH = std::max(18.f, inner.height - 16.f);
      sf::FloatRect sw{
          inner.left + inner.width - pad - swW,
          inner.top + (inner.height - swH) * 0.5f,
          swW,
          swH};

      // Track (accent only here)
      sf::Color trackOff = darken(m_theme->button, 10);
      sf::Color trackOn = m_theme->accent;

      sf::Color track = m_value ? trackOn : trackOff;
      if (!m_enabled)
        track = darken(track, 10);

      drawPill(rt, sw, mulA(track));

      // Knob
      const float knobPad = 2.f;
      const float knobR = std::max(4.f, (sw.height * 0.5f) - knobPad);
      const float knobD = 2.f * knobR;

      const float knobX = m_value ? (sw.left + sw.width - knobPad - knobD)
                                  : (sw.left + knobPad);
      const float knobY = sw.top + (sw.height - knobD) * 0.5f;

      sf::CircleShape knob(knobR);
      knob.setPosition(snap({knobX, knobY}));

      sf::Color knobFill = m_value ? m_theme->onAccent : m_theme->text;
      if (!m_enabled)
        knobFill = m_theme->subtle;
      knob.setFillColor(mulA(knobFill));
      rt.draw(knob);

      // Label left
      sf::Text label = m_label;
      sf::Color lc = m_theme->text;
      if (!m_enabled)
        lc = m_theme->subtle;
      label.setFillColor(mulA(lc));

      sf::FloatRect labelBox{
          inner.left + pad,
          inner.top,
          std::max(0.f, (sw.left - 10.f) - (inner.left + pad)),
          inner.height};
      leftCenterText(label, labelBox, 0.f);
      rt.draw(label);

      // ON/OFF text just left of switch
      sf::Text st = m_state;
      sf::Color sc = m_value ? m_theme->accent : m_theme->subtle;
      if (!m_enabled)
        sc = m_theme->subtle;
      st.setFillColor(mulA(sc));

      sf::FloatRect stateBox{sw.left - 46.f, inner.top, 40.f, inner.height};
      centerText(st, stateBox, -0.5f);
      rt.draw(st);
    }

  private:
    static void drawPill(sf::RenderTarget &rt, const sf::FloatRect &r, sf::Color fill)
    {
      if (r.width <= 0.f || r.height <= 0.f)
        return;

      const float h = r.height;
      const float radius = h * 0.5f;
      const float d = 2.f * radius;
      const float midW = std::max(0.f, r.width - d);

      sf::CircleShape c(radius);
      c.setFillColor(fill);

      c.setPosition(snap({r.left, r.top}));
      rt.draw(c);

      c.setPosition(snap({r.left + r.width - d, r.top}));
      rt.draw(c);

      sf::RectangleShape mid({midW, h});
      mid.setFillColor(fill);
      mid.setPosition(snap({r.left + radius, r.top}));
      rt.draw(mid);
    }

    const Theme *m_theme{nullptr};
    sf::FloatRect m_bounds{};

    sf::Text m_label{};
    sf::Text m_state{};

    OnToggle m_onToggle{};

    bool m_enabled{true};
    bool m_value{false};

    mutable bool m_hover{false};
    mutable bool m_pressed{false};
    bool m_prevDown{false};
  };
} // namespace lilia::view::ui
