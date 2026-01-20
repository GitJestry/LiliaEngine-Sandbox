#pragma once

#include <SFML/Graphics.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <optional>
#include <string>

#include "lilia/view/ui/interaction/focus.hpp"
#include "lilia/view/ui/style/style.hpp"
#include "lilia/view/ui/style/theme.hpp"
#include "lilia/view/ui/widgets/button.hpp"
#include "lilia/view/ui/widgets/text_field.hpp"
#include "lilia/view/ui/widgets/toggle_pill.hpp"

namespace lilia::view::ui
{
  class TimeControlPicker final
  {
  public:
    struct Value
    {
      bool enabled{false};
      int baseSeconds{300};
      int incrementSeconds{0};
    };

    TimeControlPicker() = default;

    TimeControlPicker(const sf::Font &font, const Theme &theme)
    {
      setFont(font);
      setTheme(&theme);
      initOnce();
      syncFromValue_(/*forceFields=*/true);
    }

    void setTheme(const Theme *theme)
    {
      m_theme = theme;
      applyTheme_();
    }

    void setFont(const sf::Font &font)
    {
      m_font = &font;

      m_title.setFont(font);
      m_title.setCharacterSize(15);

      m_baseLabel.setFont(font);
      m_baseLabel.setCharacterSize(12);

      m_incLabel.setFont(font);
      m_incLabel.setCharacterSize(12);

      // TextField fonts
      m_baseField.setFont(font);
      m_incField.setFont(font);

      m_toggle.setFont(font);
    }

    void setFocusManager(FocusManager *fm)
    {
      m_focus = fm;
      m_baseField.setFocusManager(fm);
      m_incField.setFocusManager(fm);
    }

    void setValue(Value v)
    {
      m_value = clamp_(v);
      syncFromValue_(/*forceFields=*/true);
      layout_();
    }

    Value value() const { return m_value; }

    void setBounds(const sf::FloatRect &r)
    {
      m_bounds = r;
      layout_();
    }

    void updateHover(sf::Vector2f mouse)
    {
      m_toggle.updateHover(mouse);

      if (!m_value.enabled)
        return;

      m_baseField.updateHover(mouse);
      m_incField.updateHover(mouse);

      m_baseMinus.updateHover(mouse);
      m_basePlus.updateHover(mouse);
      m_incMinus.updateHover(mouse);
      m_incPlus.updateHover(mouse);

      for (auto &b : m_presets)
        b.updateHover(mouse);
    }

    bool handleEvent(const sf::Event &e, sf::Vector2f mouse)
    {
      // Toggle always active
      if (m_toggle.handleEvent(e, mouse))
        return true;

      // If disabled, also ensure any editor focus is cleared.
      if (!m_value.enabled)
      {
        blurEditors_();
        return false;
      }

      // Track focus transitions to commit on blur
      const bool wasBaseFocused = m_baseField.focused();
      const bool wasIncFocused = m_incField.focused();

      // Filter TextEntered so inputs stay sane (optional but recommended)
      if (e.type == sf::Event::TextEntered)
      {
        const sf::Uint32 u = e.text.unicode;
        if (u < 32 || u == 127)
        {
          // ignore control chars
        }
        else if (u < 128)
        {
          const char ch = static_cast<char>(u);

          if (m_baseField.focused())
          {
            // digits, ':', space, +, -, h/m/s
            if (!(std::isdigit((unsigned char)ch) || ch == ':' || ch == ' ' || ch == '+' || ch == '-' ||
                  ch == 'h' || ch == 'H' || ch == 'm' || ch == 'M' || ch == 's' || ch == 'S'))
              return true; // consume invalid
          }
          if (m_incField.focused())
          {
            // digits, space, +, -, s
            if (!(std::isdigit((unsigned char)ch) || ch == ' ' || ch == '+' || ch == '-' ||
                  ch == 's' || ch == 'S'))
              return true; // consume invalid
          }
        }
      }

      // Keyboard commit/revert (only when a field is focused)
      if (e.type == sf::Event::KeyPressed)
      {
        if (m_baseField.focused() || m_incField.focused())
        {
          if (e.key.code == sf::Keyboard::Enter || e.key.code == sf::Keyboard::Return)
          {
            commitEditors_();
            return true;
          }
          if (e.key.code == sf::Keyboard::Escape)
          {
            // revert fields to current value + blur
            syncFieldsFromValue_(/*force=*/true);
            blurEditors_();
            return true;
          }
        }
      }

      // Always forward mouse presses to fields so clicking outside will blur them.
      // (Your TextField only clears focus on outside clicks if it receives the event.)
      bool consumed = false;

      // Give text fields first shot (so clicks in them don't hit +/- behind them)
      consumed = m_baseField.handleEvent(e, mouse) || consumed;
      consumed = m_incField.handleEvent(e, mouse) || consumed;

      // If a field lost focus due to this event, commit its current text
      if (wasBaseFocused && !m_baseField.focused())
        commitBaseFromField_();
      if (wasIncFocused && !m_incField.focused())
        commitIncFromField_();

      if (consumed)
        return true;

      // Keyboard shortcuts (only when NOT editing a field)
      if (e.type == sf::Event::KeyPressed && !(m_baseField.focused() || m_incField.focused()))
      {
        if (e.key.code == sf::Keyboard::Left)
        {
          stepBase_(-(e.key.shift ? 300 : 60));
          return true;
        }
        if (e.key.code == sf::Keyboard::Right)
        {
          stepBase_(+(e.key.shift ? 300 : 60));
          return true;
        }
        if (e.key.code == sf::Keyboard::Down)
        {
          stepIncrement_(-1);
          return true;
        }
        if (e.key.code == sf::Keyboard::Up)
        {
          stepIncrement_(+1);
          return true;
        }
      }

      // Buttons
      if (m_baseMinus.handleEvent(e, mouse))
        return true;
      if (m_basePlus.handleEvent(e, mouse))
        return true;

      if (m_incMinus.handleEvent(e, mouse))
        return true;
      if (m_incPlus.handleEvent(e, mouse))
        return true;

      for (auto &b : m_presets)
      {
        if (b.handleEvent(e, mouse))
          return true;
      }

      return false;
    }

