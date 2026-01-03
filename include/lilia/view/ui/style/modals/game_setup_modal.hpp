#pragma once

#include <SFML/Graphics.hpp>
#include <SFML/Window/Clipboard.hpp>

#include <algorithm>
#include <cctype>
#include <functional>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "lilia/constants.hpp"
#include "lilia/view/ui/interaction/focus.hpp"
#include "lilia/view/ui/render/layout.hpp"
#include "modal.hpp"
#include "position_builder.hpp"
#include "../style.hpp"
#include "../theme.hpp"
#include "lilia/view/ui/widgets/button.hpp"
#include "lilia/view/ui/widgets/text_field.hpp"
#include "lilia/view/ui/widgets/text_area.hpp"

namespace lilia::view::ui
{

  class GameSetupModal final : public Modal
  {
  public:
    void setOnRequestPgnUpload(std::function<void()> cb) { m_onRequestPgnUpload = std::move(cb); }

    // Controller may call these after upload
    void setFenText(const std::string &fen) { m_fenField.setText(fen); }
    void setPgnText(const std::string &pgn) { m_pgnField.setText(pgn); }
    void setPgnFilename(const std::string &name) { m_pgnFilename = name; }

    GameSetupModal(const sf::Font &font, const ui::Theme &theme, ui::FocusManager &focus)
        : m_font(font), m_theme(theme), m_focus(focus)
    {
      // Title
      m_title.setFont(m_font);
      m_title.setCharacterSize(20);
      m_title.setFillColor(m_theme.text);
      m_title.setString("Load Game / Create Start Position");

      // Header: History / Back (same visual logic, no slide)
      m_historyBtn.setTheme(&m_theme);
      m_historyBtn.setFont(m_font);
      m_historyBtn.setText("History  →", 14);
      m_historyBtn.setOnClick([this]
                              { m_showHistory = true; });

      m_backBtn.setTheme(&m_theme);
      m_backBtn.setFont(m_font);
      m_backBtn.setText("←  Back", 14);
      m_backBtn.setOnClick([this]
                           { m_showHistory = false; });

      m_close.setTheme(&m_theme);
      m_close.setFont(m_font);
      m_close.setText("Close", 14);
      m_close.setOnClick([this]
                         { requestDismiss(); });

      // Footer
      m_continue.setTheme(&m_theme);
      m_continue.setFont(m_font);
      m_continue.setText("Use Position", 15);
      m_continue.setAccent(true);
      m_continue.setOnClick([this]
                            {
        m_resultFen = resolvedFen();
        requestDismiss(); });

      // Tabs (only 2)
      m_tabPgnFen.setTheme(&m_theme);
      m_tabPgnFen.setFont(m_font);
      m_tabPgnFen.setText("PGN / FEN", 14);
      m_tabPgnFen.setOnClick([this]
                             { m_mode = Mode::PgnFen; });

      m_tabBuild.setTheme(&m_theme);
      m_tabBuild.setFont(m_font);
      m_tabBuild.setText("Builder", 14);
      m_tabBuild.setOnClick([this]
                            { m_mode = Mode::Builder; });

      // Source chips (small, clear)
      auto setupChip = [&](ui::Button &b, const char *txt, Source s)
      {
        b.setTheme(&m_theme);
        b.setFont(m_font);
        b.setText(txt, 13);
        b.setOnClick([this, s]
                     { m_source = s; });
      };
      setupChip(m_srcAuto, "Auto", Source::Auto);
      setupChip(m_srcFen, "FEN", Source::Fen);
      setupChip(m_srcPgn, "PGN", Source::Pgn);

      // FEN
      m_fenField.setTheme(&m_theme);
      m_fenField.setFont(m_font);
      m_fenField.setFocusManager(&m_focus);
      m_fenField.setCharacterSize(15);
      m_fenField.setPlaceholder("Paste/type FEN…");
      m_fenField.setText(core::START_FEN);

      // PGN
      m_pgnField.setTheme(&m_theme);
      m_pgnField.setFont(m_font);
      m_pgnField.setFocusManager(&m_focus);
      m_pgnField.setCharacterSize(14);
      m_pgnField.setPlaceholder("Paste PGN here… (optional [FEN \"…\"])");
      m_pgnField.setText("");

      // Small action buttons (compact)
      setupActionBtn(m_pasteFen, "Paste", [this]
                     {
        std::string s = sf::Clipboard::getString().toAnsiString();
        s.erase(std::remove(s.begin(), s.end(), '\n'), s.end());
        s.erase(std::remove(s.begin(), s.end(), '\r'), s.end());
        m_fenField.setText(s); });
      setupActionBtn(m_resetFen, "Reset", [this]
                     { m_fenField.setText(core::START_FEN); });

      setupActionBtn(m_pastePgn, "Paste", [this]
                     { m_pgnField.setText(sf::Clipboard::getString().toAnsiString()); });
      setupActionBtn(m_clearPgn, "Clear", [this]
                     { m_pgnField.setText(""); });

      setupActionBtn(m_uploadPgn, "Upload…", [this]
                     {
        if (m_onRequestPgnUpload)
          m_onRequestPgnUpload(); });

      // Resolved FEN display + copy
      m_resolvedFen.setTheme(&m_theme);
      m_resolvedFen.setFont(m_font);
      m_resolvedFen.setFocusManager(&m_focus);
      m_resolvedFen.setCharacterSize(14);
      m_resolvedFen.setReadOnly(true);
      m_resolvedFen.setPlaceholder("");

      setupActionBtn(m_copyResolved, "Copy", [this]
                     { sf::Clipboard::setString(m_resolvedFen.text()); });

      // Builder
      m_builderFen.setTheme(&m_theme);
      m_builderFen.setFont(m_font);
      m_builderFen.setFocusManager(&m_focus);
      m_builderFen.setCharacterSize(14);
      m_builderFen.setReadOnly(true);

      setupActionBtn(m_copyFen, "Copy", [this]
                     { sf::Clipboard::setString(m_builderFen.text()); });

      m_builder.setTheme(&m_theme);
      m_builder.setFont(&m_font);
      m_builder.resetToStart();

      // Default view
      m_mode = Mode::PgnFen;
      m_showHistory = false;
      m_source = Source::Auto;

      // History copy (placeholder)
      m_historyTitle.setFont(m_font);
      m_historyTitle.setCharacterSize(18);
      m_historyTitle.setFillColor(m_theme.text);
      m_historyTitle.setString("History");

      revalidateAll(true);
    }

