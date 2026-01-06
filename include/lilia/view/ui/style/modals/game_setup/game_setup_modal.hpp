#pragma once

#include <SFML/Graphics.hpp>

#include <functional>
#include <optional>
#include <string>

#include "../modal.hpp"
#include "lilia/view/ui/render/layout.hpp"
#include "lilia/view/ui/interaction/focus.hpp"
#include "lilia/view/ui/widgets/button.hpp"

#include "../../theme.hpp"
#include "../../style.hpp"

#include "game_setup_types.hpp"
#include "game_setup_page_pgn_fen.hpp"
#include "game_setup_page_builder.hpp"
#include "game_setup_page_history.hpp"

namespace lilia::view::ui
{
  class GameSetupModal final : public Modal
  {
  public:
    GameSetupModal(const sf::Font &font, const ui::Theme &theme, ui::FocusManager &focus)
        : m_font(font), m_theme(theme), m_focus(focus),
          m_pagePgnFen(font, theme, focus),
          m_pageBuilder(font, theme),
          m_pageHistory(font, theme)
    {
      m_title.setFont(m_font);
      m_title.setCharacterSize(20);
      m_title.setFillColor(m_theme.text);
      m_title.setString("Load Game / Create Start Position");

      // Header: History / Back
      setup_action(m_historyBtn, "History  →", [this]
                   { m_showHistory = true; });
      setup_action(m_backBtn, "←  Back", [this]
                   { m_showHistory = false; });

      setup_action(m_close, "Close", [this]
                   { requestDismiss(); });

      setup_action(m_continue, "Use Position", [this]
                   {
        const std::string rf = resolvedFen();
        // Guard: do not dismiss if resolved is empty/invalid (prevents confusing “nothing happens”).
        // If you want full legality check, do it in controller/model after retrieving rf.
        if (rf.empty())
          return;
        m_resultFen = rf;
        requestDismiss(); });

      // Tabs
      setup_action(m_tabPgnFen, "PGN / FEN", [this]
                   { m_mode = game_setup::Mode::PgnFen; });
      setup_action(m_tabBuild, "Builder", [this]
                   {
               m_mode = game_setup::Mode::Builder;
               m_pageBuilder.onOpen(); });

      // Forward upload callback into page
      m_pagePgnFen.setOnRequestPgnUpload([this]
                                         {
        if (m_onRequestPgnUpload)
          m_onRequestPgnUpload(); });
    }
    void setOnRequestPgnUpload(std::function<void()> cb)
    {
      m_onRequestPgnUpload = std::move(cb);
    }

    // Controller may call these after upload
    void setFenText(const std::string &fen) { m_pagePgnFen.setFenText(fen); }
    void setPgnText(const std::string &pgn) { m_pagePgnFen.setPgnText(pgn); }
    void setPgnFilename(const std::string &name) { m_pagePgnFen.setPgnFilename(name); }

    std::optional<std::string> resultFen() const { return m_resultFen; }