    void draw(sf::RenderTarget &rt) const
    {
      if (!m_theme)
        return;

      // Toggle always visible
      m_toggle.draw(rt);

      if (!m_value.enabled)
        return;

      // Card background
      drawSoftShadowRect(rt, m_panelRect, sf::Color(0, 0, 0, 70), 2, 2.f);

      sf::RectangleShape card({m_panelRect.width, m_panelRect.height});
      card.setPosition(snap({m_panelRect.left, m_panelRect.top}));
      card.setFillColor(m_theme->panel);
      card.setOutlineThickness(1.f);
      sf::Color ob = m_theme->panelBorder;
      ob.a = 120;
      card.setOutlineColor(ob);
      rt.draw(card);

      rt.draw(m_title);
      rt.draw(m_baseLabel);
      rt.draw(m_incLabel);

      m_baseField.draw(rt);
      m_incField.draw(rt);

      m_baseMinus.draw(rt);
      m_basePlus.draw(rt);
      m_incMinus.draw(rt);
      m_incPlus.draw(rt);

      for (const auto &b : m_presets)
        b.draw(rt);
    }

  private:
    // ---------------- parsing / formatting ----------------
    static Value clamp_(Value v)
    {
      v.baseSeconds = std::clamp(v.baseSeconds, 60, 2 * 60 * 60);
      v.incrementSeconds = std::clamp(v.incrementSeconds, 0, 30);
      return v;
    }

    static std::string trim_(std::string s)
    {
      auto issp = [](unsigned char c)
      { return std::isspace(c) != 0; };
      while (!s.empty() && issp((unsigned char)s.front()))
        s.erase(s.begin());
      while (!s.empty() && issp((unsigned char)s.back()))
        s.pop_back();
      return s;
    }

    static std::string lower_(std::string s)
    {
      for (char &c : s)
        c = char(std::tolower((unsigned char)c));
      return s;
    }

    static std::string formatClock_(int totalSeconds)
    {
      totalSeconds = std::max(0, totalSeconds);
      const int h = totalSeconds / 3600;
      const int m = (totalSeconds % 3600) / 60;
      const int s = totalSeconds % 60;

      auto two = [](int x)
      {
        std::string t = std::to_string(x);
        return (x < 10) ? ("0" + t) : t;
      };

      if (h > 0)
        return std::to_string(h) + ":" + two(m) + ":" + two(s);

      return std::to_string(m) + ":" + two(s);
    }

