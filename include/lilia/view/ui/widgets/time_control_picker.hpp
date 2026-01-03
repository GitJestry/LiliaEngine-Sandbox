#pragma once

#include <SFML/Graphics.hpp>
#include <algorithm>
#include <array>
#include <string>

#include "lilia/view/ui/style/style.hpp"
#include "lilia/view/ui/style/theme.hpp"
#include "lilia/view/ui/widgets/button.hpp"

namespace lilia::view::ui
{
  class TimeControlPicker final
  {
  public:
    struct Value
    {
      bool enabled{false};
      int baseSeconds{300};    // e.g. 5:00
      int incrementSeconds{0}; // e.g. +2
    };

    TimeControlPicker() = default;

    TimeControlPicker(const sf::Font &font, const Theme &theme)
    {
      setFont(font);
      setTheme(&theme);
      initOnce();
      syncFromValue();
    }

    void setTheme(const Theme *theme)
    {
      m_theme = theme;
      applyTheme();
    }

    void setFont(const sf::Font &font)
    {
      m_font = &font;

      m_title.setFont(font);
      m_title.setCharacterSize(14);

      m_timeMain.setFont(font);
      m_timeMain.setCharacterSize(20);

      m_incLabel.setFont(font);
      m_incLabel.setCharacterSize(12);

      m_incValue.setFont(font);
      m_incValue.setCharacterSize(16);
    }

    void setValue(Value v)
    {
      m_value = clamp(v);
      syncFromValue();
      layout();
    }

    Value value() const { return m_value; }

    void setBounds(const sf::FloatRect &r)
    {
      m_bounds = r;
      layout();
    }

    void updateHover(sf::Vector2f mouse)
    {
      m_toggle.updateHover(mouse);
      if (!m_value.enabled)
        return;

      m_baseMinus.updateHover(mouse);
      m_basePlus.updateHover(mouse);
      m_incMinus.updateHover(mouse);
      m_incPlus.updateHover(mouse);
      for (auto &b : m_presets)
        b.updateHover(mouse);
    }

    bool handleEvent(const sf::Event &e, sf::Vector2f mouse)
    {
      // keyboard shortcuts (only when enabled)
      if (e.type == sf::Event::KeyPressed && m_value.enabled)
      {
        if (e.key.code == sf::Keyboard::Left)
        {
          stepBase(-(e.key.shift ? 300 : 60));
          return true;
        }
        if (e.key.code == sf::Keyboard::Right)
        {
          stepBase(+(e.key.shift ? 300 : 60));
          return true;
        }
        if (e.key.code == sf::Keyboard::Down)
        {
          stepIncrement(-1);
          return true;
        }
        if (e.key.code == sf::Keyboard::Up)
        {
          stepIncrement(+1);
          return true;
        }
      }

      if (m_toggle.handleEvent(e, mouse))
        return true;

      if (!m_value.enabled)
        return false;

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

      // Toggle always visible (requirement).
      m_toggle.draw(rt);

      // Only draw configuration when enabled (requirement).
      if (!m_value.enabled)
        return;

      // Panel
      sf::RectangleShape panel({m_panelRect.width, m_panelRect.height});
      panel.setPosition(snap({m_panelRect.left, m_panelRect.top}));
      panel.setFillColor(m_theme->panel);
      panel.setOutlineThickness(1.f);
      panel.setOutlineColor(m_theme->panelBorder);
      rt.draw(panel);

      // Text
      rt.draw(m_title);
      rt.draw(m_timeMain);
      rt.draw(m_incLabel);
      rt.draw(m_incValue);

      // Controls
      m_baseMinus.draw(rt);
      m_basePlus.draw(rt);

      m_incMinus.draw(rt);
      m_incPlus.draw(rt);

      for (const auto &b : m_presets)
        b.draw(rt);
    }

  private:
    static int clampBaseSeconds(int s) { return std::clamp(s, 60, 2 * 60 * 60); }
    static int clampIncSeconds(int s) { return std::clamp(s, 0, 30); }

    static Value clamp(Value v)
    {
      v.baseSeconds = clampBaseSeconds(v.baseSeconds);
      v.incrementSeconds = clampIncSeconds(v.incrementSeconds);
      return v;
    }

