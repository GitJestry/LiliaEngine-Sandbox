#include "lilia/view/ui/style/modals/game_setup/game_setup_page_pgn_fen.hpp"

#include <SFML/Window/Clipboard.hpp>

namespace lilia::view::ui::game_setup
{
  PagePgnFen::PagePgnFen(const sf::Font &font, const ui::Theme &theme, ui::FocusManager &focus)
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
    m_pgnField.setPlaceholder("Paste PGN here... (optional [FEN \"...\"])");
    m_pgnField.setText("");

    // Buttons
    setup_action(m_pasteFen, "Paste", [this]
                 { paste_fen_from_clipboard(); });
    setup_action(m_resetFen, "Reset", [this]
                 { m_fenField.setText(core::START_FEN); });

    setup_action(m_uploadPgn, "Upload...", [this]
                 {
      if (m_onRequestPgnUpload) m_onRequestPgnUpload(); });

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

  void PagePgnFen::setOnRequestPgnUpload(std::function<void()> cb) { m_onRequestPgnUpload = std::move(cb); }
  void PagePgnFen::setPgnFilename(const std::string &name) { m_pgnFilename = name; }

  void PagePgnFen::setFenText(const std::string &fen)
  {
    std::string s = fen;
    strip_crlf(s);
    m_fenField.setText(s);
  }

  void PagePgnFen::setPgnText(const std::string &pgn) { m_pgnField.setText(pgn); }

  void PagePgnFen::setSource(Source s) { m_source = s; }
  Source PagePgnFen::source() const { return m_source; }

