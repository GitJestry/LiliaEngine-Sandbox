#pragma once

#include <SFML/Graphics.hpp>

#include <algorithm>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "lilia/engine/uci/engine_registry.hpp"
#include "lilia/view/ui/platform/file_dialog.hpp"
#include "lilia/view/ui/render/engine_icons.hpp"
#include "lilia/view/ui/render/layout.hpp"
#include "lilia/view/ui/style/modals/modal.hpp"
#include "lilia/view/ui/style/style.hpp"
#include "lilia/view/ui/style/theme.hpp"
#include "lilia/view/ui/widgets/button.hpp"

namespace lilia::view
{
  class BotCatalogModal final : public ui::Modal
  {
  public:
    // current: engineId of currently selected engine (empty => none)
    BotCatalogModal(const sf::Font &font, const ui::Theme &theme, std::string currentEngineId)
        : m_font(font), m_theme(theme), m_currentEngineId(std::move(currentEngineId))
    {
      m_title.setFont(m_font);
      m_title.setCharacterSize(22);
      m_title.setFillColor(m_theme.text);
      m_title.setString("Bot Catalog");

      m_hint.setFont(m_font);
      m_hint.setCharacterSize(14);
      m_hint.setFillColor(m_theme.subtle);
      m_hint.setString("Choose an engine. Lilia and Stockfish are built-ins; others are external.");

      m_status.setFont(m_font);
      m_status.setCharacterSize(14);
      m_status.setFillColor(m_theme.subtle);
      m_status.setString("");

      m_close.setTheme(&m_theme);
      m_close.setFont(m_font);
      m_close.setText("Close", 16);
      m_close.setOnClick([this]
                         { m_closing = true; });

      m_upload.setTheme(&m_theme);
      m_upload.setFont(m_font);
      m_upload.setText("Upload Engine...", 14);
      m_upload.setOnClick([this]
                          { onUploadClicked(); });

      rebuildFromRegistry();
    }

    std::optional<lilia::config::EngineRef> picked() const { return m_picked; }

    void layout(sf::Vector2u ws) override
    {
      m_ws = ws;

      const float w = std::min(760.f, std::max(540.f, float(ws.x) - 120.f));
      const float h = std::min(560.f, std::max(420.f, float(ws.y) - 160.f));

      m_rect = ui::anchoredCenter(ws, {w, h});
      m_inner = ui::inset(m_rect, 18.f);

      sf::FloatRect top = ui::rowConsume(m_inner, 44.f, 10.f);
      m_title.setPosition(ui::snap({top.left, top.top + 8.f}));
      m_close.setBounds({top.left + top.width - 120.f, top.top + 6.f, 120.f, 32.f});

      sf::FloatRect hint = ui::rowConsume(m_inner, 22.f, 6.f);
      m_hint.setPosition(ui::snap({hint.left, hint.top}));

      sf::FloatRect status = ui::rowConsume(m_inner, 22.f, 10.f);
      m_status.setPosition(ui::snap({status.left, status.top}));

      sf::FloatRect sep = ui::rowConsume(m_inner, 10.f, 10.f);
      m_sepRect = {sep.left, sep.top + 5.f, sep.width, 1.f};

      // Footer
      sf::FloatRect footer = {m_inner.left, m_inner.top + m_inner.height - 40.f, m_inner.width, 40.f};
      m_upload.setBounds({footer.left, footer.top + 4.f, 220.f, 32.f});

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

      sf::RectangleShape sep({m_sepRect.width, m_sepRect.height});
      sep.setPosition(ui::snap({m_sepRect.left, m_sepRect.top}));
      sf::Color sc = m_theme.panelBorder;
      sc.a = sf::Uint8(float(sc.a) * 0.8f * m_anim);
      sep.setFillColor(sc);
      win.draw(sep);

      // Title/hint/status with alpha
      auto alphaText = [&](sf::Text t)
      {
        sf::Color c = t.getFillColor();
        c.a = sf::Uint8(float(c.a) * m_anim);
        t.setFillColor(c);
        win.draw(t);
      };

      alphaText(m_title);
      alphaText(m_hint);
      alphaText(m_status);

      m_close.draw(win, {}, m_anim);
      m_upload.draw(win, {}, m_anim);

      // List rows
      const float rowH = 52.f;
      const float padX = 14.f;

      for (std::size_t i = 0; i < m_rows.size(); ++i)
      {
        const auto &row = m_rows[i];

        sf::FloatRect r{
            m_listRect.left,
            m_listRect.top + float(i) * rowH,
            m_listRect.width,
            rowH - 6.f};

        const bool hov = r.contains(m_mouse);
        const bool sel = (!m_currentEngineId.empty() && row.ref.engineId == m_currentEngineId);

        sf::Color base = sel ? m_theme.buttonActive : m_theme.button;
        if (hov && !sel)
          base = m_theme.buttonHover;
        base.a = sf::Uint8(float(base.a) * m_anim);

        ui::drawBevelButton(win, r, base, hov, false);

        // Left: display name
        sf::Text name(row.ref.displayName, m_font, 16);
        sf::Color ntc = m_theme.text;
        ntc.a = sf::Uint8(float(ntc.a) * m_anim);
        name.setFillColor(ntc);
        ui::leftCenterText(name, r, padX);

        sf::Color vc = m_theme.subtle;
        vc.a = sf::Uint8(float(vc.a) * m_anim);

        win.draw(name);

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
        // click outside closes
        if (!m_rect.contains(mouse))
        {
          m_closing = true;
          return true;
        }

        const float rowH = 52.f;
        if (m_listRect.contains(mouse))
        {
          const int idx = int((mouse.y - m_listRect.top) / rowH);
          if (idx >= 0 && idx < int(m_rows.size()))
          {
            m_picked = m_rows[std::size_t(idx)].ref;
            m_currentEngineId = m_picked->engineId;
            m_closing = true;
            return true;
          }
        }
      }

      return false;
    }

