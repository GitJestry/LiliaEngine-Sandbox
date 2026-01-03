#pragma once

#include <SFML/Graphics.hpp>

#include <algorithm>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "lilia/bot/bot_info.hpp"
#include "lilia/view/ui/render/layout.hpp"
#include "modal.hpp"
#include "../style.hpp"
#include "../theme.hpp"
#include "lilia/view/ui/widgets/button.hpp"

namespace lilia::view
{

  struct EngineChoice
  {
    bool external{false};
    BotType builtin{BotType::Lilia};
    std::string displayName{"Lilia"};
    std::string version{"(unknown)"};
    std::string externalPath{};
  };

  inline std::string botName(BotType t)
  {
    return getBotConfig(t).info.name;
  }
  inline std::string botVersion(BotType)
  {
    return "1.0"; // placeholder
  }

  class BotCatalogModal final : public ui::Modal
  {
  public:
    BotCatalogModal(const sf::Font &font, const ui::Theme &theme, EngineChoice current)
        : m_font(font), m_theme(theme), m_current(std::move(current))
    {
      m_title.setFont(m_font);
      m_title.setCharacterSize(22);
      m_title.setFillColor(m_theme.text);
      m_title.setString("Bot Catalog");

      m_hint.setFont(m_font);
      m_hint.setCharacterSize(14);
      m_hint.setFillColor(m_theme.subtle);
      m_hint.setString("Choose a built-in engine. External upload is a placeholder.");

      m_close.setTheme(&m_theme);
      m_close.setFont(m_font);
      m_close.setText("Close", 16);
      m_close.setOnClick([this]
                         { m_closing = true; });

      m_upload.setTheme(&m_theme);
      m_upload.setFont(m_font);
      m_upload.setText("Upload Engine (placeholder)", 14);
      m_upload.setOnClick([this] { /* placeholder */ });

      buildBuiltinList();
    }

    std::optional<EngineChoice> picked() const { return m_picked; }

    void layout(sf::Vector2u ws) override
    {
      m_ws = ws;

      const float w = std::min(740.f, std::max(520.f, float(ws.x) - 120.f));
      const float h = std::min(520.f, std::max(380.f, float(ws.y) - 160.f));

      m_rect = ui::anchoredCenter(ws, {w, h});
      m_inner = ui::inset(m_rect, 18.f);

      sf::FloatRect top = ui::rowConsume(m_inner, 44.f, 10.f);
      m_title.setPosition(ui::snap({top.left, top.top + 8.f}));
      m_close.setBounds({top.left + top.width - 120.f, top.top + 6.f, 120.f, 32.f});

      sf::FloatRect hint = ui::rowConsume(m_inner, 22.f, 12.f);
      m_hint.setPosition(ui::snap({hint.left, hint.top}));

      // Separator line area (visual cleanup)
      sf::FloatRect sep = ui::rowConsume(m_inner, 10.f, 10.f);
      m_sepRect = {sep.left, sep.top + 5.f, sep.width, 1.f};

      // Footer for upload (keeps list clean)
      sf::FloatRect footer = {m_inner.left, m_inner.top + m_inner.height - 40.f, m_inner.width, 40.f};
      m_upload.setBounds({footer.left, footer.top + 4.f, 280.f, 32.f});

      // List area above footer
      m_listRect = {m_inner.left, m_inner.top, m_inner.width, (footer.top - m_inner.top) - 10.f};
    }

    void update(float dt) override
    {
      const float speed = 10.f;
      const float target = m_closing ? 0.f : 1.f;

      if (m_anim < target)
        m_anim = std::min(target, m_anim + speed * dt);
      if (m_anim > target)
        m_anim = std::max(target, m_anim - speed * dt);

      if (m_closing && m_anim <= 0.01f)
        requestDismiss();
    }

    void updateInput(sf::Vector2f mouse, bool /*mouseDown*/) override
    {
      m_mouse = mouse;
      m_close.updateHover(mouse);
      m_upload.updateHover(mouse);
    }

    void drawOverlay(sf::RenderWindow &win) const override
    {
      sf::RectangleShape dim({float(m_ws.x), float(m_ws.y)});
      dim.setPosition(0.f, 0.f);
      dim.setFillColor(sf::Color(0, 0, 0, sf::Uint8(150 * m_anim)));
      win.draw(dim);
    }