    std::optional<std::string> resultFen() const { return m_resultFen; }

    void layout(sf::Vector2u ws) override
    {
      m_ws = ws;

      // tighter + cleaner
      m_rect = ui::anchoredCenter(ws, {900.f, 640.f});
      m_inner = ui::inset(m_rect, 18.f);

      // header
      sf::FloatRect header = ui::rowConsume(m_inner, 44.f, 12.f);
      m_title.setPosition(ui::snap({header.left, header.top + 9.f}));

      const float hBtnH = 30.f;
      const float closeW = 92.f;
      const float navW = 140.f;

      m_close.setBounds({header.left + header.width - closeW, header.top + 7.f, closeW, hBtnH});

      const float navX = header.left + header.width - closeW - 10.f - navW;
      m_historyBtn.setBounds({navX, header.top + 7.f, navW, hBtnH});
      m_backBtn.setBounds({navX, header.top + 7.f, navW, hBtnH});

      // footer
      sf::FloatRect footer = {m_rect.left + 18.f, m_rect.top + m_rect.height - 52.f,
                              m_rect.width - 36.f, 34.f};
      m_continue.setBounds({footer.left + footer.width - 190.f, footer.top + 2.f, 190.f, 32.f});

      // content
      m_pages = {m_rect.left + 18.f, header.top + header.height + 12.f, m_rect.width - 36.f,
                 m_rect.height - 18.f - (header.height + 12.f) - 52.f};

      // tabs row
      m_setupRect = m_pages;
      sf::FloatRect tabs = ui::rowConsume(m_setupRect, 32.f, 12.f);

      sf::FloatRect t = tabs;
      auto a = ui::colConsume(t, 110.f, 8.f);
      auto b = ui::colConsume(t, 90.f, 8.f);

      m_tabPgnFen.setBounds(a);
      m_tabBuild.setBounds(b);

      m_modeRect = m_setupRect;

      // common spacing
      const float gap = 12.f;
      const float labelH = 16.f;
      const float fieldH = 40.f;

      // --- PGN/FEN layout ---
      {
        sf::FloatRect r = m_modeRect;

        // FEN header line (label + status pill)
        m_fenLabelRect = {r.left, r.top, r.width, labelH};
        m_fenField.setBounds({r.left, r.top + labelH + 6.f, r.width, fieldH});

        const float smallH = 28.f;
        const float smallW = 78.f;
        const float btnY = m_fenField.bounds().top + 6.f;
        m_resetFen.setBounds({r.left + r.width - smallW, btnY, smallW, smallH});
        m_pasteFen.setBounds({r.left + r.width - (smallW * 2 + 8.f), btnY, smallW, smallH});

        m_fenStatusRect = {r.left, m_fenField.bounds().top + fieldH + 8.f, r.width, 18.f};

        // PGN
        const float pgnTop = m_fenStatusRect.top + 18.f + gap;
        m_pgnLabelRect = {r.left, pgnTop, r.width, labelH};

        // right-side mini actions
        const float upW = 98.f;
        m_uploadPgn.setBounds({r.left + r.width - upW, pgnTop - 3.f, upW, 26.f});
        m_pastePgn.setBounds({r.left + r.width - upW - 78.f - 8.f, pgnTop - 3.f, 78.f, 26.f});
        m_clearPgn.setBounds({r.left + r.width - upW - 78.f - 78.f - 16.f, pgnTop - 3.f, 78.f, 26.f});

        const float pgnFieldTop = pgnTop + labelH + 6.f;
        const float pgnH = std::max(220.f, r.height - (pgnFieldTop - r.top) - 96.f);
        m_pgnField.setBounds({r.left, pgnFieldTop, r.width, pgnH});
        m_pgnStatusRect = {r.left, pgnFieldTop + pgnH + 8.f, r.width, 18.f};

        // Resolved row (source chips + resolved fen + copy)
        const float resTop = m_pgnStatusRect.top + 18.f + 10.f;
        m_sourceRect = {r.left, resTop, 220.f, 28.f};

        // chips inside source rect
        const float chipW = 64.f;
        const float chipH = 26.f;
        m_srcAuto.setBounds({m_sourceRect.left, m_sourceRect.top + 1.f, chipW, chipH});
        m_srcFen.setBounds({m_sourceRect.left + chipW + 6.f, m_sourceRect.top + 1.f, chipW, chipH});
        m_srcPgn.setBounds({m_sourceRect.left + (chipW + 6.f) * 2.f, m_sourceRect.top + 1.f, chipW, chipH});

        // resolved fen field right side
        const float copyW = 72.f;
        const float fenX = r.left + 240.f;
        const float fenW = std::max(200.f, r.width - (fenX - r.left) - copyW - 8.f);
        m_resolvedFen.setBounds({fenX, resTop + 1.f, fenW, 28.f});
        m_copyResolved.setBounds({fenX + fenW + 8.f, resTop + 1.f, copyW, 28.f});
      }

      // --- Builder layout (stable; only drawn in Builder mode) ---
      {
        const float buildTop = m_modeRect.top;
        float boardSize = std::min(450.f, m_modeRect.height - 110.f);
        m_builder.setBounds({m_modeRect.left, buildTop, boardSize, boardSize});

        m_builderFen.setBounds({m_modeRect.left, buildTop + boardSize + 46.f, m_modeRect.width - 84.f, 36.f});
        m_copyFen.setBounds({m_modeRect.left + m_modeRect.width - 74.f, buildTop + boardSize + 46.f, 74.f, 36.f});
      }

      // History layout
      m_historyRect = m_pages;
      m_historyTitle.setPosition(ui::snap({m_historyRect.left, m_historyRect.top}));
    }

