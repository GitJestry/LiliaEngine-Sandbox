#pragma once

#include <SFML/Graphics.hpp>
#include <SFML/Window/Clipboard.hpp>

#include <algorithm>
#include <functional>
#include <optional>
#include <string>

#include "lilia/constants.hpp"
#include "lilia/view/ui/interaction/focus.hpp"
#include "lilia/view/ui/render/layout.hpp"
#include "lilia/view/ui/widgets/button.hpp"
#include "lilia/view/ui/widgets/text_area.hpp"
#include "lilia/view/ui/widgets/text_field.hpp"

#include "game_setup_types.hpp"
#include "game_setup_validation.hpp"
#include "game_setup_draw.hpp"

namespace lilia::view::ui::game_setup
{
  class PagePgnFen final
  {
  public:
    PagePgnFen(const sf::Font &font, const ui::Theme &theme, ui::FocusManager &focus)
        : m_font(font), m_theme(theme), m_focus(focus)
    {
      // Inputs
      m_fenField.setTheme(&m_theme);
      m_fenField.setFont(m_font);
      m_fenField.setFocusManager(&m_focus);
      m_fenField.setCharacterSize(14);
      m_fenField.setPlaceholder("e.g. rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
      m_fenField.setText(core::START_FEN);

      m_pgnField.setTheme(&m_theme);
      m_pgnField.setFont(m_font);
      m_pgnField.setFocusManager(&m_focus);
      m_pgnField.setCharacterSize(14);
      m_pgnField.setPlaceholder("Paste PGN here… (optional [FEN \"…\"])");
      m_pgnField.setText("");

      // Buttons
      setup_action(m_pasteFen, "Paste", [this]
                   { paste_fen_from_clipboard(); });
      setup_action(m_resetFen, "Reset", [this]
                   { m_fenField.setText(core::START_FEN); });

      setup_action(m_uploadPgn, "Upload…", [this]
                   {
        if (m_onRequestPgnUpload)
          m_onRequestPgnUpload(); });
      setup_action(m_pastePgn, "Paste", [this]
                   { m_pgnField.setText(sf::Clipboard::getString().toAnsiString()); });
      setup_action(m_clearPgn, "Clear", [this]
                   { m_pgnField.setText(""); });

      setup_chip(m_srcAuto, "Auto", Source::Auto);
      setup_chip(m_srcFen, "FEN", Source::Fen);
      setup_chip(m_srcPgn, "PGN", Source::Pgn);

      // Resolved
      m_resolvedFen.setTheme(&m_theme);
      m_resolvedFen.setFont(m_font);
      m_resolvedFen.setCharacterSize(14);
      m_resolvedFen.setReadOnly(true);
      m_resolvedFen.setPlaceholder("No resolved position");

      setup_action(m_copyResolved, "Copy", [this]
                   { sf::Clipboard::setString(m_resolvedFen.text()); });

      revalidate(true);
      refresh_resolved_field();
    }

    void setOnRequestPgnUpload(std::function<void()> cb) { m_onRequestPgnUpload = std::move(cb); }
    void setPgnFilename(const std::string &name) { m_pgnFilename = name; }

    void setFenText(const std::string &fen)
    {
      std::string s = fen;
      strip_crlf(s);
      m_fenField.setText(s);
    }

    void setPgnText(const std::string &pgn) { m_pgnField.setText(pgn); }

    void setSource(Source s) { m_source = s; }
    Source source() const { return m_source; }

    void layout(const sf::FloatRect &bounds)
    {
      m_bounds = bounds;

      // Layout: three clear cards: FEN, PGN, Resolved.
      const float gap = 12.f;

      sf::FloatRect r = bounds;

      // Reserve bottom "Resolved" card fixed height
      const float resolvedCardH = 86.f;
      sf::FloatRect resolvedCard = {r.left, r.top + r.height - resolvedCardH, r.width, resolvedCardH};
      r.height -= (resolvedCardH + gap);

      // Top "FEN" card fixed height
      const float fenCardH = 114.f;
      sf::FloatRect fenCard = {r.left, r.top, r.width, fenCardH};
      r.top += (fenCardH + gap);
      r.height -= (fenCardH + gap);

      // Remaining is PGN card
      sf::FloatRect pgnCard = r;

      m_fenCard = fenCard;
      m_pgnCard = pgnCard;
      m_resolvedCard = resolvedCard;

      // --- FEN card internal ---
      {
        sf::FloatRect inner = ui::inset(m_fenCard, 12.f);

        // Header line: label left + status pill right
        m_fenHeader = ui::rowConsume(inner, 18.f, 8.f);

        // Field row: field + buttons on the right (no overlap)
        sf::FloatRect row = ui::rowConsume(inner, 36.f, 8.f);
        const float btnW = 84.f;
        const float btnH = 30.f;
        const float btnGap = 8.f;
        const float btnGroupW = btnW * 2.f + btnGap;

        sf::FloatRect field = row;
        field.width = std::max(220.f, row.width - btnGroupW);

        sf::FloatRect buttons = row;
        buttons.left = field.left + field.width + (row.width - field.width - btnGroupW);
        buttons.width = btnGroupW;

        m_fenField.setBounds(field);
        m_pasteFen.setBounds({buttons.left, row.top + 3.f, btnW, btnH});
        m_resetFen.setBounds({buttons.left + btnW + btnGap, row.top + 3.f, btnW, btnH});

        // Status line
        m_fenStatusLine = ui::rowConsume(inner, 18.f, 0.f);
      }

      // --- PGN card internal ---
      {
        sf::FloatRect inner = ui::inset(m_pgnCard, 12.f);

        // Header line: label + actions on right (Upload/Paste/Clear)
        m_pgnHeader = ui::rowConsume(inner, 18.f, 8.f);

        const float btnW = 92.f;
        const float btnH = 28.f;
        const float btnGap = 8.f;
        const float groupW = btnW * 3.f + btnGap * 2.f;

        // place buttons in header row right
        m_uploadPgn.setBounds({m_pgnHeader.left + m_pgnHeader.width - groupW, m_pgnHeader.top - 2.f, btnW, btnH});
        m_pastePgn.setBounds({m_pgnHeader.left + m_pgnHeader.width - (btnW * 2.f + btnGap + btnW + btnGap), m_pgnHeader.top - 2.f, btnW, btnH});
        m_clearPgn.setBounds({m_pgnHeader.left + m_pgnHeader.width - (btnW + 0.f), m_pgnHeader.top - 2.f, btnW, btnH});

        // Text area
        const float statusH = 18.f;
        const float pgnAreaH = std::max(160.f, inner.height - statusH - 10.f);
        m_pgnField.setBounds({inner.left, inner.top, inner.width, pgnAreaH});

        // PGN status line
        m_pgnStatusLine = {inner.left, inner.top + pgnAreaH + 10.f, inner.width, statusH};
      }

      // --- Resolved card internal ---
      {
        sf::FloatRect inner = ui::inset(m_resolvedCard, 12.f);

        m_resolvedHeader = ui::rowConsume(inner, 18.f, 10.f);

        // Source chips + resolved field + copy
        const float chipW = 66.f;
        const float chipH = 28.f;
        const float chipGap = 6.f;
        const float chipsW = chipW * 3.f + chipGap * 2.f;

        const float copyW = 84.f;
        const float rowH = 32.f;

        sf::FloatRect row = ui::rowConsume(inner, rowH, 0.f);

        sf::FloatRect chips = row;
        chips.width = std::min(chipsW, row.width * 0.45f);

        m_srcAuto.setBounds({chips.left, row.top + 2.f, chipW, chipH});
        m_srcFen.setBounds({chips.left + chipW + chipGap, row.top + 2.f, chipW, chipH});
        m_srcPgn.setBounds({chips.left + (chipW + chipGap) * 2.f, row.top + 2.f, chipW, chipH});

        sf::FloatRect field = row;
        field.left = chips.left + chips.width + 10.f;
        field.width = std::max(200.f, row.left + row.width - field.left - copyW - 8.f);

        m_resolvedFen.setBounds({field.left, row.top, field.width, 32.f});
        m_copyResolved.setBounds({field.left + field.width + 8.f, row.top, copyW, 32.f});
      }
    }

    void update()
    {
      revalidate(false);
      refresh_resolved_field();
    }

    void updateHover(sf::Vector2f mouse)
    {
      m_fenField.updateHover(mouse);
      m_pgnField.updateHover(mouse);

      m_pasteFen.updateHover(mouse);
      m_resetFen.updateHover(mouse);

      m_uploadPgn.updateHover(mouse);
      m_pastePgn.updateHover(mouse);
      m_clearPgn.updateHover(mouse);

      m_srcAuto.updateHover(mouse);
      m_srcFen.updateHover(mouse);
      m_srcPgn.updateHover(mouse);

      m_resolvedFen.updateHover(mouse);
      m_copyResolved.updateHover(mouse);
    }

    bool handleEvent(const sf::Event &e, sf::Vector2f mouse)
    {
      // Hotkeys (when page is active)
      if (e.type == sf::Event::KeyPressed)
      {
        const bool ctrl = (e.key.control || e.key.system);
        if (ctrl && e.key.code == sf::Keyboard::O)
        {
          if (m_onRequestPgnUpload)
          {
            m_onRequestPgnUpload();
            return true;
          }
        }

        // If nothing is focused, Ctrl+V auto-pastes into the right box.
        if (ctrl && e.key.code == sf::Keyboard::V)
        {
          if (!m_focus.focused())
          {
            return paste_auto_from_clipboard();
          }
        }
      }

      // Buttons/chips first
      if (m_pasteFen.handleEvent(e, mouse))
        return true;
      if (m_resetFen.handleEvent(e, mouse))
        return true;

      if (m_uploadPgn.handleEvent(e, mouse))
        return true;
      if (m_pastePgn.handleEvent(e, mouse))
        return true;
      if (m_clearPgn.handleEvent(e, mouse))
        return true;

      if (m_srcAuto.handleEvent(e, mouse))
        return true;
      if (m_srcFen.handleEvent(e, mouse))
        return true;
      if (m_srcPgn.handleEvent(e, mouse))
        return true;

      if (m_copyResolved.handleEvent(e, mouse))
        return true;

      if (m_fenField.handleEvent(e, mouse))
        return true;
      if (m_pgnField.handleEvent(e, mouse))
        return true;

      // Read-only but still focusable
      if (m_resolvedFen.handleEvent(e, mouse))
        return true;

      return false;
    }

    void draw(sf::RenderTarget &rt) const
    {
      // Cards
      draw_section_card(rt, m_theme, m_fenCard);
      draw_section_card(rt, m_theme, m_pgnCard);
      draw_section_card(rt, m_theme, m_resolvedCard);

      // --- FEN ---
      draw_label(rt, m_font, m_theme, m_fenHeader.left, m_fenHeader.top, "FEN");

      // FEN status pill right
      {
        int k = 0;
        std::string txt = "Empty";
        if (!trim_copy(m_fenField.text()).empty())
        {
          if (m_fenValid)
          {
            k = 1;
            txt = "Valid";
          }
          else
          {
            k = 3;
            txt = "Invalid";
          }
        }
        sf::FloatRect pill = {m_fenHeader.left + m_fenHeader.width - 108.f, m_fenHeader.top - 2.f, 108.f, 16.f};
        draw_status_pill(rt, m_font, m_theme, pill, txt, k);
      }

      m_fenField.draw(rt);
      m_pasteFen.draw(rt);
      m_resetFen.draw(rt);

      // FEN hint line
      {
        sf::Text hint("Tip: Ctrl+V pastes automatically when no field is focused.", m_font, 12);
        hint.setFillColor(m_theme.subtle);
        hint.setPosition(ui::snap({m_fenStatusLine.left, m_fenStatusLine.top}));
        rt.draw(hint);
      }

      // --- PGN ---
      draw_label(rt, m_font, m_theme, m_pgnHeader.left, m_pgnHeader.top, "PGN");
      m_uploadPgn.draw(rt);
      m_pastePgn.draw(rt);
      m_clearPgn.draw(rt);

      // Selected filename (clear, non-overlapping)
      if (!m_pgnFilename.empty())
      {
        sf::Text fn("Selected file: " + m_pgnFilename, m_font, 12);
        fn.setFillColor(m_theme.subtle);
        fn.setPosition(ui::snap({m_pgnHeader.left + 52.f, m_pgnHeader.top}));
        rt.draw(fn);
      }

      m_pgnField.draw(rt);

      // PGN status pill
      {
        int k = 0;
        std::string txt = "Empty";
        if (!trim_copy(m_pgnField.text()).empty())
        {
          if (m_pgnStatus.kind == PgnStatus::Kind::OkFen)
          {
            k = 1;
            txt = "Has [FEN]";
          }
          else if (m_pgnStatus.kind == PgnStatus::Kind::OkNoFen)
          {
            k = 2;
            txt = "Moves only";
          }
          else
          {
            k = 3;
            txt = "Invalid";
          }
        }
        sf::FloatRect pill = {m_pgnStatusLine.left, m_pgnStatusLine.top, 140.f, m_pgnStatusLine.height};
        draw_status_pill(rt, m_font, m_theme, pill, txt, k);
      }

      // --- Resolved ---
      draw_label(rt, m_font, m_theme, m_resolvedHeader.left, m_resolvedHeader.top, "Position to use");

      // Chips active states
      m_srcAuto.setActive(m_source == Source::Auto);
      m_srcFen.setActive(m_source == Source::Fen);
      m_srcPgn.setActive(m_source == Source::Pgn);

      m_srcAuto.draw(rt);
      m_srcFen.draw(rt);
      m_srcPgn.draw(rt);

      // Label: requested vs actual (Auto -> actual)
      {
        const std::string actual = actual_source_label();
        sf::Text s("Using: " + actual, m_font, 12);
        s.setFillColor(m_theme.subtle);
        s.setPosition(ui::snap({m_resolvedHeader.left + 140.f, m_resolvedHeader.top}));
        rt.draw(s);
      }

      m_resolvedFen.draw(rt);
      m_copyResolved.draw(rt);
    }

    std::string resolvedFen() const { return compute_resolved_fen(); }

    // True if resolved FEN passes basic validation (or is startpos).
    bool resolvedFenOk() const
    {
      const std::string f = compute_resolved_fen();
      return !validate_fen_basic(f).has_value();
    }

    // External access for orchestrator: used for footer clarity.
    std::string actual_source_label() const
    {
      const std::string fenNorm = normalize_fen(m_fenField.text());
      const bool fenOk = !trim_copy(fenNorm).empty() && !validate_fen_basic(fenNorm).has_value();
      const bool pgnHasFen = m_pgnStatus.fenFromTag.has_value();

      if (m_source == Source::Fen)
        return fenOk ? "FEN" : "FEN → Start position";
      if (m_source == Source::Pgn)
        return pgnHasFen ? "PGN [FEN]" : "PGN → Start position";

      // Auto
      if (pgnHasFen)
        return "Auto → PGN [FEN]";
      if (fenOk)
        return "Auto → FEN";
      return "Auto → Start position";
    }

    // Called by modal when paste is triggered on Builder tab.
    bool paste_auto_from_clipboard()
    {
      const std::string clip = sf::Clipboard::getString().toAnsiString();
      if (looks_like_fen(clip))
      {
        setFenText(clip);
        m_source = Source::Fen; // “Auto-detect paste” is most predictable as explicit FEN.
        return true;
      }
      if (looks_like_pgn(clip))
      {
        setPgnText(clip);
        m_source = Source::Pgn;
        return true;
      }

      // fallback: if it has slashes, treat as fen; otherwise pgn
      if (clip.find('/') != std::string::npos)
      {
        setFenText(clip);
        m_source = Source::Fen;
      }
      else
      {
        setPgnText(clip);
        m_source = Source::Pgn;
      }
      return true;
    }

  private:
    const sf::Font &m_font;
    const ui::Theme &m_theme;
    ui::FocusManager &m_focus;

    sf::FloatRect m_bounds{};
    sf::FloatRect m_fenCard{};
    sf::FloatRect m_pgnCard{};
    sf::FloatRect m_resolvedCard{};

    sf::FloatRect m_fenHeader{};
    sf::FloatRect m_pgnHeader{};
    sf::FloatRect m_resolvedHeader{};

    sf::FloatRect m_fenStatusLine{};
    sf::FloatRect m_pgnStatusLine{};

    ui::TextField m_fenField{};
    ui::TextArea m_pgnField{};

    ui::Button m_pasteFen{};
    ui::Button m_resetFen{};

    ui::Button m_uploadPgn{};
    ui::Button m_pastePgn{};
    ui::Button m_clearPgn{};

    Source m_source{Source::Auto};
    mutable ui::Button m_srcAuto{};
    mutable ui::Button m_srcFen{};
    mutable ui::Button m_srcPgn{};

    ui::TextField m_resolvedFen{};
    ui::Button m_copyResolved{};

    std::function<void()> m_onRequestPgnUpload;
    std::string m_pgnFilename;

    // Validation cache/state
    std::string m_lastFenRaw;
    std::string m_lastPgnRaw;
    bool m_fenValid{true};
    PgnStatus m_pgnStatus{};

    void setup_action(ui::Button &b, const char *txt, std::function<void()> cb)
    {
      b.setTheme(&m_theme);
      b.setFont(m_font);
      b.setText(txt, 13);
      b.setOnClick(std::move(cb));
    }

    void setup_chip(ui::Button &b, const char *txt, Source s)
    {
      b.setTheme(&m_theme);
      b.setFont(m_font);
      b.setText(txt, 13);
      b.setOnClick([this, s]
                   { m_source = s; });
    }

    void paste_fen_from_clipboard()
    {
      std::string s = sf::Clipboard::getString().toAnsiString();
      strip_crlf(s);
      m_fenField.setText(s);
    }

    void revalidate(bool force)
    {
      const std::string fenNowRaw = m_fenField.text();
      const std::string pgnNowRaw = m_pgnField.text();

      if (!force && fenNowRaw == m_lastFenRaw && pgnNowRaw == m_lastPgnRaw)
        return;

      m_lastFenRaw = fenNowRaw;
      m_lastPgnRaw = pgnNowRaw;

      const std::string fenNorm = normalize_fen(fenNowRaw);
      m_fenValid = !trim_copy(fenNorm).empty() && !validate_fen_basic(fenNorm).has_value();

      m_pgnStatus = validate_pgn_basic(pgnNowRaw);
    }

    std::string compute_resolved_fen() const
    {
      const std::string fenNorm = normalize_fen(m_fenField.text());
      const bool fenOk = !trim_copy(fenNorm).empty() && !validate_fen_basic(fenNorm).has_value();
      const bool pgnHasFen = m_pgnStatus.fenFromTag.has_value();

      switch (m_source)
      {
      case Source::Fen:
        return fenOk ? fenNorm : core::START_FEN;
      case Source::Pgn:
        return pgnHasFen ? *m_pgnStatus.fenFromTag : core::START_FEN;
      case Source::Auto:
      default:
        // prefer explicit [FEN] tag, else valid FEN field, else startpos
        if (pgnHasFen)
          return *m_pgnStatus.fenFromTag;
        if (fenOk)
          return fenNorm;
        return core::START_FEN;
      }
    }

    void refresh_resolved_field()
    {
      const std::string rf = compute_resolved_fen();
      if (rf != m_resolvedFen.text())
        m_resolvedFen.setText(rf);
    }
  };

} // namespace lilia::view::ui::game_setup
