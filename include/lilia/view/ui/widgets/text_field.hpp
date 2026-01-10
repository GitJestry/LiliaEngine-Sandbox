#pragma once

#include <SFML/Graphics.hpp>
#include <SFML/Window/Clipboard.hpp>

#include <algorithm>
#include <cctype>
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
    void setBounds(sf::FloatRect r)
    {
      m_bounds = r;
      clampCaret_();
    }
    const sf::FloatRect &bounds() const { return m_bounds; }

    void setCharacterSize(unsigned s) { m_text.setCharacterSize(s); }
    void setPlaceholder(std::string s) { m_placeholder = std::move(s); }
    void setReadOnly(bool ro) { m_readOnly = ro; }

    void setText(std::string s)
    {
      // single-line semantics
      s.erase(std::remove(s.begin(), s.end(), '\n'), s.end());
      s.erase(std::remove(s.begin(), s.end(), '\r'), s.end());
      m_value = std::move(s);
      m_caret = m_value.size();
      m_anchor = m_caret;
      m_scrollX = 0.f;
    }
    const std::string &text() const { return m_value; }

    void setFocusManager(FocusManager *f) { m_focus = f; }
    bool focused() const { return m_focus && m_focus->focused() == this; }

    void onFocusGained() override
    {
      m_caretClock.restart();
      m_mouseSelecting = false;
    }
    void onFocusLost() override { m_mouseSelecting = false; }

    void updateHover(sf::Vector2f mouse, sf::Vector2f offset = {})
    {
      m_hover = offsetRect(m_bounds, offset).contains(mouse);
    }

    bool handleEvent(const sf::Event &e, sf::Vector2f mouse, sf::Vector2f offset = {})
    {
      if (!m_theme)
        return false;

      const sf::FloatRect gb = offsetRect(m_bounds, offset);

      // -------- mouse focus + selection --------
      if (e.type == sf::Event::MouseButtonPressed && e.mouseButton.button == sf::Mouse::Left)
      {
        if (gb.contains(mouse))
        {
          if (m_focus)
            m_focus->request(this);

          // place caret + (optionally) extend selection
          const bool shift = sf::Keyboard::isKeyPressed(sf::Keyboard::LShift) ||
                             sf::Keyboard::isKeyPressed(sf::Keyboard::RShift);

          const size_t hit = caretFromMouse_(gb, mouse);
          if (!shift)
            m_anchor = hit;
          m_caret = hit;
          m_mouseSelecting = true;

          // keep caret visible
          updateScrollToCaret_(gb);
          m_caretClock.restart();
          return true;
        }

        if (m_focus && m_focus->focused() == this)
          m_focus->clear();

        m_mouseSelecting = false;
        return false;
      }

      if (e.type == sf::Event::MouseMoved && focused() && m_mouseSelecting)
      {
        if (!gb.contains(mouse))
        {
          // allow selecting beyond bounds (clamp)
          sf::Vector2f clamped = mouse;
          clamped.x = std::clamp(clamped.x, gb.left, gb.left + gb.width);
          const size_t hit = caretFromMouse_(gb, clamped);
          m_caret = hit;
        }
        else
        {
          m_caret = caretFromMouse_(gb, mouse);
        }

        updateScrollToCaret_(gb);
        m_caretClock.restart();
        return true;
      }

      if (e.type == sf::Event::MouseButtonReleased && e.mouseButton.button == sf::Mouse::Left)
      {
        if (m_mouseSelecting)
        {
          m_mouseSelecting = false;
          return true;
        }
      }

      // -------- keyboard editing/navigation --------
      if (!focused())
        return false;

      if (e.type == sf::Event::KeyPressed)
      {
        const bool ctrl = (e.key.control || e.key.system);
        const bool shift = e.key.shift;

        // select all
        if (ctrl && e.key.code == sf::Keyboard::A)
        {
          m_anchor = 0;
          m_caret = m_value.size();
          updateScrollToCaret_(gb);
          return true;
        }

        // copy / cut / paste
        if (ctrl && e.key.code == sf::Keyboard::C)
        {
          sf::Clipboard::setString(selectionText_());
          return true;
        }
        if (ctrl && e.key.code == sf::Keyboard::X)
        {
          if (!m_readOnly)
          {
            sf::Clipboard::setString(selectionText_());
            deleteSelection_();
            updateScrollToCaret_(gb);
          }
          return true;
        }
        if (ctrl && e.key.code == sf::Keyboard::V)
        {
          if (!m_readOnly)
          {
            std::string clip = sf::Clipboard::getString().toAnsiString();
            // single-line: strip newlines
            clip.erase(std::remove(clip.begin(), clip.end(), '\n'), clip.end());
            clip.erase(std::remove(clip.begin(), clip.end(), '\r'), clip.end());
            insertText_(clip);
            updateScrollToCaret_(gb);
          }
          return true;
        }

        // navigation
        if (e.key.code == sf::Keyboard::Left)
        {
          moveLeft_(ctrl, shift);
          updateScrollToCaret_(gb);
          return true;
        }
        if (e.key.code == sf::Keyboard::Right)
        {
          moveRight_(ctrl, shift);
          updateScrollToCaret_(gb);
          return true;
        }
        if (e.key.code == sf::Keyboard::Home)
        {
          setCaret_(0, shift);
          updateScrollToCaret_(gb);
          return true;
        }
        if (e.key.code == sf::Keyboard::End)
        {
          setCaret_(m_value.size(), shift);
          updateScrollToCaret_(gb);
          return true;
        }

        // deletion
        if (e.key.code == sf::Keyboard::BackSpace)
        {
          if (!m_readOnly)
          {
            if (hasSelection_())
              deleteSelection_();
            else
              deleteLeft_(ctrl);
            updateScrollToCaret_(gb);
          }
          return true;
        }
        if (e.key.code == sf::Keyboard::Delete)
        {
          if (!m_readOnly)
          {
            if (hasSelection_())
              deleteSelection_();
            else
              deleteRight_(ctrl);
            updateScrollToCaret_(gb);
          }
          return true;
        }
      }

      if (!m_readOnly && e.type == sf::Event::TextEntered)
      {
        // ignore control chars (including backspace/enter handled above)
        const sf::Uint32 u = e.text.unicode;
        if (u >= 32 && u != 127)
        {
          // keep current "ASCII-ish" behavior
          if (u < 128)
          {
            const char ch = static_cast<char>(u);
            if (ch != '\n' && ch != '\r')
            {
              insertText_(std::string(1, ch));
              updateScrollToCaret_(gb);
              return true;
            }
          }
        }
      }

      return false;
    }

    void draw(sf::RenderTarget &rt, sf::Vector2f offset = {}) const
    {
      if (!m_theme)
        return;

      const sf::FloatRect gb = offsetRect(m_bounds, offset);

      // border: focus beats hover
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

      const float pad = 8.f;

      // clipping region for text (so horizontal scrolling behaves properly)
      const sf::FloatRect clip = {gb.left + pad, gb.top + 2.f, gb.width - 2.f * pad, gb.height - 4.f};
      withClip_(rt, clip, [&]()
                {
        sf::Text t = m_text;

        const bool empty = m_value.empty();
        const std::string &s = empty ? m_placeholder : m_value;

        t.setString(s);
        t.setFillColor(empty ? m_theme->subtle : m_theme->text);

        auto b = t.getLocalBounds();
        const float y = gb.top + (gb.height - b.height) * 0.5f - b.top;

        const float x0 = gb.left + pad - m_scrollX;
        t.setPosition(snap({x0, y}));

        // selection highlight (only when not placeholder)
        if (!empty && focused() && hasSelection_())
        {
          const auto [a, bsel] = selectionRange_();

          sf::Text probe = t; // same string/pos/font
          const float xa = probe.findCharacterPos(static_cast<unsigned>(a)).x;
          const float xb = probe.findCharacterPos(static_cast<unsigned>(bsel)).x;

          const float h = gb.height * 0.62f;
          const float yy = gb.top + (gb.height - h) * 0.5f;

          sf::RectangleShape sel({std::max(0.f, xb - xa), h});
          sel.setPosition(snap({xa, yy}));
          sf::Color c = m_theme->accent;
          c.a = 70;
          sel.setFillColor(c);
          rt.draw(sel);
        }

        rt.draw(t);

        // caret
        if (!empty && focused() && !m_readOnly)
        {
          const float blink = std::fmod(m_caretClock.getElapsedTime().asSeconds(), 1.f);
          if (blink < 0.5f)
          {
            sf::Text probe = t;
            const float cx = probe.findCharacterPos(static_cast<unsigned>(m_caret)).x;

            sf::RectangleShape caret({2.f, gb.height * 0.62f});
            caret.setPosition(snap({cx, gb.top + (gb.height - caret.getSize().y) * 0.5f}));
            caret.setFillColor(m_theme->text);
            rt.draw(caret);
          }
        } });
    }

  private:
    // ---------- selection helpers ----------
    bool hasSelection_() const { return m_caret != m_anchor; }

    std::pair<size_t, size_t> selectionRange_() const
    {
      const size_t a = std::min(m_caret, m_anchor);
      const size_t b = std::max(m_caret, m_anchor);
      return {a, b};
    }

    std::string selectionText_() const
    {
      if (m_value.empty())
        return {};

      const auto [a, b] = selectionRange_();
      if (a == b)
        return m_value; // convenient legacy behavior: copy-all if no selection
      return m_value.substr(a, b - a);
    }

    void clampCaret_()
    {
      m_caret = std::min(m_caret, m_value.size());
      m_anchor = std::min(m_anchor, m_value.size());
    }

    void clearSelection_() { m_anchor = m_caret; }

    void deleteSelection_()
    {
      const auto [a, b] = selectionRange_();
      if (a == b)
        return;
      m_value.erase(a, b - a);
      m_caret = a;
      m_anchor = a;
    }

    void insertText_(const std::string &s)
    {
      deleteSelection_();
      m_value.insert(m_caret, s);
      m_caret += s.size();
      m_anchor = m_caret;
      m_caretClock.restart();
    }

    static bool isWordChar_(unsigned char c)
    {
      return std::isalnum(c) || c == '_';
    }

    void deleteLeft_(bool ctrl)
    {
      if (m_caret == 0)
        return;

      if (!ctrl)
      {
        m_value.erase(m_caret - 1, 1);
        --m_caret;
        m_anchor = m_caret;
        return;
      }

      // ctrl+backspace: delete previous "word"
      size_t i = m_caret;
      while (i > 0 && std::isspace(static_cast<unsigned char>(m_value[i - 1])))
        --i;
      while (i > 0 && isWordChar_(static_cast<unsigned char>(m_value[i - 1])))
        --i;

      m_value.erase(i, m_caret - i);
      m_caret = i;
      m_anchor = i;
    }

    void deleteRight_(bool ctrl)
    {
      if (m_caret >= m_value.size())
        return;

      if (!ctrl)
      {
        m_value.erase(m_caret, 1);
        m_anchor = m_caret;
        return;
      }

      // ctrl+delete: delete next "word"
      size_t i = m_caret;
      while (i < m_value.size() && std::isspace(static_cast<unsigned char>(m_value[i])))
        ++i;
      while (i < m_value.size() && isWordChar_(static_cast<unsigned char>(m_value[i])))
        ++i;

      m_value.erase(m_caret, i - m_caret);
      m_anchor = m_caret;
    }

    void setCaret_(size_t pos, bool shift)
    {
      pos = std::min(pos, m_value.size());
      m_caret = pos;
      if (!shift)
        m_anchor = m_caret;
      m_caretClock.restart();
    }

    void moveLeft_(bool ctrl, bool shift)
    {
      if (!ctrl)
      {
        if (m_caret > 0)
          setCaret_(m_caret - 1, shift);
        else
          setCaret_(0, shift);
        return;
      }

      size_t i = m_caret;
      while (i > 0 && std::isspace(static_cast<unsigned char>(m_value[i - 1])))
        --i;
      while (i > 0 && isWordChar_(static_cast<unsigned char>(m_value[i - 1])))
        --i;
      setCaret_(i, shift);
    }

    void moveRight_(bool ctrl, bool shift)
    {
      if (!ctrl)
      {
        setCaret_(std::min(m_caret + 1, m_value.size()), shift);
        return;
      }

      size_t i = m_caret;
      while (i < m_value.size() && std::isspace(static_cast<unsigned char>(m_value[i])))
        ++i;
      while (i < m_value.size() && isWordChar_(static_cast<unsigned char>(m_value[i])))
        ++i;
      setCaret_(i, shift);
    }

    // caret hit-test
    size_t caretFromMouse_(const sf::FloatRect &gb, sf::Vector2f mouse) const
    {
      if (m_value.empty())
        return 0;

      const float pad = 8.f;

      sf::Text t = m_text;
      t.setString(m_value);

      auto lb = t.getLocalBounds();
      const float y = gb.top + (gb.height - lb.height) * 0.5f - lb.top;
      const float x0 = gb.left + pad - m_scrollX;
      t.setPosition({x0, y});

      const float x = mouse.x;

      // clamp before first and after last
      const float xFirst = t.findCharacterPos(0).x;
      const float xLast = t.findCharacterPos(static_cast<unsigned>(m_value.size())).x;
      if (x <= xFirst)
        return 0;
      if (x >= xLast)
        return m_value.size();

      // binary search between glyph centers
      size_t lo = 0;
      size_t hi = m_value.size();
      while (lo + 1 < hi)
      {
        const size_t mid = (lo + hi) / 2;
        const float xm = t.findCharacterPos(static_cast<unsigned>(mid)).x;
        if (x < xm)
          hi = mid;
        else
          lo = mid;
      }

      // choose closer of lo / lo+1
      const float xLo = t.findCharacterPos(static_cast<unsigned>(lo)).x;
      const float xHi = t.findCharacterPos(static_cast<unsigned>(lo + 1)).x;
      return (x < (xLo + xHi) * 0.5f) ? lo : (lo + 1);
    }

    void updateScrollToCaret_(const sf::FloatRect &gb)
    {
      if (m_value.empty())
      {
        m_scrollX = 0.f;
        return;
      }

      const float pad = 8.f;
      const float viewW = std::max(10.f, gb.width - 2.f * pad);

      sf::Text t = m_text;
      t.setString(m_value);
      auto lb = t.getLocalBounds();
      const float y = gb.top + (gb.height - lb.height) * 0.5f - lb.top;

      const float x0 = gb.left + pad - m_scrollX;
      t.setPosition({x0, y});
      const float caretX = t.findCharacterPos(static_cast<unsigned>(m_caret)).x;

      const float leftEdge = gb.left + pad;
      const float rightEdge = gb.left + pad + viewW;

      if (caretX < leftEdge + 6.f)
        m_scrollX = std::max(0.f, m_scrollX - (leftEdge + 6.f - caretX));
      else if (caretX > rightEdge - 6.f)
        m_scrollX += (caretX - (rightEdge - 6.f));

      // clamp scroll so text doesn't drift infinitely
      // estimate full text width:
      const float fullW = t.findCharacterPos(static_cast<unsigned>(m_value.size())).x - t.findCharacterPos(0).x;
      const float maxScroll = std::max(0.f, fullW - viewW);
      m_scrollX = std::clamp(m_scrollX, 0.f, maxScroll);
    }

    template <class Fn>
    static void withClip_(sf::RenderTarget &rt, const sf::FloatRect &rect, Fn &&fn)
    {
      const sf::View old = rt.getView();

      const sf::Vector2u tsz = rt.getSize();
      const float vw = (tsz.x > 0) ? (rect.width / float(tsz.x)) : 1.f;
      const float vh = (tsz.y > 0) ? (rect.height / float(tsz.y)) : 1.f;
      const float vx = (tsz.x > 0) ? (rect.left / float(tsz.x)) : 0.f;
      const float vy = (tsz.y > 0) ? (rect.top / float(tsz.y)) : 0.f;

      sf::View clip(sf::FloatRect(rect.left, rect.top, rect.width, rect.height));
      clip.setViewport(sf::FloatRect(vx, vy, vw, vh));

      rt.setView(clip);
      fn();
      rt.setView(old);
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

    // editor state
    size_t m_caret{0};
    size_t m_anchor{0};
    bool m_mouseSelecting{false};
    mutable float m_scrollX{0.f};

    mutable sf::Clock m_caretClock{};
  };

} // namespace lilia::view::ui