    void layout(sf::Vector2u ws) override
    {
      m_ws = ws;

      // Modal geometry
      m_rect = ui::anchoredCenter(ws, {900.f, 640.f});
      m_inner = ui::inset(m_rect, 18.f);

      // Header
      sf::FloatRect header = ui::rowConsume(m_inner, 44.f, 12.f);
      m_title.setPosition(ui::snap({header.left, header.top + 9.f}));

      const float hBtnH = 30.f;
      const float closeW = 92.f;
      const float navW = 140.f;

      m_close.setBounds({header.left + header.width - closeW, header.top + 7.f, closeW, hBtnH});

      const float navX = header.left + header.width - closeW - 10.f - navW;
      m_historyBtn.setBounds({navX, header.top + 7.f, navW, hBtnH});
      m_backBtn.setBounds({navX, header.top + 7.f, navW, hBtnH});

      // Footer
      sf::FloatRect footer = {m_rect.left + 18.f, m_rect.top + m_rect.height - 52.f,
                              m_rect.width - 36.f, 34.f};
      m_continue.setBounds({footer.left + footer.width - 200.f, footer.top + 2.f, 200.f, 32.f});

      // Content
      m_pages = {m_rect.left + 18.f, header.top + header.height + 12.f, m_rect.width - 36.f,
                 m_rect.height - 18.f - (header.height + 12.f) - 52.f};

      // Tabs row
      sf::FloatRect content = m_pages;
      sf::FloatRect tabs = ui::rowConsume(content, 32.f, 12.f);

      sf::FloatRect t = tabs;
      auto a = ui::colConsume(t, 120.f, 8.f);
      auto b = ui::colConsume(t, 92.f, 8.f);

      m_tabPgnFen.setBounds(a);
      m_tabBuild.setBounds(b);

      m_contentRect = content;

      // Pages layout
      m_pagePgnFen.layout(m_contentRect);
      m_pageBuilder.layout(m_contentRect);
      m_pageHistory.layout(m_contentRect);
    }

    void update(float /*dt*/) override
    {
      if (!m_showHistory)
      {
        if (m_mode == game_setup::Mode::PgnFen)
          m_pagePgnFen.update();
        else
          m_pageBuilder.update();
      }
    }

    void updateInput(sf::Vector2f mouse, bool /*mouseDown*/) override
    {
      m_mouse = mouse;

      m_close.updateHover(mouse);
      m_continue.updateHover(mouse);

      if (!m_showHistory)
        m_historyBtn.updateHover(mouse);
      else
        m_backBtn.updateHover(mouse);

      if (m_showHistory)
      {
        m_pageHistory.updateHover(mouse);
        return;
      }

      m_tabPgnFen.updateHover(mouse);
      m_tabBuild.updateHover(mouse);

      if (m_mode == game_setup::Mode::PgnFen)
        m_pagePgnFen.updateHover(mouse);
      else
        m_pageBuilder.updateHover(mouse);
    }

    void drawOverlay(sf::RenderWindow &win) const override
    {
      sf::RectangleShape dim({float(m_ws.x), float(m_ws.y)});
      dim.setPosition(0.f, 0.f);
      dim.setFillColor(sf::Color(0, 0, 0, 150));
      win.draw(dim);
    }

    void drawPanel(sf::RenderWindow &win) const override
    {
      ui::drawPanelShadow(win, m_rect);

      sf::RectangleShape panel({m_rect.width, m_rect.height});
      panel.setPosition(ui::snap({m_rect.left, m_rect.top}));
      panel.setFillColor(m_theme.panel);
      panel.setOutlineThickness(1.f);
      panel.setOutlineColor(m_theme.panelBorder);
      win.draw(panel);

      win.draw(m_title);

      if (!m_showHistory)
        m_historyBtn.draw(win);
      else
        m_backBtn.draw(win);

      m_close.draw(win);
      m_continue.draw(win);

      // Tabs always visible (unless history)
      if (!m_showHistory)
      {
        m_tabPgnFen.setActive(m_mode == game_setup::Mode::PgnFen);
        m_tabBuild.setActive(m_mode == game_setup::Mode::Builder);
        m_tabPgnFen.draw(win);
        m_tabBuild.draw(win);

        // Content
        if (m_mode == game_setup::Mode::PgnFen)
          m_pagePgnFen.draw(win);
        else
          m_pageBuilder.draw(win);

        // Footer clarity: always show what will be used
        sf::Text info(("Will use: " + resolvedSourceLabel()), m_font, 12);
        info.setFillColor(m_theme.subtle);
        info.setPosition(ui::snap({m_pages.left, m_continue.bounds().top + 8.f}));
        win.draw(info);
      }
      else
      {
        m_pageHistory.draw(win);
      }
    }