    // Base input accepted:
    // - "5:00", "10:00", "1:05:00"
    // - "5"  (minutes)
    // - "300" (seconds because >= 60)
    // - "5m", "300s"
    static bool parseBase_(const std::string &in, int &outSeconds)
    {
      std::string s = lower_(trim_(in));
      if (s.empty())
        return false;

      // suffix handling
      if (s.size() >= 2 && (s.back() == 'm' || s.back() == 's' || s.back() == 'h'))
      {
        char suf = s.back();
        s.pop_back();
        s = trim_(s);
        if (s.empty())
          return false;

        // integer
        long n = 0;
        try
        {
          n = std::stol(s);
        }
        catch (...)
        {
          return false;
        }

        if (suf == 'h')
          outSeconds = int(n * 3600);
        else if (suf == 'm')
          outSeconds = int(n * 60);
        else
          outSeconds = int(n);

        return true;
      }

      // clock format
      if (s.find(':') != std::string::npos)
      {
        // split by ':'
        int parts[3] = {0, 0, 0};
        int count = 0;

        std::string cur;
        for (char c : s)
        {
          if (c == ':')
          {
            if (cur.empty() || count >= 3)
              return false;
            parts[count++] = std::stoi(cur);
            cur.clear();
          }
          else
          {
            if (!std::isdigit((unsigned char)c))
              return false;
            cur.push_back(c);
          }
        }

        if (!cur.empty())
        {
          if (count >= 3)
            return false;
          parts[count++] = std::stoi(cur);
        }

        if (count == 2)
        {
          // m:ss
          int mm = parts[0];
          int ss = parts[1];
          outSeconds = mm * 60 + ss;
          return true;
        }
        if (count == 3)
        {
          // h:mm:ss
          int hh = parts[0];
          int mm = parts[1];
          int ss = parts[2];
          outSeconds = hh * 3600 + mm * 60 + ss;
          return true;
        }

        return false;
      }

      // plain number: minutes if < 60 else seconds
      long n = 0;
      try
      {
        n = std::stol(s);
      }
      catch (...)
      {
        return false;
      }

      if (n < 60)
        outSeconds = int(n * 60);
      else
        outSeconds = int(n);

      return true;
    }

    // Increment input accepted:
    // - "2", "+2", "2s", "+2s"
    static bool parseInc_(const std::string &in, int &outSeconds)
    {
      std::string s = lower_(trim_(in));
      if (s.empty())
        return false;

      if (!s.empty() && s.front() == '+')
        s.erase(s.begin());

      s = trim_(s);

      if (!s.empty() && s.back() == 's')
      {
        s.pop_back();
        s = trim_(s);
      }

      if (s.empty())
        return false;

      long n = 0;
      try
      {
        n = std::stol(s);
      }
      catch (...)
      {
        return false;
      }

      outSeconds = int(n);
      return true;
    }

    // ---------------- init / theme / sync ----------------
    void initOnce()
    {
      if (m_inited || !m_theme || !m_font)
        return;

      // Toggle
      m_toggle.setTheme(m_theme);
      m_toggle.setLabel("Time Control", 14);
      m_toggle.setValue(m_value.enabled);
      m_toggle.setOnToggle([this](bool on)
                           {
                             m_value.enabled = on;
                             if (!m_value.enabled)
                               blurEditors_();
                             syncFromValue_(/*forceFields=*/true);
                             layout_(); });

      // Buttons
      auto initAdj = [&](Button &b, const char *txt, unsigned sz)
      {
        b.setTheme(m_theme);
        b.setFont(*m_font);
        b.setText(txt, sz);
        b.setHoverOutline(true);
      };

      initAdj(m_baseMinus, "-", 16);
      initAdj(m_basePlus, "+", 16);
      initAdj(m_incMinus, "-", 16);
      initAdj(m_incPlus, "+", 16);

      m_baseMinus.setOnClick([this]
                             {
                               commitEditors_();
                               stepBase_(-60); });
      m_basePlus.setOnClick([this]
                            {
                              commitEditors_();
                              stepBase_(+60); });
      m_incMinus.setOnClick([this]
                            {
                              commitEditors_();
                              stepIncrement_(-1); });
      m_incPlus.setOnClick([this]
                           {
                             commitEditors_();
                             stepIncrement_(+1); });

      // Text fields
      m_baseField.setTheme(m_theme);
      m_baseField.setCharacterSize(22);
      m_baseField.setPlaceholder("5:00");
      m_baseField.setReadOnly(false);
      if (m_focus)
        m_baseField.setFocusManager(m_focus);

      m_incField.setTheme(m_theme);
      m_incField.setCharacterSize(20);
      m_incField.setPlaceholder("0s");
      m_incField.setReadOnly(false);
      if (m_focus)
        m_incField.setFocusManager(m_focus);

      // Presets
      m_presetDefs = {{
          {"Bullet", 60, 0},
          {"Blitz", 180, 2},
          {"Rapid", 600, 0},
      }};

      for (std::size_t i = 0; i < m_presets.size(); ++i)
      {
        m_presets[i].setTheme(m_theme);
        m_presets[i].setFont(*m_font);
        m_presets[i].setText(m_presetDefs[i].label, 13);
        m_presets[i].setHoverOutline(true);
        m_presets[i].setOnClick([this, i]
                                {
                                  commitEditors_();
                                  m_value.baseSeconds = std::clamp(m_presetDefs[i].base, 60, 2 * 60 * 60);
                                  m_value.incrementSeconds = std::clamp(m_presetDefs[i].inc, 0, 30);
                                  syncFromValue_(/*forceFields=*/true);
                                  layout_(); });
      }

      m_title.setString("Time Settings");
      m_baseLabel.setString("Base");
      m_incLabel.setString("Increment");

      m_inited = true;
      applyTheme_();
    }