    void update(float /*dt*/) override
    {
      revalidateAll(false);

      // keep resolved fen field updated
      if (!m_showHistory)
      {
        const std::string rf = resolvedFen();
        if (rf != m_resolvedFen.text())
          m_resolvedFen.setText(rf);
      }
    }

    void updateInput(sf::Vector2f mouse, bool /*mouseDown*/) override
    {
      m_mouse = mouse;

      // header/footer hover
      m_close.updateHover(mouse);
      m_continue.updateHover(mouse);

      if (!m_showHistory)
        m_historyBtn.updateHover(mouse);
      else
        m_backBtn.updateHover(mouse);

      if (m_showHistory)
        return;

      // setup hover
      m_tabPgnFen.updateHover(mouse);
      m_tabBuild.updateHover(mouse);

      if (m_mode == Mode::PgnFen)
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
      else
      {
        m_builder.updateHover(mouse);
        m_builderFen.updateHover(mouse);
        m_copyFen.updateHover(mouse);
      }
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

      // header buttons
      if (!m_showHistory)
        m_historyBtn.draw(win);
      else
        m_backBtn.draw(win);

      m_close.draw(win);
      m_continue.draw(win);

      if (m_showHistory)
      {
        drawHistoryPage(win);
        return;
      }

      drawSetupPage(win);
    }

