#pragma once

#include <SFML/Graphics.hpp>
#include <SFML/Window/Clipboard.hpp>

#include <algorithm>
#include <cmath>
#include <string>

#include "lilia/view/ui/interaction/focus.hpp"
#include "lilia/view/ui/style/style.hpp"
#include "lilia/view/ui/style/theme.hpp"

namespace lilia::view::ui
{

  class TextField : public Focusable
  {
  public:
    void setTheme(const Theme *t) { m_theme = t; }
    void setFont(const sf::Font &f) { m_text.setFont(f); }

    void setBounds(sf::FloatRect r) { m_bounds = r; }
    const sf::FloatRect &bounds() const { return m_bounds; }

    void setCharacterSize(unsigned s) { m_text.setCharacterSize(s); }
    void setPlaceholder(std::string s) { m_placeholder = std::move(s); }
    void setReadOnly(bool ro) { m_readOnly = ro; }

    void setText(std::string s)
    {
      // keep single-line semantics
      s.erase(std::remove(s.begin(), s.end(), '\n'), s.end());
      s.erase(std::remove(s.begin(), s.end(), '\r'), s.end());
      m_value = std::move(s);
    }
    const std::string &text() const { return m_value; }

    void setFocusManager(FocusManager *f) { m_focus = f; }
    bool focused() const { return m_focus && m_focus->focused() == this; }

    void onFocusGained() override { m_caretClock.restart(); }
    void onFocusLost() override {}

    void updateHover(sf::Vector2f mouse, sf::Vector2f offset = {})
    {
      m_hover = offsetRect(m_bounds, offset).contains(mouse);
    }

    bool handleEvent(const sf::Event &e, sf::Vector2f mouse, sf::Vector2f offset = {})
    {
      if (!m_theme)
        return false;

      const sf::FloatRect gb = offsetRect(m_bounds, offset);

      if (e.type == sf::Event::MouseButtonPressed && e.mouseButton.button == sf::Mouse::Left)
      {
        if (gb.contains(mouse))
        {
          if (m_focus)
            m_focus->request(this);
          return true;
        }
        if (m_focus && m_focus->focused() == this)
          m_focus->clear();
      }

      if (!focused() || m_readOnly)
        return false;

      if (e.type == sf::Event::KeyPressed)
      {
        if ((e.key.control || e.key.system) && e.key.code == sf::Keyboard::V)
        {
          std::string clip = sf::Clipboard::getString().toAnsiString();
          clip.erase(std::remove(clip.begin(), clip.end(), '\n'), clip.end());
          clip.erase(std::remove(clip.begin(), clip.end(), '\r'), clip.end());
          m_value += clip;
          return true;
        }
        if ((e.key.control || e.key.system) && e.key.code == sf::Keyboard::C)
        {
          sf::Clipboard::setString(m_value);
          return true;
        }
      }

      if (e.type == sf::Event::TextEntered)
      {
        if (e.text.unicode == 8)
        { // backspace
          if (!m_value.empty())
            m_value.pop_back();
          return true;
        }
        if (e.text.unicode >= 32 && e.text.unicode < 127)
        {
          m_value.push_back(char(e.text.unicode));
          return true;
        }
      }

      return false;
    }

    void draw(sf::RenderTarget &rt, sf::Vector2f offset = {}) const
    {
      if (!m_theme)
        return;

      const sf::FloatRect gb = offsetRect(m_bounds, offset);

      // subtle, consistent border logic: focus beats hover
      sf::Color border = m_theme->inputBorder;
      if (m_hover)
        border = m_theme->panelBorder;
      if (focused())
        border = m_theme->accent;

      sf::RectangleShape box({gb.width, gb.height});
      box.setPosition(snap({gb.left, gb.top}));
      box.setFillColor(m_theme->inputBg);
      box.setOutlineThickness(1.5f);
      box.setOutlineColor(border);
      rt.draw(box);

      // text (fit inside)
      const float pad = 8.f;
      const float availW = std::max(10.f, gb.width - pad * 2.f);

      sf::Text t = m_text;
      const bool empty = m_value.empty();
      std::string shown = empty ? m_placeholder : fitMiddleEllipsis(m_value, t, availW);

      t.setString(shown);
      t.setFillColor(empty ? m_theme->subtle : m_theme->text);
      leftCenterText(t, gb, pad);
      rt.draw(t);

      // caret (at end of visible string; good enough for single-line)
      if (focused() && !m_readOnly)
      {
        float blink = std::fmod(m_caretClock.getElapsedTime().asSeconds(), 1.f);
        if (blink < 0.5f)
        {
          sf::Text probe(shown, *t.getFont(), t.getCharacterSize());
          auto b = probe.getLocalBounds();
          float x = gb.left + pad + b.width + 1.f;
          float maxX = gb.left + gb.width - 6.f;
          x = std::min(x, maxX);

          sf::RectangleShape caret({2.f, gb.height * 0.62f});
          caret.setPosition(snap({x, gb.top + (gb.height - caret.getSize().y) * 0.5f}));
          caret.setFillColor(m_theme->text);
          rt.draw(caret);
        }
      }
    }

  private:
    static std::string fitMiddleEllipsis(const std::string &s, sf::Text &probe, float maxW)
    {
      probe.setString(s);
      if (probe.getLocalBounds().width <= maxW)
        return s;

      const std::string ell = "…";
      // binary search how many chars we can keep from both ends
      int lo = 1;
      int hi = (int)s.size() / 2;
      int best = 1;

      auto fits = [&](int keep) -> bool
      {
        if (keep <= 0)
          return true;
        std::string out = s.substr(0, keep) + ell + s.substr(s.size() - keep);
        probe.setString(out);
        return probe.getLocalBounds().width <= maxW;
      };

      while (lo <= hi)
      {
        int mid = (lo + hi) / 2;
        if (fits(mid))
        {
          best = mid;
          lo = mid + 1;
        }
        else
        {
          hi = mid - 1;
        }
      }

      std::string out = s.substr(0, best) + ell + s.substr(s.size() - best);
      probe.setString(out);

      // if even that doesn’t fit, fall back to tail-ellipsis
      if (probe.getLocalBounds().width > maxW)
      {
        int k = std::min<int>((int)s.size(), 32);
        out = ell + s.substr(s.size() - k);
      }
      return out;
    }

  private:
    const Theme *m_theme{nullptr};
    FocusManager *m_focus{nullptr};

    sf::FloatRect m_bounds{};
    sf::Text m_text{};

    std::string m_value{};
    std::string m_placeholder{"..."};

    mutable bool m_hover{false};
    bool m_readOnly{false};

    mutable sf::Clock m_caretClock{};
  };

} // namespace lilia::view::ui
