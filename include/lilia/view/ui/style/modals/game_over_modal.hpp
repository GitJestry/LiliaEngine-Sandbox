#pragma once

#include <SFML/Graphics.hpp>

#include <algorithm>
#include <functional>
#include <string>

#include "../palette_cache.hpp"
#include "modal.hpp"
#include "../style.hpp"
#include "../theme.hpp"
#include "lilia/view/ui/widgets/button.hpp"

namespace lilia::view::ui
{

  class GameOverModal final : public Modal
  {
  public:
    struct Params
    {
      const Theme *theme{};
      const sf::Font *font{};
      std::function<void()> onNewBot{};
      std::function<void()> onRematch{};
      std::function<void()> onClose{};
    };

    void open(sf::Vector2u ws, sf::Vector2f anchorCenter, const std::string &title, bool won, Params p)
    {
      m_open = true;
      m_dismissed = false;
      m_closing = false;
      m_anim = 0.f;

      m_ws = ws;
      m_anchor = anchorCenter;
      m_theme = p.theme;
      m_font = p.font;
      m_won = won;
      m_titleStr = title;

      m_onNewBot = std::move(p.onNewBot);
      m_onRematch = std::move(p.onRematch);
      m_onClose = std::move(p.onClose);

      build();
      layout(ws);
    }

    void close()
    {
      m_open = false;
      m_dismissed = true;
    }

    bool isOpen() const { return m_open; }
    void setAnchor(sf::Vector2f c)
    {
      m_anchor = c;
      if (m_open)
        layout(m_ws);
    }