  private:
    struct Row
    {
      lilia::config::EngineRef ref;
      bool builtin{false};
    };

    void rebuildFromRegistry()
    {
      m_rows.clear();

      auto &reg = lilia::engine::uci::EngineRegistry::instance();
      reg.load();

      auto lowerCopy = [](std::string s)
      {
        std::transform(s.begin(), s.end(), s.begin(),
                       [](unsigned char c)
                       { return char(std::tolower(c)); });
        return s;
      };

      auto looksLike = [&](const lilia::engine::uci::EngineEntry &e, const std::string &token)
      {
        const std::string t = lowerCopy(token);
        const std::string id = lowerCopy(e.ref.engineId);
        const std::string dn = lowerCopy(e.ref.displayName);
        const std::string in = lowerCopy(e.id.name);
        return (id == t) || (id.find(t) != std::string::npos) ||
               (dn.find(t) != std::string::npos) ||
               (in.find(t) != std::string::npos);
      };

      const bool haveBuiltinLilia = [&]
      {
        auto x = reg.get("lilia");
        return x && x->builtin;
      }();

      const bool haveBuiltinSf = [&]
      {
        auto x = reg.get("stockfish");
        return x && x->builtin;
      }();

      auto list = reg.list();

      // Stable ordering: built-ins first, then alphabetical by displayName.
      std::sort(list.begin(), list.end(), [](const auto &a, const auto &b)
                {
    if (a.builtin != b.builtin) return a.builtin > b.builtin;
    return a.ref.displayName < b.ref.displayName; });

      for (const auto &e : list)
      {
        // Hide external duplicates of builtins if the builtin exists.
        if (!e.builtin && haveBuiltinLilia && looksLike(e, "lilia"))
          continue;
        if (!e.builtin && haveBuiltinSf && looksLike(e, "stockfish"))
          continue;

        Row r;
        r.ref = e.ref;
        r.builtin = e.builtin;

        if (r.ref.displayName.empty())
          r.ref.displayName = !e.id.name.empty() ? e.id.name : r.ref.engineId;

        if (r.ref.version.empty())
          r.ref.version = "unknown";

        m_rows.push_back(std::move(r));
      }

      if (m_rows.empty())
      {
        m_status.setString("No engines registered.");
      }
      else
      {
        m_status.setString("");
        // If current is empty, default to Lilia if present; else first entry.
        if (m_currentEngineId.empty())
        {
          auto it = std::find_if(m_rows.begin(), m_rows.end(),
                                 [](const Row &r)
                                 { return r.ref.engineId == "lilia"; });
          m_currentEngineId = (it != m_rows.end()) ? it->ref.engineId : m_rows.front().ref.engineId;
        }
      }
    }

    void onUploadClicked()
    {
      m_status.setFillColor(m_theme.subtle);
      m_status.setString("");

      auto path = ui::platform::openExecutableFileDialog();
      if (!path || path->empty())
        return;

      auto &reg = lilia::engine::uci::EngineRegistry::instance();
      std::string err;
      auto installed = reg.installExternal(*path, &err);
      if (!installed)
      {
        m_status.setFillColor(sf::Color(220, 90, 90));
        m_status.setString(err.empty() ? "Failed to install engine." : err);
        return;
      }

      // Refresh list and select the new engine
      rebuildFromRegistry();
      m_currentEngineId = installed->ref.engineId;

      m_status.setFillColor(sf::Color(90, 200, 120));
      m_status.setString("Engine installed: " + installed->ref.displayName);
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
    sf::Text m_status{};

    ui::Button m_close{};
    ui::Button m_upload{};

    std::string m_currentEngineId{};
    std::vector<Row> m_rows{};
    std::optional<lilia::config::EngineRef> m_picked{};

    float m_anim{0.f};
    bool m_closing{false};
  };
} // namespace lilia::view