    void applyTheme_()
    {
      if (!m_theme || !m_font)
        return;

      initOnce();

      m_title.setFillColor(m_theme->text);
      m_baseLabel.setFillColor(m_theme->subtle);
      m_incLabel.setFillColor(m_theme->subtle);

      m_baseField.setTheme(m_theme);
      m_incField.setTheme(m_theme);

      m_toggle.setTheme(m_theme);
    }

    void syncFieldsFromValue_(bool force)
    {
      // Avoid overwriting while user is actively editing
      if (force || !m_baseField.focused())
        m_baseField.setText(formatClock_(m_value.baseSeconds));

      if (force || !m_incField.focused())
        m_incField.setText(std::to_string(m_value.incrementSeconds) + "s");
    }

    void syncFromValue_(bool forceFields)
    {
      m_toggle.setValue(m_value.enabled);

      const bool en = m_value.enabled;
      m_baseMinus.setEnabled(en);
      m_basePlus.setEnabled(en);
      m_incMinus.setEnabled(en);
      m_incPlus.setEnabled(en);
      for (auto &b : m_presets)
        b.setEnabled(en);

      const int sel = detectPresetIndex_();
      for (std::size_t i = 0; i < m_presets.size(); ++i)
        m_presets[i].setActive((int)i == sel);

      syncFieldsFromValue_(forceFields);
      applyTheme_();
    }

    int detectPresetIndex_() const
    {
      for (std::size_t i = 0; i < m_presetDefs.size(); ++i)
      {
        if (m_value.baseSeconds == m_presetDefs[i].base &&
            m_value.incrementSeconds == m_presetDefs[i].inc)
          return (int)i;
      }
      return -1;
    }

    // ---------------- commit / stepping ----------------
    void commitBaseFromField_()
    {
      int sec = 0;
      if (!parseBase_(m_baseField.text(), sec))
      {
        // revert on invalid
        syncFieldsFromValue_(/*force=*/true);
        return;
      }

      m_value.baseSeconds = std::clamp(sec, 60, 2 * 60 * 60);
      syncFromValue_(/*forceFields=*/true);
      layout_();
    }

    void commitIncFromField_()
    {
      int sec = 0;
      if (!parseInc_(m_incField.text(), sec))
      {
        syncFieldsFromValue_(/*force=*/true);
        return;
      }

      m_value.incrementSeconds = std::clamp(sec, 0, 30);
      syncFromValue_(/*forceFields=*/true);
      layout_();
    }

    void commitEditors_()
    {
      if (m_baseField.focused())
        commitBaseFromField_();
      if (m_incField.focused())
        commitIncFromField_();
    }

    void stepBase_(int delta)
    {
      m_value.baseSeconds = std::clamp(m_value.baseSeconds + delta, 60, 2 * 60 * 60);
      syncFromValue_(/*forceFields=*/true);
      layout_();
    }

    void stepIncrement_(int delta)
    {
      m_value.incrementSeconds = std::clamp(m_value.incrementSeconds + delta, 0, 30);
      syncFromValue_(/*forceFields=*/true);
      layout_();
    }

    void blurEditors_()
    {
      if (m_focus)
      {
        if (m_baseField.focused() || m_incField.focused())
          m_focus->clear();
      }
    }