  void PagePgnFen::layout(const sf::FloatRect &bounds)
  {
    m_bounds = bounds;

    const float gap = 12.f;
    sf::FloatRect r = bounds;

    // Bottom resolved card
    const float resolvedCardH = 92.f;
    sf::FloatRect resolvedCard = {r.left, r.top + r.height - resolvedCardH, r.width, resolvedCardH};
    r.height -= (resolvedCardH + gap);

    // Top fen card
    const float fenCardH = 114.f;
    sf::FloatRect fenCard = {r.left, r.top, r.width, fenCardH};
    r.top += (fenCardH + gap);
    r.height -= (fenCardH + gap);

    // Remaining pgn
    sf::FloatRect pgnCard = r;

    m_fenCard = fenCard;
    m_pgnCard = pgnCard;
    m_resolvedCard = resolvedCard;

    // --- FEN card ---
    {
      sf::FloatRect inner = ui::inset(m_fenCard, 12.f);

      m_fenHeader = ui::rowConsume(inner, 18.f, 8.f);

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

      m_fenStatusLine = ui::rowConsume(inner, 18.f, 0.f);
    }

    // --- PGN card ---
    {
      sf::FloatRect inner = ui::inset(m_pgnCard, 12.f);

      m_pgnHeader = ui::rowConsume(inner, 18.f, 8.f);

      const float btnW = 92.f;
      const float btnH = 28.f;
      const float btnGap = 8.f;
      const float groupW = btnW * 3.f + btnGap * 2.f;

      const float bx = m_pgnHeader.left + m_pgnHeader.width - groupW;
      m_uploadPgn.setBounds({bx, m_pgnHeader.top - 2.f, btnW, btnH});
      m_pastePgn.setBounds({bx + (btnW + btnGap), m_pgnHeader.top - 2.f, btnW, btnH});
      m_clearPgn.setBounds({bx + (btnW + btnGap) * 2.f, m_pgnHeader.top - 2.f, btnW, btnH});

      const float statusH = 18.f;
      const float pgnAreaH = std::max(160.f, inner.height - statusH - 10.f);
      m_pgnField.setBounds({inner.left, inner.top, inner.width, pgnAreaH});

      m_pgnStatusLine = {inner.left, inner.top + pgnAreaH + 10.f, inner.width, statusH};
    }

    // --- Resolved card ---
    {
      sf::FloatRect inner = ui::inset(m_resolvedCard, 12.f);

      m_resolvedHeader = ui::rowConsume(inner, 18.f, 10.f);

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

  void PagePgnFen::update()
  {
    revalidate(false);
    refresh_resolved_field();
  }

  void PagePgnFen::updateHover(sf::Vector2f mouse)
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

  bool PagePgnFen::handleEvent(const sf::Event &e, sf::Vector2f mouse)
  {
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

      if (ctrl && e.key.code == sf::Keyboard::V)
      {
        if (!m_focus.focused())
          return paste_auto_from_clipboard();
      }
    }

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
    if (m_resolvedFen.handleEvent(e, mouse))
      return true;

    return false;
  }

  void PagePgnFen::draw(sf::RenderTarget &rt) const
  {
    draw_section_card(rt, m_theme, m_fenCard);
    draw_section_card(rt, m_theme, m_pgnCard);
    draw_section_card(rt, m_theme, m_resolvedCard);

    // --- FEN ---
    draw_label(rt, m_font, m_theme, m_fenHeader.left, m_fenHeader.top, "FEN");

    {
      const std::string raw = trim_copy(m_fenField.text());
      const bool empty = raw.empty();
      const bool ok = m_fenOk;

      const int kind = empty ? 0 : (ok ? 1 : 3);
      const std::string txt = empty ? "Empty" : (ok ? "Valid" : "Invalid");

      sf::FloatRect pill = {m_fenHeader.left + m_fenHeader.width - 108.f, m_fenHeader.top - 2.f, 108.f, 18.f};
      draw_status_pill(rt, m_font, m_theme, pill, txt, kind);
    }

    m_fenField.draw(rt);
    m_pasteFen.draw(rt);
    m_resetFen.draw(rt);

    // --- PGN ---
    draw_label(rt, m_font, m_theme, m_pgnHeader.left, m_pgnHeader.top, "PGN");
    m_uploadPgn.draw(rt);
    m_pastePgn.draw(rt);
    m_clearPgn.draw(rt);

    if (!m_pgnFilename.empty())
    {
      sf::Text fn("File: " + m_pgnFilename, m_font, 12);
      fn.setFillColor(m_theme.subtle);
      fn.setPosition(ui::snap({m_pgnHeader.left + 52.f, m_pgnHeader.top}));
      rt.draw(fn);
    }

    m_pgnField.draw(rt);

    {
      int kind = 0;
      std::string txt = "Empty";

      if (!trim_copy(m_pgnField.text()).empty())
      {
        if (m_pgnStatus.kind == PgnStatus::Kind::OkFen)
        {
          kind = 1;
          txt = "Has FEN";
        }
        else if (m_pgnStatus.kind == PgnStatus::Kind::OkNoFen)
        {
          kind = 1;
          txt = "Moves";
        }
        else
        {
          kind = 3;
          txt = "Invalid";
        }
      }

      sf::FloatRect pill = {m_pgnStatusLine.left, m_pgnStatusLine.top, 120.f, m_pgnStatusLine.height};
      draw_status_pill(rt, m_font, m_theme, pill, txt, kind);
    }

    // --- Resolved ---
    draw_label(rt, m_font, m_theme, m_resolvedHeader.left, m_resolvedHeader.top, "Position");

    {
      const auto path = resolved_path();
      const bool usingCustom = (path == ResolvedPath::Fen || path == ResolvedPath::Pgn);
      const int kind = usingCustom ? 1 : 3;

      const std::string txt = usingCustom ? "Ready" : "Start position";
      sf::FloatRect pill = {m_resolvedHeader.left + m_resolvedHeader.width - 140.f, m_resolvedHeader.top - 2.f, 140.f, 18.f};
      draw_status_pill(rt, m_font, m_theme, pill, txt, kind);
    }

    m_srcAuto.setActive(m_source == Source::Auto);
    m_srcFen.setActive(m_source == Source::Fen);
    m_srcPgn.setActive(m_source == Source::Pgn);

    m_srcAuto.draw(rt);
    m_srcFen.draw(rt);
    m_srcPgn.draw(rt);

    m_resolvedFen.draw(rt);
    m_copyResolved.draw(rt);
  }

  std::string PagePgnFen::resolvedFen() const { return compute_resolved_fen(); }

  bool PagePgnFen::resolvedFenOk() const
  {
    const std::string f = compute_resolved_fen();
    return !sanitize_fen_playable(f).empty();
  }

  std::string PagePgnFen::actual_source_label() const
  {
    const auto path = resolved_path();
    switch (path)
    {
    case ResolvedPath::Pgn:
      return "PGN";
    case ResolvedPath::Fen:
      return "FEN";
    case ResolvedPath::Start:
    default:
      return "Start position";
    }
  }

  bool PagePgnFen::using_custom_position() const
  {
    const auto path = resolved_path();
    return (path == ResolvedPath::Fen || path == ResolvedPath::Pgn);
  }

  bool PagePgnFen::paste_auto_from_clipboard()
  {
    const std::string clip = sf::Clipboard::getString().toAnsiString();

    if (looks_like_fen(clip))
    {
      setFenText(clip);
      m_source = Source::Fen;
      return true;
    }
    if (looks_like_pgn(clip))
    {
      setPgnText(clip);
      m_source = Source::Pgn;
      return true;
    }

    // Fallback heuristic
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

  void PagePgnFen::paste_auto_from_clipboard_into_fields()
  {
    (void)paste_auto_from_clipboard();
  }

  void PagePgnFen::setup_action(ui::Button &b, const char *txt, std::function<void()> cb)
  {
    b.setTheme(&m_theme);
    b.setFont(m_font);
    b.setText(txt, 13);
    b.setOnClick(std::move(cb));
  }

  void PagePgnFen::setup_chip(ui::Button &b, const char *txt, Source s)
  {
    b.setTheme(&m_theme);
    b.setFont(m_font);
    b.setText(txt, 13);
    b.setOnClick([this, s]
                 { m_source = s; });
  }

  void PagePgnFen::paste_fen_from_clipboard()
  {
    std::string s = sf::Clipboard::getString().toAnsiString();
    strip_crlf(s);
    m_fenField.setText(s);
  }

  void PagePgnFen::revalidate(bool force)
  {
    const std::string fenNowRaw = m_fenField.text();
    const std::string pgnNowRaw = m_pgnField.text();

    if (!force && fenNowRaw == m_lastFenRaw && pgnNowRaw == m_lastPgnRaw)
      return;

    m_lastFenRaw = fenNowRaw;
    m_lastPgnRaw = pgnNowRaw;

    m_fenSanitized = sanitize_fen_playable(fenNowRaw);
    m_fenOk = !m_fenSanitized.empty();

    m_pgnStatus = validate_pgn_basic(pgnNowRaw);
  }

  PagePgnFen::ResolvedPath PagePgnFen::resolved_path() const
  {
    const bool fenOk = m_fenOk;
    const bool pgnHasFen = m_pgnStatus.fenFromTag.has_value();

    switch (m_source)
    {
    case Source::Fen:
      return fenOk ? ResolvedPath::Fen : ResolvedPath::Start;
    case Source::Pgn:
      return pgnHasFen ? ResolvedPath::Pgn : ResolvedPath::Start;
    case Source::Auto:
    default:
      if (pgnHasFen)
        return ResolvedPath::Pgn;
      if (fenOk)
        return ResolvedPath::Fen;
      return ResolvedPath::Start;
    }
  }

  std::string PagePgnFen::compute_resolved_fen() const
  {
    const bool fenOk = m_fenOk;
    const bool pgnHasFen = m_pgnStatus.fenFromTag.has_value();

    switch (m_source)
    {
    case Source::Fen:
      return fenOk ? m_fenSanitized : core::START_FEN;

    case Source::Pgn:
      return pgnHasFen ? *m_pgnStatus.fenFromTag : core::START_FEN;

    case Source::Auto:
    default:
      if (pgnHasFen)
        return *m_pgnStatus.fenFromTag;
      if (fenOk)
        return m_fenSanitized;
      return core::START_FEN;
    }
  }

  void PagePgnFen::refresh_resolved_field()
  {
    const std::string rf = compute_resolved_fen();
    if (rf != m_resolvedFen.text())
      m_resolvedFen.setText(rf);
  }

} // namespace lilia::view::ui::game_setup