    void drawPanel(sf::RenderWindow &win) const override
    {
      ui::drawPanelShadow(win, m_rect);

      sf::RectangleShape panel({m_rect.width, m_rect.height});
      panel.setPosition(ui::snap({m_rect.left, m_rect.top}));
      sf::Color p = m_theme.panel;
      p.a = sf::Uint8(float(p.a) * m_anim);
      panel.setFillColor(p);
      panel.setOutlineThickness(1.f);
      sf::Color ob = m_theme.panelBorder;
      ob.a = sf::Uint8(float(ob.a) * m_anim);
      panel.setOutlineColor(ob);
      win.draw(panel);

      // Separator
      sf::RectangleShape sep({m_sepRect.width, m_sepRect.height});
      sep.setPosition(ui::snap({m_sepRect.left, m_sepRect.top}));
      sf::Color sc = m_theme.panelBorder;
      sc.a = sf::Uint8(float(sc.a) * 0.8f * m_anim);
      sep.setFillColor(sc);
      win.draw(sep);

      // Title/hint with alpha
      sf::Text title = m_title;
      sf::Color tc = title.getFillColor();
      tc.a = sf::Uint8(float(tc.a) * m_anim);
      title.setFillColor(tc);

      sf::Text hint = m_hint;
      sf::Color hc = hint.getFillColor();
      hc.a = sf::Uint8(float(hc.a) * m_anim);
      hint.setFillColor(hc);

      win.draw(title);
      win.draw(hint);

      m_close.draw(win, {}, m_anim);
      m_upload.draw(win, {}, m_anim);

      // List rows
      const float rowH = 52.f;
      const float padX = 14.f;

      for (std::size_t i = 0; i < m_builtin.size(); ++i)
      {
        sf::FloatRect r{
            m_listRect.left,
            m_listRect.top + float(i) * rowH,
            m_listRect.width,
            rowH - 6.f};

        const bool hov = r.contains(m_mouse);
        const bool sel = (!m_current.external && m_builtin[i].builtin == m_current.builtin);

        sf::Color base = sel ? m_theme.buttonActive : m_theme.button;
        if (hov && !sel)
          base = m_theme.buttonHover;

        base.a = sf::Uint8(float(base.a) * m_anim);

        ui::drawBevelButton(win, r, base, hov, false);

        sf::Text name(m_builtin[i].displayName, m_font, 16);
        sf::Color ntc = m_theme.text;
        ntc.a = sf::Uint8(float(ntc.a) * m_anim);
        name.setFillColor(ntc);
        ui::leftCenterText(name, r, padX);

        sf::Text ver("v " + m_builtin[i].version, m_font, 14);
        sf::Color vc = m_theme.subtle;
        vc.a = sf::Uint8(float(vc.a) * m_anim);
        ver.setFillColor(vc);
        ver.setPosition(ui::snap({r.left + r.width - 140.f, r.top + 14.f}));

        win.draw(name);
        win.draw(ver);

        if (sel)
        {
          sf::Color ac = m_theme.accent;
          ac.a = sf::Uint8(float(ac.a) * m_anim);
          ui::drawAccentInset(win, r, ac);
        }
      }
    }

    bool handleEvent(const sf::Event &e, sf::Vector2f mouse) override
    {
      m_mouse = mouse;

      if (e.type == sf::Event::KeyPressed && e.key.code == sf::Keyboard::Escape && closeOnEsc())
      {
        m_closing = true;
        return true;
      }

      if (m_close.handleEvent(e, mouse))
        return true;
      if (m_upload.handleEvent(e, mouse))
        return true;

      if (e.type == sf::Event::MouseButtonPressed && e.mouseButton.button == sf::Mouse::Left)
      {
        // click outside panel closes
        if (!m_rect.contains(mouse))
        {
          m_closing = true;
          return true;
        }

        const float rowH = 52.f;
        if (m_listRect.contains(mouse))
        {
          const int idx = int((mouse.y - m_listRect.top) / rowH);
          if (idx >= 0 && idx < int(m_builtin.size()))
          {
            m_picked = m_builtin[std::size_t(idx)];
            m_current = *m_picked;
            m_closing = true;
            return true;
          }
        }
      }

      return false;
    }

  private:
    void buildBuiltinList()
    {
      m_builtin.clear();
      const std::vector<BotType> types{BotType::Lilia};

      for (auto t : types)
      {
        EngineChoice c;
        c.external = false;
        c.builtin = t;
        c.displayName = botName(t);
        c.version = botVersion(t);
        m_builtin.push_back(std::move(c));
      }
    }

    const sf::Font &m_font;
    const ui::Theme &m_theme;

    sf::Vector2u m_ws{};
    sf::FloatRect m_rect{};
    sf::FloatRect m_inner{};
    sf::FloatRect m_listRect{};
    sf::FloatRect m_sepRect{};
    sf::Vector2f m_mouse{};

    sf::Text m_title{};
    sf::Text m_hint{};

    ui::Button m_close{};
    ui::Button m_upload{};

    EngineChoice m_current{};
    std::vector<EngineChoice> m_builtin;
    std::optional<EngineChoice> m_picked{};

    float m_anim{0.f};
    bool m_closing{false};
  };

} // namespace lilia::view