    static std::string formatHMS(int totalSeconds)
    {
      totalSeconds = std::max(0, totalSeconds);
      int h = totalSeconds / 3600;
      int m = (totalSeconds % 3600) / 60;
      int s = totalSeconds % 60;

      auto two = [](int x)
      {
        std::string t = std::to_string(x);
        return (x < 10) ? ("0" + t) : t;
      };

      return two(h) + ":" + two(m) + ":" + two(s);
    }

    void initOnce()
    {
      if (m_inited || !m_theme || !m_font)
        return;

      // ---- Toggle ----
      m_toggle.setTheme(m_theme);
      m_toggle.setFont(*m_font);
      m_toggle.setText("TIME OFF", 14);
      m_toggle.setOnClick([this]
                          {
      m_value.enabled = !m_value.enabled;
      syncFromValue();
      layout(); });

      // ---- Base +/- ----
      m_baseMinus.setTheme(m_theme);
      m_baseMinus.setFont(*m_font);
      m_baseMinus.setText("-", 18);
      m_baseMinus.setOnClick([this]
                             { stepBase(-60); });

      m_basePlus.setTheme(m_theme);
      m_basePlus.setFont(*m_font);
      m_basePlus.setText("+", 18);
      m_basePlus.setOnClick([this]
                            { stepBase(+60); });

      // ---- Increment +/- ----
      m_incMinus.setTheme(m_theme);
      m_incMinus.setFont(*m_font);
      m_incMinus.setText("-", 16);
      m_incMinus.setOnClick([this]
                            { stepIncrement(-1); });

      m_incPlus.setTheme(m_theme);
      m_incPlus.setFont(*m_font);
      m_incPlus.setText("+", 16);
      m_incPlus.setOnClick([this]
                           { stepIncrement(+1); });

      // ---- Presets ----
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
        m_presets[i].setOnClick([this, i]
                                {
        m_value.baseSeconds = clampBaseSeconds(m_presetDefs[i].base);
        m_value.incrementSeconds = clampIncSeconds(m_presetDefs[i].inc);
        syncFromValue();
        layout(); });
      }

      m_title.setString("Time Control");
      m_incLabel.setString("Increment");

      m_inited = true;
    }

    void applyTheme()
    {
      if (!m_theme || !m_font)
        return;

      initOnce();

      m_title.setFillColor(m_theme->subtle);
      m_timeMain.setFillColor(m_theme->text);
      m_incLabel.setFillColor(m_theme->subtle);
      m_incValue.setFillColor(m_theme->text);
    }

    void syncFromValue()
    {
      if (!m_theme)
        return;

      m_toggle.setAccent(m_value.enabled);
      m_toggle.setText(m_value.enabled ? "TIME ON" : "TIME OFF", 14);

      const bool en = m_value.enabled;
      m_baseMinus.setEnabled(en);
      m_basePlus.setEnabled(en);
      m_incMinus.setEnabled(en);
      m_incPlus.setEnabled(en);
      for (auto &b : m_presets)
        b.setEnabled(en);

      m_timeMain.setString(formatHMS(m_value.baseSeconds));
      m_incValue.setString("+" + std::to_string(m_value.incrementSeconds) + "s");

      const int sel = detectPresetIndex();
      for (std::size_t i = 0; i < m_presets.size(); ++i)
        m_presets[i].setActive((int)i == sel);

      applyTheme();
    }

    int detectPresetIndex() const
    {
      for (std::size_t i = 0; i < m_presetDefs.size(); ++i)
      {
        if (m_value.baseSeconds == m_presetDefs[i].base &&
            m_value.incrementSeconds == m_presetDefs[i].inc)
          return (int)i;
      }
      return -1;
    }

    void stepBase(int delta)
    {
      m_value.baseSeconds = clampBaseSeconds(m_value.baseSeconds + delta);
      syncFromValue();
      layout();
    }

    void stepIncrement(int delta)
    {
      m_value.incrementSeconds = clampIncSeconds(m_value.incrementSeconds + delta);
      syncFromValue();
      layout();
    }