    bool handleEvent(const sf::Event &e, sf::Vector2f mouse) override
    {
      m_mouse = mouse;

      if (e.type == sf::Event::KeyPressed && e.key.code == sf::Keyboard::Escape && closeOnEsc())
      {
        requestDismiss();
        return true;
      }

      // Modal-level hotkeys:
      if (e.type == sf::Event::KeyPressed)
      {
        const bool ctrl = (e.key.control || e.key.system);

        // Ctrl+Enter = Use Position
        if (ctrl && e.key.code == sf::Keyboard::Enter)
        {
          m_resultFen = resolvedFen();
          requestDismiss();
          return true;
        }

        // Ctrl+V fallback routing even in Builder tab (if no field is focused and page didn’t handle it)
        if (ctrl && e.key.code == sf::Keyboard::V)
        {
          if (!m_focus.focused() && !m_showHistory)
          {
            // If we are in Builder mode, route paste into PGN/FEN page and switch.
            // This ensures “paste position via keyboard” always works.
            if (m_mode == game_setup::Mode::Builder)
            {
              m_mode = game_setup::Mode::PgnFen;
              m_pagePgnFen.paste_auto_from_clipboard();
              return true;
            }
          }
        }
      }

      if (e.type == sf::Event::MouseButtonPressed && e.mouseButton.button == sf::Mouse::Left)
      {
        if (!m_rect.contains(mouse))
        {
          requestDismiss();
          return true;
        }
      }

      if (m_close.handleEvent(e, mouse))
        return true;

      if (m_continue.handleEvent(e, mouse))
        return true;

      if (!m_showHistory)
      {
        if (m_historyBtn.handleEvent(e, mouse))
          return true;
      }
      else
      {
        if (m_backBtn.handleEvent(e, mouse))
          return true;
      }

      if (m_showHistory)
        return m_pageHistory.handleEvent(e, mouse);

      // Tabs
      if (m_tabPgnFen.handleEvent(e, mouse))
        return true;
      if (m_tabBuild.handleEvent(e, mouse))
        return true;

      // Content
      if (m_mode == game_setup::Mode::PgnFen)
      {
        if (m_pagePgnFen.handleEvent(e, mouse))
          return true;
      }
      else
      {
        // If builder page didn’t consume Ctrl+V, we still route it (handled above).
        if (m_pageBuilder.handleEvent(e, mouse))
          return true;
      }

      return false;
    }

  private:
    const sf::Font &m_font;
    const ui::Theme &m_theme;
    ui::FocusManager &m_focus;

    sf::Vector2u m_ws{};
    sf::Vector2f m_mouse{};

    sf::FloatRect m_rect{};
    sf::FloatRect m_inner{};
    sf::FloatRect m_pages{};
    sf::FloatRect m_contentRect{};

    sf::Text m_title{};

    ui::Button m_close{};
    ui::Button m_continue{};
    ui::Button m_historyBtn{};
    ui::Button m_backBtn{};

    mutable ui::Button m_tabPgnFen{};
    mutable ui::Button m_tabBuild{};

    bool m_showHistory{false};
    game_setup::Mode m_mode{game_setup::Mode::PgnFen};

    game_setup::PagePgnFen m_pagePgnFen;
    game_setup::PageBuilder m_pageBuilder;
    game_setup::PageHistory m_pageHistory;

    std::function<void()> m_onRequestPgnUpload;
    std::optional<std::string> m_resultFen{};

    void setup_action(ui::Button &b, const char *txt, std::function<void()> cb)
    {
      b.setTheme(&m_theme);
      b.setFont(m_font);
      b.setText(txt, 14);
      b.setOnClick(std::move(cb));
    }

    std::string resolvedFen() const
    {
      if (m_mode == game_setup::Mode::Builder)
        return m_pageBuilder.resolvedFen();
      return m_pagePgnFen.resolvedFen();
    }

    std::string resolvedSourceLabel() const
    {
      if (m_mode == game_setup::Mode::Builder)
        return "Builder";
      return m_pagePgnFen.actual_source_label();
    }
  };

} // namespace lilia::view::ui