    bool handleEvent(const sf::Event &e, sf::Vector2f mouse) override
    {
      m_mouse = mouse;

      if (e.type == sf::Event::KeyPressed && e.key.code == sf::Keyboard::Escape && closeOnEsc())
      {
        requestDismiss();
        return true;
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
        return handleHistoryPageEvent(e, mouse);

      return handleSetupPageEvent(e, mouse);
    }

  private:
    enum class Mode
    {
      PgnFen,
      Builder
    };

    enum class Source
    {
      Auto,
      Fen,
      Pgn
    };

    void setupActionBtn(ui::Button &b, const char *label, std::function<void()> onClick)
    {
      b.setTheme(&m_theme);
      b.setFont(m_font);
      b.setText(label, 13);
      b.setOnClick(std::move(onClick));
    }

    // -------- Validation helpers (kept, but UI uses minimal messages) --------

    static inline std::string trim_copy(const std::string &s)
    {
      auto is_space = [](unsigned char c)
      { return std::isspace(c) != 0; };
      size_t a = 0, b = s.size();
      while (a < b && is_space((unsigned char)s[a]))
        ++a;
      while (b > a && is_space((unsigned char)s[b - 1]))
        --b;
      return s.substr(a, b - a);
    }

    static inline std::vector<std::string> split_ws(const std::string &s)
    {
      std::vector<std::string> out;
      std::istringstream iss(s);
      std::string tok;
      while (iss >> tok)
        out.push_back(tok);
      return out;
    }

    static inline bool is_piece_placement_char(char c)
    {
      switch (c)
      {
      case 'p':
      case 'r':
      case 'n':
      case 'b':
      case 'q':
      case 'k':
      case 'P':
      case 'R':
      case 'N':
      case 'B':
      case 'Q':
      case 'K':
        return true;
      default:
        return false;
      }
    }

    static inline std::string normalize_fen(std::string fen)
    {
      fen = trim_copy(fen);
      if (fen.empty())
        return fen;

      auto parts = split_ws(fen);
      if (parts.size() == 4)
      {
        parts.push_back("0");
        parts.push_back("1");
      }
      else if (parts.size() == 5)
      {
        parts.push_back("1");
      }

      std::ostringstream ss;
      for (size_t i = 0; i < parts.size(); ++i)
      {
        if (i)
          ss << ' ';
        ss << parts[i];
      }
      return ss.str();
    }

    static inline std::optional<std::string> validate_fen_basic(const std::string &fenRaw)
    {
      const std::string fen = normalize_fen(fenRaw);
      auto parts = split_ws(fen);
      if (parts.size() != 6)
        return std::string("needs 6 fields");

      const std::string &placement = parts[0];
      int ranks = 0;
      int fileCount = 0;
      for (size_t i = 0; i < placement.size(); ++i)
      {
        char c = placement[i];
        if (c == '/')
        {
          if (fileCount != 8)
            return std::string("rank not 8");
          ++ranks;
          fileCount = 0;
          continue;
        }
        if (c >= '1' && c <= '8')
        {
          fileCount += (c - '0');
          continue;
        }
        if (!is_piece_placement_char(c))
          return std::string("bad char");
        fileCount += 1;
        if (fileCount > 8)
          return std::string("rank overflow");
      }
      if (fileCount != 8)
        return std::string("last rank not 8");
      if (ranks != 7)
        return std::string("not 8 ranks");

      if (!(parts[1] == "w" || parts[1] == "b"))
        return std::string("turn not w/b");

      const std::string &cs = parts[2];
      if (cs != "-")
      {
        for (char c : cs)
          if (!(c == 'K' || c == 'Q' || c == 'k' || c == 'q'))
            return std::string("castling invalid");
      }

      const std::string &ep = parts[3];
      if (ep != "-")
      {
        if (ep.size() != 2)
          return std::string("ep invalid");
        char f = ep[0], r = ep[1];
        if (f < 'a' || f > 'h')
          return std::string("ep file");
        if (!(r == '3' || r == '6'))
          return std::string("ep rank");
      }

      for (char c : parts[4])
        if (!std::isdigit((unsigned char)c))
          return std::string("halfmove");
      for (char c : parts[5])
        if (!std::isdigit((unsigned char)c))
          return std::string("fullmove");

      return std::nullopt;
    }

    static inline std::optional<std::string> extract_fen_tag(const std::string &pgn)
    {
      const std::string key = "[FEN \"";
      auto pos = pgn.find(key);
      if (pos == std::string::npos)
        return std::nullopt;
      pos += key.size();
      auto end = pgn.find("\"]", pos);
      if (end == std::string::npos)
        return std::nullopt;
      return pgn.substr(pos, end - pos);
    }

    struct PgnStatus
    {
      enum class Kind
      {
        Empty,
        OkNoFen,
        OkFen,
        Error
      } kind{Kind::Empty};
      std::optional<std::string> fenFromTag;
    };

    static inline PgnStatus validate_pgn_basic(const std::string &pgnRaw)
    {
      PgnStatus st{};
      const std::string pgn = trim_copy(pgnRaw);
      if (pgn.empty())
      {
        st.kind = PgnStatus::Kind::Empty;
        return st;
      }

      if (auto fen = extract_fen_tag(pgn))
      {
        auto err = validate_fen_basic(*fen);
        if (err.has_value())
        {
          st.kind = PgnStatus::Kind::Error;
          return st;
        }
        st.kind = PgnStatus::Kind::OkFen;
        st.fenFromTag = normalize_fen(*fen);
        return st;
      }

      // accept as "moves" if it contains move numbers or result
      const bool looksLikeMoves =
          (pgn.find("1.") != std::string::npos) || (pgn.find("...") != std::string::npos);
      const bool hasResult =
          (pgn.find("1-0") != std::string::npos) || (pgn.find("0-1") != std::string::npos) ||
          (pgn.find("1/2-1/2") != std::string::npos);

      st.kind = (looksLikeMoves || hasResult) ? PgnStatus::Kind::OkNoFen : PgnStatus::Kind::Error;
      return st;
    }

    // -------- resolved source + fen --------

    void revalidateAll(bool force)
    {
      const std::string fenNowRaw = m_fenField.text();
      const std::string pgnNowRaw = m_pgnField.text();

      if (!force && fenNowRaw == m_lastFenRaw && pgnNowRaw == m_lastPgnRaw)
        return;

      m_lastFenRaw = fenNowRaw;
      m_lastPgnRaw = pgnNowRaw;

      m_fenNormalized = normalize_fen(fenNowRaw);
      m_fenValid = !trim_copy(m_fenNormalized).empty() && !validate_fen_basic(m_fenNormalized).has_value();

      m_pgnStatus = validate_pgn_basic(pgnNowRaw);
    }

    std::string resolvedFen() const
    {
      // Builder always resolves to builder position
      if (m_mode == Mode::Builder)
      {
        const std::string bf = normalize_fen(m_builder.fen());
        if (!validate_fen_basic(bf).has_value())
          return bf;
        return core::START_FEN;
      }

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
        // chess.com-like: prefer explicit [FEN] tag if present, else valid FEN field, else startpos
        if (pgnHasFen)
          return *m_pgnStatus.fenFromTag;
        if (fenOk)
          return fenNorm;
        return core::START_FEN;
      }
    }

