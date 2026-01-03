#pragma once

#include <SFML/Graphics.hpp>
#include <SFML/Window/Clipboard.hpp>

#include <algorithm>
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
      normalizeNewlines(s);
      m_value = std::move(s);
      m_layoutDirty = true;
      scrollToBottom();
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

      if (e.type == sf::Event::MouseWheelScrolled)
      {
        if (gb.contains(mouse))
        {
          ensureLayout();
          const float step = lineHeight() * 3.f;
          m_scrollPx -= e.mouseWheelScroll.delta * step;
          clampScroll();
          return true;
        }
      }

      if (!focused() || m_readOnly)
        return false;

      if (e.type == sf::Event::KeyPressed)
      {
        if ((e.key.control || e.key.system) && e.key.code == sf::Keyboard::V)
        {
          std::string clip = sf::Clipboard::getString().toAnsiString();
          normalizeNewlines(clip);
          m_value += clip;
          m_layoutDirty = true;
          scrollToBottom();
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
        {
          if (!m_value.empty())
          {
            m_value.pop_back();
            m_layoutDirty = true;
            scrollToBottom();
          }
          return true;
        }
        if (e.text.unicode == 13 || e.text.unicode == 10)
        {
          m_value.push_back('\n');
          m_layoutDirty = true;
          scrollToBottom();
          return true;
        }
        if (e.text.unicode >= 32 && e.text.unicode < 127)
        {
          m_value.push_back(char(e.text.unicode));
          m_layoutDirty = true;
          scrollToBottom();
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

      ensureLayout();

      const float pad = 8.f;
      const float x0 = gb.left + pad;
      const float y0 = gb.top + pad;

      const float clipTop = gb.top;
      const float clipBot = gb.top + gb.height;

      if (m_value.empty())
      {
        sf::Text t = m_text;
        t.setString(m_placeholder);
        t.setFillColor(m_theme->subtle);
        t.setPosition(snap({x0, y0}));
        rt.draw(t);
      }
      else
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

      // caret at end
      if (focused() && !m_readOnly)
      {
        float blink = std::fmod(m_caretClock.getElapsedTime().asSeconds(), 1.f);
        if (blink < 0.5f)
        {
          const sf::Vector2f cpos = caretPosPx(gb);
          if (cpos.y >= gb.top + 3.f && cpos.y <= gb.top + gb.height - 3.f)
          {
            sf::RectangleShape caret({2.f, m_lineH * 0.78f});
            caret.setPosition(snap({std::min(cpos.x, gb.left + gb.width - 6.f),
                                    std::min(cpos.y, gb.top + gb.height - caret.getSize().y - 3.f)}));
            caret.setFillColor(m_theme->text);
            rt.draw(caret);
          }
        }
      }

      drawScrollbar(rt, gb);
    }

  private:
    struct Line
    {
      std::string s;
      float y;
      float w;
    };

    static void normalizeNewlines(std::string &s)
    {
      s.erase(std::remove(s.begin(), s.end(), '\r'), s.end());
    }

    float lineHeight() const
    {
      if (!m_text.getFont())
        return 16.f;
      return m_text.getFont()->getLineSpacing(m_text.getCharacterSize());
    }

    void ensureLayout() const
    {
      if (!m_layoutDirty)
        return;

      m_lines.clear();
      m_lineH = lineHeight();

      const float pad = 8.f;
      const float maxW = std::max(10.f, m_bounds.width - 2.f * pad - m_scrollbarW);

      auto pushLine = [&](std::string s)
      {
        sf::Text probe = m_text;
        probe.setString(s);
        const float w = probe.getLocalBounds().width;
        const float y = (float)m_lines.size() * m_lineH;
        m_lines.push_back(Line{std::move(s), y, w});
      };

      if (m_value.empty())
      {
        m_contentH = 0.f;
        m_layoutDirty = false;
        return;
      }

      size_t start = 0;
      while (start <= m_value.size())
      {
        size_t end = m_value.find('\n', start);
        if (end == std::string::npos)
          end = m_value.size();

        std::string para = m_value.substr(start, end - start);
        wrapParagraph(para, maxW, pushLine);

        if (end == m_value.size())
          break;

        pushLine(std::string{});
        start = end + 1;
      }

      m_contentH = (float)m_lines.size() * m_lineH;
      clampScroll();
      m_layoutDirty = false;
    }

    template <class PushFn>
    void wrapParagraph(const std::string &para, float maxW, PushFn pushLine) const
    {
      if (para.empty())
      {
        pushLine(std::string{});
        return;
      }

      auto measure = [&](const std::string &s) -> float
      {
        sf::Text probe = m_text;
        probe.setString(s);
        return probe.getLocalBounds().width;
      };

      size_t i = 0;
      std::string current;

      auto flush = [&]()
      {
        if (!current.empty())
        {
          pushLine(current);
          current.clear();
        }
      };

      while (i < para.size())
      {
        while (i < para.size() && para[i] == ' ')
        {
          if (!current.empty())
            current.push_back(' ');
          ++i;
        }

        size_t j = i;
        while (j < para.size() && para[j] != ' ')
          ++j;

        std::string word = para.substr(i, j - i);
        i = j;

        if (word.empty())
          continue;

        std::string trial = current.empty() ? word : (current + (current.back() == ' ' ? "" : " ") + word);

        if (measure(trial) <= maxW)
        {
          current = std::move(trial);
          continue;
        }

        if (!current.empty())
        {
          flush();
          if (measure(word) <= maxW)
          {
            current = word;
            continue;
          }
        }

        std::string chunk;
        for (char c : word)
        {
          std::string t = chunk;
          t.push_back(c);
          if (measure(t) <= maxW || chunk.empty())
          {
            chunk.push_back(c);
          }
          else
          {
            pushLine(chunk);
            chunk.clear();
            chunk.push_back(c);
          }
        }
        if (!chunk.empty())
          pushLine(chunk);
      }

      flush();
    }

    void clampScroll() const
    {
      const float pad = 8.f;
      const float viewH = std::max(10.f, m_bounds.height - 2.f * pad);
      const float maxScroll = std::max(0.f, m_contentH - viewH);
      m_scrollPx = std::clamp(m_scrollPx, 0.f, maxScroll);
    }

    void scrollToBottom()
    {
      ensureLayout();
      const float pad = 8.f;
      const float viewH = std::max(10.f, m_bounds.height - 2.f * pad);
      const float maxScroll = std::max(0.f, m_contentH - viewH);
      m_scrollPx = maxScroll;
    }

    sf::Vector2f caretPosPx(const sf::FloatRect &gb) const
    {
      ensureLayout();
      const float pad = 8.f;
      const float x0 = gb.left + pad;
      const float y0 = gb.top + pad;

      if (m_lines.empty())
        return {x0, y0};

      const Line &last = m_lines.back();
      float x = x0 + last.w + 1.f;
      float y = y0 + last.y - m_scrollPx;

      x = std::min(x, gb.left + gb.width - 8.f);
      return {x, y};
    }

    void drawScrollbar(sf::RenderTarget &rt, const sf::FloatRect &gb) const
    {
      ensureLayout();

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

    mutable bool m_layoutDirty{true};
    mutable std::vector<Line> m_lines;
    mutable float m_lineH{16.f};
    mutable float m_contentH{0.f};
    mutable float m_scrollPx{0.f};

    float m_scrollbarW{10.f};
    mutable sf::Clock m_caretClock{};
  };

} // namespace lilia::view::ui