    void layout_()
    {
      if (!m_theme)
        return;

      initOnce();

      if (m_bounds.width <= 0.f || m_bounds.height <= 0.f)
        return;

      // Shared width for toggle + card (keeps the component visually aligned)
      const float maxW = 560.f;
      const float panelW = std::min(maxW, m_bounds.width);
      const float panelX = m_bounds.left + (m_bounds.width - panelW) * 0.5f;

      // Toggle
      const float toggleH = 40.f;
      const float toggleY = m_bounds.top;
      m_toggle.setBounds({panelX, toggleY, panelW, toggleH});

      if (!m_value.enabled)
      {
        m_panelRect = {};
        return;
      }

      // Card sizing: fill remaining height under toggle (with a minimum to fit content)
      const float gap = 12.f;
      const float availH = std::max(0.f, m_bounds.height - (toggleH + gap));

      const float padT = 14.f;
      const float padB = 20.f; // a bit deeper so the outline goes lower
      const float titleH = 18.f;
      const float sectionGap = 10.f;
      const float rowH = 34.f;
      const float rowGap = 10.f;
      const float chipH = 30.f;

      const float needH = padT + titleH + sectionGap + rowH + rowGap + rowH + rowGap + chipH + padB;

      float cardH = std::max(needH, availH);

      const float cardY = toggleY + toggleH + gap;
      m_panelRect = {panelX, cardY, panelW, cardH};

      const float innerL = m_panelRect.left + padT;
      const float innerR = m_panelRect.left + m_panelRect.width - padT;

      // Title
      m_title.setPosition(snap({innerL, m_panelRect.top + padT}));

      // Grid
      const float labelW = 96.f;
      const float btnS = 32.f;
      const float xGap = 10.f;

      const float row1Y = m_panelRect.top + padT + titleH + sectionGap;
      const float row2Y = row1Y + rowH + rowGap;
      const float row3Y = row2Y + rowH + rowGap;

      sf::FloatRect baseLabelBox{innerL, row1Y, labelW, rowH};
      sf::FloatRect incLabelBox{innerL, row2Y, labelW, rowH};
      leftCenterText(m_baseLabel, baseLabelBox, 0.f);
      leftCenterText(m_incLabel, incLabelBox, 0.f);

      const float contentL = innerL + labelW + 12.f;
      const float contentR = innerR;

      // Base row: [-] [field........] [+]
      {
        const float fieldX = contentL + btnS + xGap;
        const float fieldW = std::max(120.f, (contentR - btnS - xGap) - fieldX);

        m_baseMinus.setBounds({contentL, row1Y, btnS, rowH});
        m_baseField.setBounds({fieldX, row1Y, fieldW, rowH});
        m_basePlus.setBounds({contentR - btnS, row1Y, btnS, rowH});
      }

      // Increment row: [-] [field] [+]
      {
        const float fieldX = contentL + btnS + xGap;
        const float fieldW = std::max(88.f, (contentR - btnS - xGap) - fieldX);

        m_incMinus.setBounds({contentL, row2Y, btnS, rowH});
        m_incField.setBounds({fieldX, row2Y, fieldW, rowH});
        m_incPlus.setBounds({contentR - btnS, row2Y, btnS, rowH});
      }

      // Presets row (dedicated row, centered, consistent spacing)
      {
        const float chipGap = 10.f;
        const float areaW = std::max(0.f, contentR - contentL);

        const float preferredChipW = 92.f;
        const float minChipW = 74.f;

        float chipW = preferredChipW;
        float chipsW = 3.f * chipW + 2.f * chipGap;
        if (chipsW > areaW)
        {
          chipW = std::max(minChipW, (areaW - 2.f * chipGap) / 3.f);
          chipsW = 3.f * chipW + 2.f * chipGap;
        }

        const float chipsX = contentL + (areaW - chipsW) * 0.5f;

        for (std::size_t i = 0; i < m_presets.size(); ++i)
          m_presets[i].setBounds({chipsX + float(i) * (chipW + chipGap), row3Y, chipW, chipH});
      }
    }

  private:
    struct PresetDef
    {
      const char *label;
      int base;
      int inc;
    };

    const sf::Font *m_font{nullptr};
    const Theme *m_theme{nullptr};
    FocusManager *m_focus{nullptr};

    bool m_inited{false};

    sf::FloatRect m_bounds{};
    sf::FloatRect m_panelRect{};

    Value m_value{};

    TogglePill m_toggle{};

    TextField m_baseField{};
    TextField m_incField{};

    Button m_baseMinus{};
    Button m_basePlus{};
    Button m_incMinus{};
    Button m_incPlus{};

    std::array<Button, 3> m_presets{};
    std::array<PresetDef, 3> m_presetDefs{};

    sf::Text m_title{};
    sf::Text m_baseLabel{};
    sf::Text m_incLabel{};
  };
} // namespace lilia::view::ui