    void layout()
    {
      if (!m_theme || !m_inited)
        return;

      if (m_bounds.width <= 0.f || m_bounds.height <= 0.f)
        return;

      const float pad = 12.f;

      // Toggle (always)
      const float toggleW = 170.f;
      const float toggleH = 34.f;

      sf::FloatRect toggleRect{
          m_bounds.left + (m_bounds.width - toggleW) * 0.5f,
          m_bounds.top,
          toggleW,
          toggleH};
      m_toggle.setBounds(toggleRect);

      // If disabled: nothing else is laid out.
      if (!m_value.enabled)
      {
        m_panelRect = {};
        return;
      }

      // Panel under toggle
      const float gap = 10.f;
      const float panelY = toggleRect.top + toggleRect.height + gap;

      const float panelW = std::min(520.f, m_bounds.width);
      const float panelH = 96.f;

      m_panelRect = {
          m_bounds.left + (m_bounds.width - panelW) * 0.5f,
          panelY,
          panelW,
          panelH};

      // Title top-left
      m_title.setPosition(snap({m_panelRect.left + pad, m_panelRect.top + 6.f}));

      // Base row (centered group)
      const float baseRowY = m_panelRect.top + 30.f;
      const float baseBtnS = 28.f;
      const float baseGap = 10.f;
      const float timeBoxW = 130.f;
      const float groupW = baseBtnS + baseGap + timeBoxW + baseGap + baseBtnS;

      const float groupX = m_panelRect.left + (m_panelRect.width - groupW) * 0.5f;

      m_baseMinus.setBounds({groupX, baseRowY, baseBtnS, baseBtnS});
      m_basePlus.setBounds({groupX + baseBtnS + baseGap + timeBoxW + baseGap, baseRowY, baseBtnS, baseBtnS});

      sf::FloatRect timeBox{
          groupX + baseBtnS + baseGap,
          baseRowY,
          timeBoxW,
          baseBtnS};
      centerText(m_timeMain, timeBox, -1.f);

      // Bottom row: increment left, presets right (panelW is designed to be wide enough in StartScreen)
      const float bottomY = m_panelRect.top + m_panelRect.height - 30.f;

      m_incLabel.setPosition(snap({m_panelRect.left + pad, bottomY - 8.f}));

      const float incBtnS = 24.f;
      const float incGap = 6.f;
      const float incValueW = 58.f;

      // Increment group right edge
      const float incRight = m_panelRect.left + pad + 220.f;
      m_incPlus.setBounds({incRight - incBtnS, bottomY, incBtnS, incBtnS});
      m_incMinus.setBounds({incRight - incBtnS - incGap - incBtnS, bottomY, incBtnS, incBtnS});

      sf::FloatRect incValBox{
          m_incMinus.bounds().left - incGap - incValueW,
          bottomY,
          incValueW,
          incBtnS};
      centerText(m_incValue, incValBox, -1.f);

      // Presets right-aligned
      const float chipH = 24.f;
      const float chipW = 74.f;
      const float chipGap = 10.f;
      const float chipsW = 3.f * chipW + 2.f * chipGap;

      float chipsX = m_panelRect.left + m_panelRect.width - pad - chipsW;
      float chipsY = bottomY;

      for (std::size_t i = 0; i < m_presets.size(); ++i)
      {
        sf::FloatRect r{chipsX + i * (chipW + chipGap), chipsY, chipW, chipH};
        m_presets[i].setBounds(r);
      }
    }

    struct PresetDef
    {
      const char *label;
      int base;
      int inc;
    };

    const sf::Font *m_font{nullptr};
    const Theme *m_theme{nullptr};

    bool m_inited{false};

    sf::FloatRect m_bounds{};
    sf::FloatRect m_panelRect{};

    Value m_value{};

    Button m_toggle{};
    Button m_baseMinus{};
    Button m_basePlus{};
    Button m_incMinus{};
    Button m_incPlus{};
    std::array<Button, 3> m_presets{};

    std::array<PresetDef, 3> m_presetDefs{};

    sf::Text m_title{};
    sf::Text m_timeMain{};
    sf::Text m_incLabel{};
    sf::Text m_incValue{};
  };

} // namespace lilia::view::ui
