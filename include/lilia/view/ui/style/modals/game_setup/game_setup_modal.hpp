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
#include "game_setup_draw.hpp"
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

      // Header: History / Back (ASCII-only labels)
      setup_action(m_historyBtn, "History", [this]
                   { m_showHistory = true; });
      setup_action(m_backBtn, "Back", [this]
                   { m_showHistory = false; });

      setup_action(m_close, "Close", [this]
                   { requestDismiss(); });

      setup_action(m_continue, "Use Position", [this]
                   {
        const std::string rf = resolvedFen();
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

      m_pagePgnFen.setOnRequestPgnUpload([this]
                                         {
                                           if (m_onRequestPgnUpload)
                                             m_onRequestPgnUpload(); });
    }

    void setOnRequestPgnUpload(std::function<void()> cb) { m_onRequestPgnUpload = std::move(cb); }

    void setFenText(const std::string &fen) { m_pagePgnFen.setFenText(fen); }
    void setPgnText(const std::string &pgn) { m_pagePgnFen.setPgnText(pgn); }
    void setPgnFilename(const std::string &name) { m_pagePgnFen.setPgnFilename(name); }

    std::optional<std::string> resultFen() const { return m_resultFen; }

    void layout(sf::Vector2u ws) override
    {
      m_ws = ws;

      // Slightly larger + more breathing room for footer
      m_rect = ui::anchoredCenter(ws, {940.f, 680.f});
      m_inner = ui::inset(m_rect, 18.f);

      // Header
      sf::FloatRect header = ui::rowConsume(m_inner, 44.f, 12.f);
      m_title.setPosition(ui::snap({header.left, header.top + 9.f}));

      const float hBtnH = 30.f;
      const float closeW = 92.f;
      const float navW = 120.f;

      m_close.setBounds({header.left + header.width - closeW, header.top + 7.f, closeW, hBtnH});

      const float navX = header.left + header.width - closeW - 10.f - navW;
      m_historyBtn.setBounds({navX, header.top + 7.f, navW, hBtnH});
      m_backBtn.setBounds({navX, header.top + 7.f, navW, hBtnH});

      // Footer (taller, clearer)
      sf::FloatRect footer = {m_rect.left + 18.f, m_rect.top + m_rect.height - 66.f,
                              m_rect.width - 36.f, 48.f};

      // Using pill (left)
      m_usingPill = {footer.left, footer.top + 10.f, 260.f, 28.f};

      // Primary action (right) with extra margin
      const float useW = 240.f;
      m_continue.setBounds({footer.left + footer.width - useW, footer.top + 6.f, useW, 36.f});

      // Content
      m_pages = {m_rect.left + 18.f, header.top + header.height + 12.f, m_rect.width - 36.f,
                 m_rect.height - 18.f - (header.height + 12.f) - 66.f};

      sf::FloatRect content = m_pages;
      sf::FloatRect tabs = ui::rowConsume(content, 32.f, 12.f);

      sf::FloatRect t = tabs;
      auto a = ui::colConsume(t, 120.f, 8.f);
      auto b = ui::colConsume(t, 92.f, 8.f);

      m_tabPgnFen.setBounds(a);
      m_tabBuild.setBounds(b);

      m_contentRect = content;

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

      // Tabs
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

        // Footer: Using pill + primary CTA
        drawUsingFooter(win);

        // Make CTA stand out: subtle accent glow behind it
        const auto b = m_continue.bounds();
        sf::RectangleShape glow({b.width + 10.f, b.height + 10.f});
        glow.setPosition(ui::snap({b.left - 5.f, b.top - 5.f}));
        glow.setFillColor(game_setup::withA(m_theme.accent, 40));
        win.draw(glow);

        m_continue.draw(win);
        m_close.draw(win);
      }
      else
      {
        m_pageHistory.draw(win);
        m_close.draw(win);
      }
    }

    bool handleEvent(const sf::Event &e, sf::Vector2f mouse) override
    {
      m_mouse = mouse;

      // Do NOT close on Esc (requested). Consume to avoid underlying UI actions.
      if (e.type == sf::Event::KeyPressed && e.key.code == sf::Keyboard::Escape)
        return true;

      // Modal-level hotkeys
      if (e.type == sf::Event::KeyPressed)
      {
        const bool ctrl = (e.key.control || e.key.system);

        // Ctrl+Enter = Use Position
        if (ctrl && e.key.code == sf::Keyboard::Enter)
        {
          const std::string rf = resolvedFen();
          if (rf.empty())
            return true;
          m_resultFen = rf;
          requestDismiss();
          return true;
        }

        // Ctrl+V routing (when nothing focused)
        if (ctrl && e.key.code == sf::Keyboard::V)
        {
          if (!m_focus.focused() && !m_showHistory)
          {
            if (m_mode == game_setup::Mode::Builder)
            {
              m_mode = game_setup::Mode::PgnFen;
              m_pagePgnFen.paste_auto_from_clipboard();
              return true;
            }
          }
        }
      }

      // Do NOT dismiss on outside click (requested).
      // Still consume the click so it doesn't leak to underlying UI.
      if (e.type == sf::Event::MouseButtonPressed && e.mouseButton.button == sf::Mouse::Left)
      {
        if (!m_rect.contains(mouse))
          return true;
      }

      if (m_close.handleEvent(e, mouse))
        return true;

      if (!m_showHistory)
      {
        if (m_continue.handleEvent(e, mouse))
          return true;

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

      if (m_tabPgnFen.handleEvent(e, mouse))
        return true;
      if (m_tabBuild.handleEvent(e, mouse))
        return true;

      if (m_mode == game_setup::Mode::PgnFen)
      {
        if (m_pagePgnFen.handleEvent(e, mouse))
          return true;
      }
      else
      {
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
    sf::FloatRect m_usingPill{};

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

    std::string usingLabel() const
    {
      if (m_mode == game_setup::Mode::Builder)
        return "Builder";
      return m_pagePgnFen.actual_source_label();
    }

    bool usingOk() const
    {
      if (m_mode == game_setup::Mode::Builder)
        return !m_pageBuilder.resolvedFen().empty();
      return m_pagePgnFen.using_custom_position();
    }

    void drawUsingFooter(sf::RenderTarget &rt) const
    {
      const bool ok = usingOk();
      const int kind = ok ? 1 : 3;

      const std::string txt = std::string("Using: ") + usingLabel();
      game_setup::draw_status_pill(rt, m_font, m_theme, m_usingPill, txt, kind);
    }
  };

} // namespace lilia::view::ui
