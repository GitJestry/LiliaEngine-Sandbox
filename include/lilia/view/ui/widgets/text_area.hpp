#pragma once

#include <SFML/Graphics.hpp>
#include <SFML/Window/Clipboard.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>
#include <vector>

#include "lilia/view/ui/interaction/focus.hpp"
#include "lilia/view/ui/style/style.hpp"
#include "lilia/view/ui/style/theme.hpp"

namespace lilia::view::ui
{
  class TextArea : public Focusable
  {
  public:
    void setTheme(const Theme *t)
    {
      m_theme = t;
      m_layoutDirty = true;
    }
    void setFont(const sf::Font &f)
    {
      m_text.setFont(f);
      m_layoutDirty = true;
    }

    void setBounds(sf::FloatRect r)
    {
      m_bounds = r;
      m_layoutDirty = true;
    }
    const sf::FloatRect &bounds() const { return m_bounds; }

    void setCharacterSize(unsigned s)
    {
      m_text.setCharacterSize(s);
      m_layoutDirty = true;
    }
    void setPlaceholder(std::string s) { m_placeholder = std::move(s); }
    void setReadOnly(bool ro) { m_readOnly = ro; }

    void setText(std::string s)
    {
      normalizeNewlines_(s);
      m_value = std::move(s);
      m_caret = m_value.size();
      m_anchor = m_caret;
      m_layoutDirty = true;
      m_scrollPx = 0.f;
      scrollToCaret_(); // end
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

      // focus + caret/selection
      if (e.type == sf::Event::MouseButtonPressed && e.mouseButton.button == sf::Mouse::Left)
      {
        if (gb.contains(mouse))
        {
          if (m_focus)
            m_focus->request(this);

          ensureLayout_();

          const bool shift = sf::Keyboard::isKeyPressed(sf::Keyboard::LShift) ||
                             sf::Keyboard::isKeyPressed(sf::Keyboard::RShift);

          const size_t hit = caretFromMouse_(gb, mouse);
          if (!shift)
            m_anchor = hit;
          m_caret = hit;

          m_mouseSelecting = true;
          m_caretDesiredX = caretPixelX_(gb);
          ensureCaretVisible_(gb);
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
        ensureLayout_();

        sf::Vector2f clamped = mouse;
        clamped.x = std::clamp(clamped.x, gb.left, gb.left + gb.width);
        clamped.y = std::clamp(clamped.y, gb.top, gb.top + gb.height);

        m_caret = caretFromMouse_(gb, clamped);
        ensureCaretVisible_(gb);
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

      // wheel scrolling
      if (e.type == sf::Event::MouseWheelScrolled)
      {
        if (gb.contains(mouse))
        {
          ensureLayout_();
          const float step = lineHeight_() * 3.f;
          m_scrollPx -= e.mouseWheelScroll.delta * step;
          clampScroll_();
          return true;
        }
      }

      if (!focused())
        return false;

      // keyboard
      if (e.type == sf::Event::KeyPressed)
      {
        const bool ctrl = (e.key.control || e.key.system);
        const bool shift = e.key.shift;

        // select all
        if (ctrl && e.key.code == sf::Keyboard::A)
        {
          m_anchor = 0;
          m_caret = m_value.size();
          m_caretDesiredX = caretPixelX_(gb);
          ensureCaretVisible_(gb);
          return true;
        }

        // copy/cut/paste
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
            m_layoutDirty = true;
            ensureLayout_();
            m_caretDesiredX = caretPixelX_(gb);
            ensureCaretVisible_(gb);
          }
          return true;
        }
        if (ctrl && e.key.code == sf::Keyboard::V)
        {
          if (!m_readOnly)
          {
            std::string clip = sf::Clipboard::getString().toAnsiString();
            normalizeNewlines_(clip);
            insertText_(clip);
            m_layoutDirty = true;
            ensureLayout_();
            m_caretDesiredX = caretPixelX_(gb);
            ensureCaretVisible_(gb);
          }
          return true;
        }

        // navigation
        if (e.key.code == sf::Keyboard::Left)
        {
          moveLeft_(ctrl, shift);
          m_caretDesiredX = caretPixelX_(gb);
          ensureCaretVisible_(gb);
          return true;
        }
        if (e.key.code == sf::Keyboard::Right)
        {
          moveRight_(ctrl, shift);
          m_caretDesiredX = caretPixelX_(gb);
          ensureCaretVisible_(gb);
          return true;
        }
        if (e.key.code == sf::Keyboard::Up)
        {
          moveUp_(shift, gb);
          ensureCaretVisible_(gb);
          return true;
        }
        if (e.key.code == sf::Keyboard::Down)
        {
          moveDown_(shift, gb);
          ensureCaretVisible_(gb);
          return true;
        }
        if (e.key.code == sf::Keyboard::Home)
        {
          if (ctrl)
            setCaret_(0, shift);
          else
            moveLineHome_(shift);
          m_caretDesiredX = caretPixelX_(gb);
          ensureCaretVisible_(gb);
          return true;
        }
        if (e.key.code == sf::Keyboard::End)
        {
          if (ctrl)
            setCaret_(m_value.size(), shift);
          else
            moveLineEnd_(shift);
          m_caretDesiredX = caretPixelX_(gb);
          ensureCaretVisible_(gb);
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
            m_layoutDirty = true;
            ensureLayout_();
            m_caretDesiredX = caretPixelX_(gb);
            ensureCaretVisible_(gb);
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
            m_layoutDirty = true;
            ensureLayout_();
            m_caretDesiredX = caretPixelX_(gb);
            ensureCaretVisible_(gb);
          }
          return true;
        }

        // enter
        if (e.key.code == sf::Keyboard::Enter || e.key.code == sf::Keyboard::Return)
        {
          if (!m_readOnly)
          {
            insertText_("\n");
            m_layoutDirty = true;
            ensureLayout_();
            m_caretDesiredX = caretPixelX_(gb);
            ensureCaretVisible_(gb);
          }
          return true;
        }
      }