    std::string activeSourceLabel() const
    {
      if (m_mode == Mode::Builder)
        return "Builder";

      const std::string fenNorm = normalize_fen(m_fenField.text());
      const bool fenOk = !trim_copy(fenNorm).empty() && !validate_fen_basic(fenNorm).has_value();
      const bool pgnHasFen = m_pgnStatus.fenFromTag.has_value();

      if (m_source == Source::Fen)
        return fenOk ? "FEN" : "FEN (fallback)";
      if (m_source == Source::Pgn)
        return pgnHasFen ? "PGN [FEN]" : "PGN (fallback)";
      // Auto
      if (pgnHasFen)
        return "PGN [FEN]";
      if (fenOk)
        return "FEN";
      return "Start position";
    }

    // -------- drawing helpers --------

    static inline sf::Color withA(sf::Color c, uint8_t a)
    {
      c.a = a;
      return c;
    }

    void drawDivider(sf::RenderTarget &rt, float y) const
    {
      sf::RectangleShape line({m_pages.width, 1.f});
      line.setPosition(ui::snap({m_pages.left, y}));
      line.setFillColor(withA(m_theme.panelBorder, 120));
      rt.draw(line);
    }

    void drawLabel(sf::RenderTarget &rt, float x, float y, const std::string &txt) const
    {
      sf::Text t(txt, m_font, 13);
      t.setFillColor(m_theme.subtle);
      t.setPosition(ui::snap({x, y}));
      rt.draw(t);
    }