    // Modal
    void layout(sf::Vector2u ws) override
    {
      m_ws = ws;
      if (!m_open)
        return;

      const sf::Vector2f size{380.f, 190.f};
      sf::FloatRect r{m_anchor.x - size.x * 0.5f, m_anchor.y - size.y * 0.5f, size.x, size.y};
      r.left = snapf(r.left);
      r.top = snapf(r.top);
      m_panel = r;

      // Close button (top-right)
      const float closeS = 28.f;
      sf::FloatRect closeR{r.left + r.width - closeS - 10.f, r.top + 10.f, closeS, closeS};
      m_btnClose.setBounds(closeR);

      // Action buttons
      const float btnW = 120.f;
      const float btnH = 36.f;
      const float gap = 16.f;
      const float by = r.top + r.height - 52.f;

      sf::FloatRect leftBtn{r.left + r.width * 0.5f - gap - btnW, by, btnW, btnH};
      sf::FloatRect rightBtn{r.left + r.width * 0.5f + gap, by, btnW, btnH};
      m_btnLeft.setBounds(leftBtn);
      m_btnRight.setBounds(rightBtn);

      layoutTrophyAndTitle();
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

    void updateInput(sf::Vector2f mouse, bool mouseDown) override
    {
      m_mouse = mouse;
      m_btnLeft.updateInput(mouse, mouseDown);
      m_btnRight.updateInput(mouse, mouseDown);
      m_btnClose.updateInput(mouse, mouseDown);
    }

    void drawOverlay(sf::RenderWindow &win) const override
    {
      if (!m_open || !m_theme)
        return;

      sf::RectangleShape ov({float(m_ws.x), float(m_ws.y)});
      ov.setPosition(0.f, 0.f);
      sf::Color c = m_theme->toastBg;
      c.a = static_cast<sf::Uint8>(float(c.a) * m_anim);
      ov.setFillColor(c);
      win.draw(ov);
    }

    void drawPanel(sf::RenderWindow &win) const override
    {
      if (!m_open || !m_theme)
        return;

      drawPanelShadow(win, m_panel);

      sf::RectangleShape panel({m_panel.width, m_panel.height});
      panel.setPosition(snap({m_panel.left, m_panel.top}));
      sf::Color p = m_theme->panel;
      p.a = static_cast<sf::Uint8>(float(p.a) * m_anim);
      panel.setFillColor(p);
      panel.setOutlineThickness(1.f);
      sf::Color ob = m_theme->panelBorder;
      ob.a = static_cast<sf::Uint8>(float(ob.a) * m_anim);
      panel.setOutlineColor(ob);
      win.draw(panel);

      if (m_won)
      {
        // Apply fade to trophy primitives.
        m_trophyCup.setFillColor(alphaMul(m_gold, m_anim));
        m_trophyStem.setFillColor(alphaMul(m_gold, m_anim));
        m_trophyBase.setFillColor(alphaMul(m_gold, m_anim));

        m_trophyHandleL.setOutlineColor(alphaMul(m_gold, m_anim));
        m_trophyHandleR.setOutlineColor(alphaMul(m_gold, m_anim));

        win.draw(m_trophyHandleL);
        win.draw(m_trophyHandleR);
        win.draw(m_trophyCup);
        win.draw(m_trophyStem);
        win.draw(m_trophyBase);
      }

      // Apply fade to text without mutating state (keeps draw() const).
      sf::Text title = m_title;
      title.setFillColor(alphaMul(m_theme->text, m_anim));
      win.draw(title);

      // Fade buttons in/out consistently with the panel.
      m_btnLeft.draw(win, {}, m_anim);
      m_btnRight.draw(win, {}, m_anim);
      m_btnClose.draw(win, {}, m_anim);
    }

    bool handleEvent(const sf::Event &e, sf::Vector2f mouse) override
    {
      if (!m_open || !m_theme)
        return false;

      // Buttons
      if (m_btnLeft.handleEvent(e, mouse))
        return true;
      if (m_btnRight.handleEvent(e, mouse))
        return true;
      if (m_btnClose.handleEvent(e, mouse))
        return true;

      // Escape closes
      if (e.type == sf::Event::KeyPressed && e.key.code == sf::Keyboard::Escape)
      {
        if (m_onClose)
          m_onClose();
        m_closing = true;
        return true;
      }

      // Click outside closes
      if (e.type == sf::Event::MouseButtonPressed && e.mouseButton.button == sf::Mouse::Left)
      {
        if (!m_panel.contains(mouse))
        {
          if (m_onClose)
            m_onClose();
          m_closing = true;
          return true;
        }
      }

      return false;
    }

    bool dismissed() const override { return m_dismissed; }

  private:
    static sf::Color alphaMul(sf::Color c, float mul)
    {
      c.a = static_cast<sf::Uint8>(static_cast<float>(c.a) * std::clamp(mul, 0.f, 1.f));
      return c;
    }

    void build()
    {
      if (!m_theme || !m_font)
        return;

      m_title.setFont(*m_font);
      m_title.setCharacterSize(28);
      m_title.setStyle(sf::Text::Bold);
      m_title.setString(m_titleStr);
      m_title.setFillColor(m_theme->text);

      // Cache trophy base color once (palette lookups are stable but keep draw hot-path lean).
      m_gold = PaletteCache::get().color(ColorId::COL_GOLD);

      m_btnLeft.setTheme(m_theme);
      m_btnLeft.setFont(*m_font);
      m_btnLeft.setText("New Bot", 16);
      m_btnLeft.setAccent(false);
      m_btnLeft.setOnClick([this]
                           {
        if (m_onNewBot) m_onNewBot();
        m_closing = true; });

      m_btnRight.setTheme(m_theme);
      m_btnRight.setFont(*m_font);
      m_btnRight.setText("Rematch", 16);
      m_btnRight.setAccent(true);
      m_btnRight.setOnClick([this]
                            {
        if (m_onRematch) m_onRematch();
        m_closing = true; });

      m_btnClose.setTheme(m_theme);
      m_btnClose.setFont(*m_font);
      m_btnClose.setText("X", 16);
      m_btnClose.setAccent(false);
      m_btnClose.setOnClick([this]
                            {
        if (m_onClose) m_onClose();
        m_closing = true; });
    }

    void layoutTrophyAndTitle()
    {
      if (!m_theme)
        return;

      const float left = m_panel.left;
      const float top = m_panel.top;
      const float W = m_panel.width;
      const float centerX = left + W * 0.5f;

      float textTop = top + 22.f;

      if (m_won)
      {
        const sf::Color gold = m_gold;

        const float cupW = 60.f;
        const float cupH = 40.f;
        const float stemH = 10.f;
        const float baseH = 10.f;
        const float trophyTop = top + 14.f;

        m_trophyCup.setPointCount(4);
        m_trophyCup.setPoint(0, {0.f, 0.f});
        m_trophyCup.setPoint(1, {cupW, 0.f});
        m_trophyCup.setPoint(2, {cupW * 0.8f, cupH});
        m_trophyCup.setPoint(3, {cupW * 0.2f, cupH});
        m_trophyCup.setFillColor(gold);
        m_trophyCup.setPosition(snap({centerX - cupW * 0.5f, trophyTop}));

        const float handleR = 12.f;
        m_trophyHandleL.setRadius(handleR);
        m_trophyHandleL.setPointCount(30);
        m_trophyHandleL.setFillColor(sf::Color::Transparent);
        m_trophyHandleL.setOutlineThickness(4.f);
        m_trophyHandleL.setOutlineColor(gold);
        m_trophyHandleL.setPosition(snap({centerX - cupW * 0.5f - handleR + 1.f, trophyTop + 5.f}));

        m_trophyHandleR.setRadius(handleR);
        m_trophyHandleR.setPointCount(30);
        m_trophyHandleR.setFillColor(sf::Color::Transparent);
        m_trophyHandleR.setOutlineThickness(4.f);
        m_trophyHandleR.setOutlineColor(gold);
        m_trophyHandleR.setPosition(snap({centerX + cupW * 0.5f - handleR - 1.f, trophyTop + 5.f}));

        m_trophyStem.setSize({cupW * 0.3f, stemH});
        m_trophyStem.setFillColor(gold);
        m_trophyStem.setPosition(snap({centerX - (cupW * 0.3f) * 0.5f, trophyTop + cupH}));

        m_trophyBase.setSize({cupW * 0.6f, baseH});
        m_trophyBase.setFillColor(gold);
        m_trophyBase.setPosition(snap({centerX - (cupW * 0.6f) * 0.5f, trophyTop + cupH + stemH}));

        textTop = trophyTop + cupH + stemH + baseH + 10.f;
      }

      auto tb = m_title.getLocalBounds();
      m_title.setPosition(snap({centerX - tb.width * 0.5f - tb.left, textTop - tb.top}));
    }

  private:
    bool m_open{false};
    bool m_dismissed{false};
    bool m_closing{false};
    float m_anim{0.f};

    sf::Vector2u m_ws{};
    sf::Vector2f m_anchor{};
    sf::Vector2f m_mouse{};

    const Theme *m_theme{nullptr};
    const sf::Font *m_font{nullptr};

    std::string m_titleStr{};
    bool m_won{false};

    sf::FloatRect m_panel{};
    sf::Text m_title{};

    // Cached accent color for the trophy (from the active palette).
    sf::Color m_gold{};

    // Trophy primitives
    mutable sf::ConvexShape m_trophyCup{};
    mutable sf::RectangleShape m_trophyStem{};
    mutable sf::RectangleShape m_trophyBase{};
    mutable sf::CircleShape m_trophyHandleL{};
    mutable sf::CircleShape m_trophyHandleR{};

    Button m_btnLeft{};
    Button m_btnRight{};
    Button m_btnClose{};

    std::function<void()> m_onNewBot{};
    std::function<void()> m_onRematch{};
    std::function<void()> m_onClose{};
  };

} // namespace lilia::view::ui