      if (!m_readOnly && e.type == sf::Event::TextEntered)
      {
        const sf::Uint32 u = e.text.unicode;

        // ignore control chars (enter handled above for consistent behavior)
        if (u >= 32 && u != 127 && u != '\r' && u != '\n')
        {
          if (u < 128)
          {
            insertText_(std::string(1, static_cast<char>(u)));
            m_layoutDirty = true;
            ensureLayout_();
            m_caretDesiredX = caretPixelX_(gb);
            ensureCaretVisible_(gb);
            return true;
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

      ensureLayout_();

      const float pad = 8.f;
      const float x0 = gb.left + pad;
      const float y0 = gb.top + pad;

      const float clipTop = gb.top + 1.f;
      const float clipBot = gb.top + gb.height - 1.f;

      if (m_value.empty())
      {
        sf::Text t = m_text;
        t.setString(m_placeholder);
        t.setFillColor(m_theme->subtle);
        t.setPosition(snap({x0, y0}));
        rt.draw(t);
        // do NOT return; allow caret draw at {x0,y0}
      }

      // selection highlight (behind text)
      if (focused() && hasSelection_())
      {
        const auto [sa, sb] = selectionRange_();
        sf::Color selC = m_theme->accent;
        selC.a = 60;

        for (const auto &ln : m_lines)
        {
          const float y = y0 + ln.y - m_scrollPx;
          if (y > clipBot)
            break;
          if (y + m_lineH < clipTop)
            continue;

          const size_t lineA = ln.start;
          const size_t lineB = ln.start + ln.len;

          const size_t a = std::max(sa, lineA);
          const size_t b = std::min(sb, lineB);
          if (a >= b)
            continue;

          sf::Text probe = m_text;
          probe.setString(ln.s);
          probe.setPosition({x0, y});

          const float xa = probe.findCharacterPos(static_cast<unsigned>(a - lineA)).x;
          const float xb = probe.findCharacterPos(static_cast<unsigned>(b - lineA)).x;

          sf::RectangleShape r({std::max(0.f, xb - xa), m_lineH});
          r.setPosition(snap({xa, y}));
          r.setFillColor(selC);
          rt.draw(r);
        }
      }

      // draw text lines
      {
        sf::Text t = m_text;
        t.setFillColor(m_theme->text);

        for (const auto &ln : m_lines)
        {
          const float y = y0 + ln.y - m_scrollPx;
          if (y > clipBot)
            break;
          if (y + m_lineH < clipTop)
            continue;

          t.setString(ln.s);
          t.setPosition(snap({x0, y}));
          rt.draw(t);
        }
      }

      // caret
      if (focused() && !m_readOnly)
      {
        const float blink = std::fmod(m_caretClock.getElapsedTime().asSeconds(), 1.f);
        if (blink < 0.5f)
        {
          const sf::Vector2f cp = caretPosPx_(gb);
          if (cp.y >= gb.top + 3.f && cp.y <= gb.top + gb.height - 3.f)
          {
            sf::RectangleShape caret({2.f, m_lineH * 0.78f});
            caret.setPosition(snap({std::min(cp.x, gb.left + gb.width - 6.f),
                                    std::min(cp.y, gb.top + gb.height - caret.getSize().y - 3.f)}));
            caret.setFillColor(m_theme->text);
            rt.draw(caret);
          }
        }
      }

      drawScrollbar_(rt, gb);
    }

  private:
    struct Line
    {
      std::string s;
      size_t start{0}; // start index in m_value
      size_t len{0};   // length in m_value (excludes '\n')
      float y{0.f};
      float w{0.f};
    };

    // ---------- core helpers ----------
    static void normalizeNewlines_(std::string &s)
    {
      s.erase(std::remove(s.begin(), s.end(), '\r'), s.end());
    }

    float lineHeight_() const
    {
      if (!m_text.getFont())
        return 16.f;
      return m_text.getFont()->getLineSpacing(m_text.getCharacterSize());
    }

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
        return m_value; // legacy convenience (copy-all if no selection)
      return m_value.substr(a, b - a);
    }

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
        setCaret_((m_caret > 0) ? (m_caret - 1) : 0, shift);
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

    // ---------- layout that preserves exact indices ----------
    float maxTextWidth_() const
    {
      const float pad = 8.f;
      return std::max(10.f, m_bounds.width - 2.f * pad - m_scrollbarW);
    }

    float measure_(const std::string &s) const
    {
      sf::Text probe = m_text;
      probe.setString(s);
      return probe.getLocalBounds().width;
    }

    void ensureLayout_() const
    {
      if (!m_layoutDirty)
        return;

      m_lines.clear();
      m_lineH = lineHeight_();

      const float maxW = maxTextWidth_();
      const std::string &v = m_value;

      if (v.empty())
      {
        m_contentH = 0.f;
        m_layoutDirty = false;
        return;
      }

      auto pushLine = [&](size_t start, size_t len)
      {
        Line ln;
        ln.start = start;
        ln.len = len;
        ln.s = v.substr(start, len);
        ln.w = measure_(ln.s);
        ln.y = float(m_lines.size()) * m_lineH;
        m_lines.push_back(std::move(ln));
      };

      size_t paraStart = 0;
      while (paraStart <= v.size())
      {
        size_t paraEnd = v.find('\n', paraStart);
        const bool hasNl = (paraEnd != std::string::npos);
        if (!hasNl)
          paraEnd = v.size();

        // wrap [paraStart, paraEnd) without mutating
        if (paraEnd == paraStart)
        {
          pushLine(paraStart, 0);
        }
        else
        {
          size_t lineStart = paraStart;
          size_t lastBreak = std::string::npos;

          for (size_t i = paraStart; i < paraEnd; ++i)
          {
            const char c = v[i];
            if (c == ' ' || c == '\t')
              lastBreak = i + 1; // can break after whitespace

            const size_t candLen = (i + 1) - lineStart;
            const float w = measure_(v.substr(lineStart, candLen));

            if (w > maxW && (i > lineStart))
            {
              if (lastBreak != std::string::npos && lastBreak > lineStart)
              {
                const size_t len = lastBreak - lineStart;
                pushLine(lineStart, len);
                lineStart = lastBreak;
                lastBreak = std::string::npos;
                i = lineStart;
                if (i > 0)
                  --i; // compensate for loop ++i
                continue;
              }
              else
              {
                const size_t len = i - lineStart;
                pushLine(lineStart, len);
                lineStart = i;
                lastBreak = std::string::npos;
                i = lineStart;
                if (i > 0)
                  --i;
                continue;
              }
            }
          }

          if (lineStart <= paraEnd)
            pushLine(lineStart, paraEnd - lineStart);
        }

        if (!hasNl)
          break;

        paraStart = paraEnd + 1;
        if (paraStart > v.size())
          break;
      }

      m_contentH = float(m_lines.size()) * m_lineH;
      clampScroll_();
      m_layoutDirty = false;
    }

    void clampScroll_() const
    {
      const float pad = 8.f;
      const float viewH = std::max(10.f, m_bounds.height - 2.f * pad);
      const float maxScroll = std::max(0.f, m_contentH - viewH);
      m_scrollPx = std::clamp(m_scrollPx, 0.f, maxScroll);
    }

    // ---------- caret mapping ----------
    int lineIndexForCaret_() const
    {
      ensureLayout_();
      if (m_lines.empty())
        return 0;

      // find last line with start <= caret (linear is fine; can binary if needed)
      int best = 0;
      for (int i = 0; i < (int)m_lines.size(); ++i)
      {
        const auto &ln = m_lines[i];
        if (ln.start <= m_caret && m_caret <= ln.start + ln.len)
          best = i;
        if (ln.start > m_caret)
          break;
      }
      return best;
    }

    sf::Vector2f caretPosPx_(const sf::FloatRect &gb) const
    {
      ensureLayout_();
      const float pad = 8.f;
      const float x0 = gb.left + pad;
      const float y0 = gb.top + pad;

      if (m_lines.empty())
        return {x0, y0};

      const int li = lineIndexForCaret_();
      const Line &ln = m_lines[li];
      const size_t col = std::min(m_caret - ln.start, ln.len);

      const float y = y0 + ln.y - m_scrollPx;

      sf::Text probe = m_text;
      probe.setString(ln.s);
      probe.setPosition({x0, y});
      const float x = probe.findCharacterPos(static_cast<unsigned>(col)).x;

      return {x, y};
    }

    float caretPixelX_(const sf::FloatRect &gb) const
    {
      return caretPosPx_(gb).x;
    }

    void ensureCaretVisible_(const sf::FloatRect &gb)
    {
      ensureLayout_();
      const float pad = 8.f;
      const float viewTop = gb.top + pad;
      const float viewBot = gb.top + gb.height - pad;

      const sf::Vector2f cp = caretPosPx_(gb);
      const float caretTop = cp.y;
      const float caretBot = cp.y + m_lineH;

      if (caretTop < viewTop + 2.f)
        m_scrollPx -= (viewTop + 2.f - caretTop);
      else if (caretBot > viewBot - 2.f)
        m_scrollPx += (caretBot - (viewBot - 2.f));

      clampScroll_();
    }

    void scrollToCaret_()
    {
      // used on setText() only
      ensureLayout_();
      const float pad = 8.f;
      const float viewH = std::max(10.f, m_bounds.height - 2.f * pad);
      const float maxScroll = std::max(0.f, m_contentH - viewH);
      m_scrollPx = maxScroll;
      clampScroll_();
    }

    size_t caretFromMouse_(const sf::FloatRect &gb, sf::Vector2f mouse) const
    {
      // If empty, clicking should place caret at start (no layout to hit-test).
      if (m_value.empty())
        return 0;

      ensureLayout_();
      if (m_lines.empty())
        return 0; // defensive

      const float pad = 8.f;
      const float x0 = gb.left + pad;
      const float y0 = gb.top + pad;

      const float localY = (mouse.y - y0) + m_scrollPx;
      int li = int(std::floor(localY / std::max(1.f, m_lineH)));

      // IMPORTANT: clamp manually so we never clamp with hi < lo
      if (li < 0)
        li = 0;
      if (li >= (int)m_lines.size())
        li = (int)m_lines.size() - 1;

      const Line &ln = m_lines[li];

      sf::Text probe = m_text;
      probe.setString(ln.s);
      probe.setPosition({x0, y0 + ln.y - m_scrollPx});

      const float mx = mouse.x;

      if (ln.s.empty())
        return ln.start;

      const float xFirst = probe.findCharacterPos(0).x;
      const float xLast = probe.findCharacterPos(static_cast<unsigned>(ln.s.size())).x;
      if (mx <= xFirst)
        return ln.start;
      if (mx >= xLast)
        return ln.start + ln.len;

      size_t lo = 0;
      size_t hi = ln.s.size();
      while (lo + 1 < hi)
      {
        const size_t mid = (lo + hi) / 2;
        const float xm = probe.findCharacterPos(static_cast<unsigned>(mid)).x;
        if (mx < xm)
          hi = mid;
        else
          lo = mid;
      }

      const float xLo = probe.findCharacterPos(static_cast<unsigned>(lo)).x;
      const float xHi = probe.findCharacterPos(static_cast<unsigned>(lo + 1)).x;
      const size_t col = (mx < (xLo + xHi) * 0.5f) ? lo : (lo + 1);

      return ln.start + std::min(col, ln.len);
    }

    // ---------- vertical navigation ----------
    void moveUp_(bool shift, const sf::FloatRect &gb)
    {
      ensureLayout_();
      if (m_lines.empty())
        return;

      const int li = lineIndexForCaret_();
      if (li <= 0)
      {
        setCaret_(0, shift);
        return;
      }

      const Line &prev = m_lines[li - 1];
      const size_t col = columnForX_(prev, gb, m_caretDesiredX);
      setCaret_(prev.start + col, shift);
    }

    void moveDown_(bool shift, const sf::FloatRect &gb)
    {
      ensureLayout_();
      if (m_lines.empty())
        return;

      const int li = lineIndexForCaret_();
      if (li >= (int)m_lines.size() - 1)
      {
        setCaret_(m_value.size(), shift);
        return;
      }

      const Line &next = m_lines[li + 1];
      const size_t col = columnForX_(next, gb, m_caretDesiredX);
      setCaret_(next.start + col, shift);
    }

    size_t columnForX_(const Line &ln, const sf::FloatRect &gb, float targetX) const
    {
      const float pad = 8.f;
      const float x0 = gb.left + pad;

      sf::Text probe = m_text;
      probe.setString(ln.s);
      probe.setPosition({x0, 0.f});

      if (ln.s.empty())
        return 0;

      // clamp
      const float xFirst = probe.findCharacterPos(0).x;
      const float xLast = probe.findCharacterPos(static_cast<unsigned>(ln.s.size())).x;
      if (targetX <= xFirst)
        return 0;
      if (targetX >= xLast)
        return ln.len;

      size_t lo = 0;
      size_t hi = ln.s.size();
      while (lo + 1 < hi)
      {
        const size_t mid = (lo + hi) / 2;
        const float xm = probe.findCharacterPos(static_cast<unsigned>(mid)).x;
        if (targetX < xm)
          hi = mid;
        else
          lo = mid;
      }

      const float xLo = probe.findCharacterPos(static_cast<unsigned>(lo)).x;
      const float xHi = probe.findCharacterPos(static_cast<unsigned>(lo + 1)).x;
      const size_t col = (targetX < (xLo + xHi) * 0.5f) ? lo : (lo + 1);
      return std::min(col, ln.len);
    }

    void moveLineHome_(bool shift)
    {
      ensureLayout_();
      const int li = lineIndexForCaret_();
      const Line &ln = m_lines[li];
      setCaret_(ln.start, shift);
    }

    void moveLineEnd_(bool shift)
    {
      ensureLayout_();
      const int li = lineIndexForCaret_();
      const Line &ln = m_lines[li];
      setCaret_(ln.start + ln.len, shift);
    }

    // ---------- scrollbar ----------
    void drawScrollbar_(sf::RenderTarget &rt, const sf::FloatRect &gb) const
    {
      ensureLayout_();

      const float pad = 8.f;
      const float viewH = std::max(10.f, gb.height - 2.f * pad);
      if (m_contentH <= viewH + 1.f)
        return;

      const float x = gb.left + gb.width - m_scrollbarW - 4.f;
      const float y = gb.top + pad;
      const float h = viewH;

      sf::RectangleShape track({m_scrollbarW, h});
      track.setPosition(snap({x, y}));
      sf::Color tr = m_theme->inputBorder;
      tr.a = 90;
      track.setFillColor(tr);
      rt.draw(track);

      const float maxScroll = std::max(1.f, m_contentH - viewH);
      const float ratio = viewH / m_contentH;
      const float thumbH = std::max(18.f, h * ratio);
      const float t = (maxScroll <= 0.f) ? 0.f : (m_scrollPx / maxScroll);
      const float thumbY = y + (h - thumbH) * t;

      sf::RectangleShape thumb({m_scrollbarW, thumbH});
      thumb.setPosition(snap({x, thumbY}));
      sf::Color th = (m_hover || focused()) ? m_theme->accent : m_theme->subtle;
      th.a = 160;
      thumb.setFillColor(th);
      rt.draw(thumb);
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
    mutable float m_caretDesiredX{0.f};

    // layout + scrolling
    mutable bool m_layoutDirty{true};
    mutable std::vector<Line> m_lines;
    mutable float m_lineH{16.f};
    mutable float m_contentH{0.f};
    mutable float m_scrollPx{0.f};

    float m_scrollbarW{10.f};
    mutable sf::Clock m_caretClock{};
  };

} // namespace lilia::view::ui