    void drawStatusMini(sf::RenderTarget &rt, const sf::FloatRect &r, const std::string &txt, int kind) const
    {
      // 0=neutral,1=ok,2=warn,3=err
      sf::Color bg = withA(m_theme.panelBorder, 80);
      sf::Color fg = m_theme.subtle;

      if (kind == 1)
      {
        bg = sf::Color(60, 170, 110, 150);
        fg = sf::Color(230, 255, 240, 255);
      }
      if (kind == 2)
      {
        bg = sf::Color(200, 150, 60, 150);
        fg = sf::Color(255, 245, 230, 255);
      }
      if (kind == 3)
      {
        bg = sf::Color(190, 80, 80, 160);
        fg = sf::Color(255, 235, 235, 255);
      }

      sf::RectangleShape pill({r.width, r.height});
      pill.setPosition(ui::snap({r.left, r.top}));
      pill.setFillColor(bg);
      pill.setOutlineThickness(1.f);
      pill.setOutlineColor(withA(sf::Color::Black, 40));
      rt.draw(pill);

      sf::Text t(txt, m_font, 12);
      t.setFillColor(fg);
      t.setPosition(ui::snap({r.left + 8.f, r.top + 1.f}));
      rt.draw(t);
    }

    // -------- pages --------

    void drawSetupPage(sf::RenderTarget &rt) const
    {
      // tabs
      m_tabPgnFen.setActive(m_mode == Mode::PgnFen);
      m_tabBuild.setActive(m_mode == Mode::Builder);
      m_tabPgnFen.draw(rt);
      m_tabBuild.draw(rt);

      // content
      if (m_mode == Mode::PgnFen)
      {
        // section labels
        drawLabel(rt, m_fenLabelRect.left, m_fenLabelRect.top, "FEN");
        drawLabel(rt, m_pgnLabelRect.left, m_pgnLabelRect.top, "PGN");

        // fields + actions
        m_fenField.draw(rt);
        m_pasteFen.draw(rt);
        m_resetFen.draw(rt);

        m_pgnField.draw(rt);
        m_uploadPgn.draw(rt);
        m_pastePgn.draw(rt);
        m_clearPgn.draw(rt);

        // show selected upload name (subtle, not noisy)
        if (!m_pgnFilename.empty())
        {
          sf::Text fn("Selected: " + m_pgnFilename, m_font, 12);
          fn.setFillColor(m_theme.subtle);
          fn.setPosition(ui::snap({m_pgnField.bounds().left, m_pgnLabelRect.top - 3.f}));
          rt.draw(fn);
        }

        // minimal statuses
        {
          int k = 0;
          std::string txt = "Empty";
          if (!trim_copy(m_fenField.text()).empty())
          {
            if (m_fenValid)
            {
              k = 1;
              txt = "OK";
            }
            else
            {
              k = 3;
              txt = "Invalid";
            }
          }
          drawStatusMini(rt, {m_fenStatusRect.left, m_fenStatusRect.top, 90.f, m_fenStatusRect.height}, "FEN: " + txt, k);
        }
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
              txt = "Moves";
            }
            else
            {
              k = 3;
              txt = "Invalid";
            }
          }
          drawStatusMini(rt, {m_pgnStatusRect.left, m_pgnStatusRect.top, 130.f, m_pgnStatusRect.height}, "PGN: " + txt, k);
        }

        // source chips
        m_srcAuto.setActive(m_source == Source::Auto);
        m_srcFen.setActive(m_source == Source::Fen);
        m_srcPgn.setActive(m_source == Source::Pgn);

        drawLabel(rt, m_sourceRect.left, m_sourceRect.top - 14.f, "Source");
        m_srcAuto.draw(rt);
        m_srcFen.draw(rt);
        m_srcPgn.draw(rt);

        // resolved info
        drawLabel(rt, m_resolvedFen.bounds().left, m_resolvedFen.bounds().top - 14.f, "Resolved position (" + activeSourceLabel() + ")");
        m_resolvedFen.draw(rt);
        m_copyResolved.draw(rt);

        return;
      }

      // Builder
      m_builder.draw(rt);
      m_builderFen.draw(rt);
      m_copyFen.draw(rt);

      sf::Text hint("Hotkeys: 1 Pawn  2 Bishop  3 Knight  4 Rook  5 Queen  6 King   |   Tab: color   |   Right click: clear",
                    m_font, 12);
      hint.setFillColor(m_theme.subtle);
      hint.setPosition(ui::snap({m_builderFen.bounds().left, m_builderFen.bounds().top - 18.f}));
      rt.draw(hint);

      // resolved label bottom
      sf::Text a("Resolved position: Builder", m_font, 13);
      a.setFillColor(m_theme.subtle);
      a.setPosition(ui::snap({m_builderFen.bounds().left, m_builderFen.bounds().top + 44.f}));
      rt.draw(a);
    }

    void drawHistoryPage(sf::RenderTarget &rt) const
    {
      rt.draw(m_historyTitle);

      sf::Text p("History view placeholder.\nRender a list here (saved games, imported PGNs, start positions).", m_font, 14);
      p.setFillColor(m_theme.subtle);
      p.setPosition(ui::snap({m_historyRect.left, m_historyRect.top + 34.f}));
      rt.draw(p);

      sf::FloatRect card = {m_historyRect.left, m_historyRect.top + 110.f, m_historyRect.width, 92.f};
      sf::RectangleShape r({card.width, card.height});
      r.setPosition(ui::snap({card.left, card.top}));
      r.setFillColor(withA(m_theme.panelBorder, 40));
      r.setOutlineThickness(1.f);
      r.setOutlineColor(withA(sf::Color::Black, 40));
      rt.draw(r);

      sf::Text tip("Next: searchable list + open/import.", m_font, 13);
      tip.setFillColor(m_theme.subtle);
      tip.setPosition(ui::snap({card.left + 12.f, card.top + 12.f}));
      rt.draw(tip);
    }

    // -------- events --------

    bool handleSetupPageEvent(const sf::Event &e, sf::Vector2f mouse)
    {
      if (m_tabPgnFen.handleEvent(e, mouse))
        return true;
      if (m_tabBuild.handleEvent(e, mouse))
        return true;

      if (m_mode == Mode::PgnFen)
      {
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

      // Builder
      if (m_copyFen.handleEvent(e, mouse))
        return true;

      if (m_builder.handleEvent(e, mouse))
      {
        m_builderFen.setText(m_builder.fen());
        return true;
      }

      if (m_builderFen.handleEvent(e, mouse))
        return true;
      return false;
    }

    bool handleHistoryPageEvent(const sf::Event & /*e*/, sf::Vector2f /*mouse*/)
    {
      return false;
    }

  private:
    const sf::Font &m_font;
    const ui::Theme &m_theme;
    ui::FocusManager &m_focus;

    sf::Vector2u m_ws{};
    sf::FloatRect m_rect{};
    sf::FloatRect m_inner{};
    sf::FloatRect m_pages{};
    sf::FloatRect m_setupRect{};
    sf::FloatRect m_historyRect{};
    sf::FloatRect m_modeRect{};

    sf::FloatRect m_fenLabelRect{};
    sf::FloatRect m_fenStatusRect{};
    sf::FloatRect m_pgnLabelRect{};
    sf::FloatRect m_pgnStatusRect{};
    sf::FloatRect m_sourceRect{};

    sf::Vector2f m_mouse{};

    sf::Text m_title{};

    // Header/Footer
    ui::Button m_close{};
    ui::Button m_continue{};
    ui::Button m_historyBtn{};
    ui::Button m_backBtn{};

    // Tabs
    mutable ui::Button m_tabPgnFen{};
    mutable ui::Button m_tabBuild{};
    Mode m_mode{Mode::PgnFen};

    // History view toggle (no slide animation)
    bool m_showHistory{false};

    // Inputs
    ui::TextField m_fenField{};
    ui::TextArea m_pgnField{};

    ui::Button m_pasteFen{};
    ui::Button m_resetFen{};
    ui::Button m_uploadPgn{};
    ui::Button m_pastePgn{};
    ui::Button m_clearPgn{};

    // Source selection
    Source m_source{Source::Auto};
    mutable ui::Button m_srcAuto{};
    mutable ui::Button m_srcFen{};
    mutable ui::Button m_srcPgn{};

    // Resolved display
    ui::TextField m_resolvedFen{};
    ui::Button m_copyResolved{};

    // Builder
    ui::TextField m_builderFen{};
    ui::Button m_copyFen{};
    PositionBuilder m_builder{};

    // Result
    std::optional<std::string> m_resultFen{};

    // Validation cache/state
    std::string m_lastFenRaw;
    std::string m_lastPgnRaw;
    bool m_fenValid{true};
    std::string m_fenNormalized;
    PgnStatus m_pgnStatus{};

    // History placeholder
    sf::Text m_historyTitle{};

    // Upload
    std::function<void()> m_onRequestPgnUpload;
    std::string m_pgnFilename;
  };

} // namespace lilia::view::ui
