#pragma once

#include <SFML/Graphics.hpp>
#include <SFML/Window/Keyboard.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <optional>
#include <string>

#include "lilia/constants.hpp"
#include "lilia/view/ui/render/render_constants.hpp"
#include "lilia/view/ui/render/texture_table.hpp"

#include "../../style.hpp"
#include "../../theme.hpp"

namespace lilia::view
{
  class PositionBuilder
  {
  public:
    PositionBuilder()
    {
      resetToStart(false);

      if (!s_lastFen.empty())
        setFromFen(s_lastFen, false);

      enterAddDefault();
    }

    void onOpen()
    {
      if (!s_lastFen.empty())
        setFromFen(s_lastFen, false);
      else
        resetToStart(false);

      enterAddDefault();
    }

    void setTheme(const ui::Theme *t)
    {
      m_theme = t;
      m_texReady = false;
      rebuildGeometry();
    }

    void setFont(const sf::Font *f)
    {
      m_font = f;
      rebuildGeometry();
    }

    void setBounds(sf::FloatRect r)
    {
      m_bounds = r;
      rebuildGeometry();
    }

    void clear(bool remember = true)
    {
      for (auto &r : m_board)
        r.fill('.');

      m_dragging = false;
      m_dragPiece = '.';
      m_dragFrom.reset();

      refreshKingCounts();
      if (remember)
        rememberCurrent();
    }

    void resetToStart(bool remember = true)
    {
      clear(false);
      setFromFen(core::START_FEN, remember);
    }

    std::string fen() const { return placement() + " w - - 0 1"; }

    std::string fenForUse() const
    {
      if (!kingsOk())
        return {};
      return fen();
    }

    bool kingsOk() const { return (m_whiteKings == 1 && m_blackKings == 1); }
    int whiteKings() const { return m_whiteKings; }
    int blackKings() const { return m_blackKings; }

    void updateHover(sf::Vector2f mouse, sf::Vector2f offset = {})
    {
      m_mouseGlobal = mouse;
      m_offset = offset;

      sf::Vector2f local = {mouse.x - offset.x, mouse.y - offset.y};

      m_hoverSquare = squareFromMouse(local);
      m_hoverLeft = hitLeft(local);
      m_hoverPiece = hitPiece(local);
    }

    // States:
    // - Add (A): places pieces (defaults to pawn of current color when you enter Add).
    // - Move (M): drag existing pieces.
    //
    // Delete:
    // - Right mouse button deletes a square (no erase state).
    //
    // Other:
    // - Tab toggles color
    // - W / B forces color
    // - 1..6 selects piece type for Add (Pawn/Bishop/Knight/Rook/Queen/King)
    // - C clears board
    // - R resets to start position
    bool handleEvent(const sf::Event &e, sf::Vector2f mouse, sf::Vector2f offset = {})
    {
      if (!m_theme || !m_font)
        return false;

      m_mouseGlobal = mouse;
      m_offset = offset;
      sf::Vector2f local = {mouse.x - offset.x, mouse.y - offset.y};

      if (e.type == sf::Event::KeyPressed)
      {
        const bool shift = e.key.shift;

        if (e.key.code == sf::Keyboard::Tab)
        {
          m_placeWhite = !m_placeWhite;
          if (m_selected.kind == ToolKind::Piece)
            m_selected.pieceChar = applyColorToPieceType(m_lastAddPieceLower, m_placeWhite);
          return true;
        }

        if (e.key.code == sf::Keyboard::W)
        {
          m_placeWhite = true;
          if (m_selected.kind == ToolKind::Piece)
            m_selected.pieceChar = applyColorToPieceType(m_lastAddPieceLower, true);
          return true;
        }

        if (e.key.code == sf::Keyboard::B)
        {
          m_placeWhite = false;
          if (m_selected.kind == ToolKind::Piece)
            m_selected.pieceChar = applyColorToPieceType(m_lastAddPieceLower, false);
          return true;
        }

        if (e.key.code == sf::Keyboard::A)
        {
          enterAddDefault();
          return true;
        }

        if (e.key.code == sf::Keyboard::M)
        {
          m_selected = ToolSelection::move();
          return true;
        }

        if (e.key.code == sf::Keyboard::C)
        {
          clear(true);
          return true;
        }

        if (e.key.code == sf::Keyboard::R)
        {
          resetToStart(true);
          return true;
        }

        char placed = '.';
        if (e.key.code == sf::Keyboard::Num1)
          placed = 'p';
        if (e.key.code == sf::Keyboard::Num2)
          placed = 'b';
        if (e.key.code == sf::Keyboard::Num3)
          placed = 'n';
        if (e.key.code == sf::Keyboard::Num4)
          placed = 'r';
        if (e.key.code == sf::Keyboard::Num5)
          placed = 'q';
        if (e.key.code == sf::Keyboard::Num6)
          placed = 'k';

        if (placed != '.')
        {
          m_lastAddPieceLower = placed;
          const bool white = shift ? false : m_placeWhite;
          m_selected = ToolSelection::make_piece(applyColorToPieceType(placed, white));
          return true;
        }
      }

      // Right click deletes a square.
      if (e.type == sf::Event::MouseButtonPressed && e.mouseButton.button == sf::Mouse::Right)
      {
        auto sq = squareFromMouse(local);
        if (!sq)
          return false;

        auto [x, y] = *sq;
        trySet(x, y, '.', true);
        return true;
      }

      if (e.type == sf::Event::MouseButtonPressed && e.mouseButton.button == sf::Mouse::Left)
      {
        if (auto id = hitLeft(local))
        {
          onLeftButton(*id);
          return true;
        }

        if (auto p = hitPiece(local))
        {
          const char chosen = *p;
          m_placeWhite = std::isupper(static_cast<unsigned char>(chosen));
          m_lastAddPieceLower = char(std::tolower(static_cast<unsigned char>(chosen)));
          m_selected = ToolSelection::make_piece(chosen);
          return true;
        }

        auto sq = squareFromMouse(local);
        if (!sq)
          return false;

        auto [x, y] = *sq;

        if (m_selected.kind == ToolKind::Piece)
        {
          if (!trySet(x, y, m_selected.pieceChar, true))
            invalidAction("Kings must be unique per color.\nUse Move to reposition an existing king.");
          return true;
        }

        // Move
        char p = at(x, y);
        if (p != '.')
        {
          m_dragging = true;
          m_dragPiece = p;
          m_dragFrom = std::make_pair(x, y);

          set(x, y, '.');
          refreshKingCounts();
          return true;
        }

        return false;
      }

      if (e.type == sf::Event::MouseButtonReleased && e.mouseButton.button == sf::Mouse::Left)
      {
        if (!m_dragging)
          return false;

        m_dragging = false;

        auto sq = squareFromMouse(local);
        if (sq)
        {
          auto [tx, ty] = *sq;
          if (!trySet(tx, ty, m_dragPiece, true))
          {
            invalidAction("Invalid drop.\nKings must be unique per color.");
            if (m_dragFrom)
            {
              auto [ox, oy] = *m_dragFrom;
              set(ox, oy, m_dragPiece);
            }
          }
        }
        else if (m_dragFrom)
        {
          auto [ox, oy] = *m_dragFrom;
          set(ox, oy, m_dragPiece);
        }

        m_dragPiece = '.';
        m_dragFrom.reset();
        refreshKingCounts();
        rememberCurrent();
        return true;
      }

      return false;
    }

    void draw(sf::RenderTarget &rt, sf::Vector2f offset = {}) const
    {
      if (!m_theme || !m_font)
        return;

      ensureTextures();

      const float dt = clampDt(m_animClock.restart().asSeconds());
      animate(dt);

      // Background fill only (no outer outline) to avoid the "double outline".
      sf::RectangleShape bg({m_bounds.width, m_bounds.height});
      bg.setPosition(ui::snap({m_bounds.left + offset.x, m_bounds.top + offset.y}));
      bg.setFillColor(m_theme->panel);
      rt.draw(bg);

      sf::Vector2f shake = {0.f, 0.f};
      if (m_shakeT > 0.f)
      {
        const float a = (m_shakeT / m_shakeDur);
        shake.x = std::sin(m_shakePhase) * (6.f * a);
      }

      drawSidePanel(rt, offset, m_leftRect, "Tools", true, shake);
      drawSidePanel(rt, offset, m_rightRect, "Pieces", false, shake);
      drawBoard(rt, offset, shake);

      if (m_showShortcuts)
        drawShortcutsOverlay(rt, offset);

      if (m_errorT > 0.f)
        drawErrorToast(rt, offset);
    }

  private:
    static inline std::string s_lastFen{};

    const ui::Theme *m_theme{nullptr};
    const sf::Font *m_font{nullptr};

    sf::FloatRect m_bounds{};

    sf::FloatRect m_boardRect{};
    sf::FloatRect m_leftRect{};
    sf::FloatRect m_rightRect{};
    sf::FloatRect m_shortcutsRect{};
    float m_sq{44.f};
    float m_pieceYOffset{0.f};

    std::array<std::array<char, 8>, 8> m_board{};

    int m_whiteKings{0};
    int m_blackKings{0};

    enum class ToolKind
    {
      Move,
      Piece
    };

    struct ToolSelection
    {
      ToolKind kind{ToolKind::Move};
      char pieceChar{'.'};

      static ToolSelection move() { return ToolSelection{ToolKind::Move, '.'}; }
      static ToolSelection make_piece(char p) { return ToolSelection{ToolKind::Piece, p}; }
    };

    ToolSelection m_selected = ToolSelection::move();

    bool m_placeWhite{true};
    char m_lastAddPieceLower{'p'};

    bool m_dragging{false};
    char m_dragPiece{'.'};
    std::optional<std::pair<int, int>> m_dragFrom{};

    mutable sf::Vector2f m_mouseGlobal{};
    mutable sf::Vector2f m_offset{};
    std::optional<std::pair<int, int>> m_hoverSquare{};

    enum class LeftBtn
    {
      White,
      Black,
      Add,
      Move,
      Clear,
      Reset,
      Shortcuts
    };

    sf::FloatRect m_btnWhite{};
    sf::FloatRect m_btnBlack{};
    sf::FloatRect m_btnAdd{};
    sf::FloatRect m_btnMove{};
    sf::FloatRect m_btnClear{};
    sf::FloatRect m_btnReset{};
    sf::FloatRect m_btnShortcuts{};

    bool m_showShortcuts{false};

    std::array<sf::FloatRect, 12> m_pieceBtns{};

    std::optional<LeftBtn> m_hoverLeft{};
    std::optional<char> m_hoverPiece{};

    mutable float m_hvWhite{0}, m_hvBlack{0}, m_hvAdd{0}, m_hvMove{0}, m_hvClear{0}, m_hvReset{0},
        m_hvShort{0};
    mutable std::array<float, 12> m_hvPiece{};

    mutable sf::Clock m_animClock{};
    mutable float m_shakeT{0.f};
    mutable float m_shakeDur{0.18f};
    mutable float m_shakePhase{0.f};

    mutable float m_errorT{0.f};
    mutable float m_errorDur{1.1f};
    mutable std::string m_errorMsg{};

    mutable bool m_texReady{false};
    mutable const sf::Texture *m_texWhite{nullptr};
    mutable const sf::Texture *m_texBlack{nullptr};
    mutable std::array<const sf::Texture *, 12> m_pieceTex{};
    mutable sf::Sprite m_sqWhite{};
    mutable sf::Sprite m_sqBlack{};
    mutable std::array<sf::Sprite, 12> m_pieceTpl{};

    // ---------- behavior ----------
    void enterAddDefault()
    {
      m_lastAddPieceLower = 'p';
      m_selected = ToolSelection::make_piece(applyColorToPieceType('p', m_placeWhite));
    }

    void rememberCurrent() { s_lastFen = fen(); }

    // ---------- animation helpers ----------
    static float clampDt(float dt)
    {
      if (dt < 0.f)
        return 0.f;
      if (dt > 0.05f)
        return 0.05f;
      return dt;
    }

    static float approach(float current, float target, float k)
    {
      return current + (target - current) * std::clamp(k, 0.f, 1.f);
    }

    static sf::Color withA(sf::Color c, sf::Uint8 a)
    {
      c.a = a;
      return c;
    }

    static sf::Color mix(sf::Color a, sf::Color b, float t)
    {
      t = std::clamp(t, 0.f, 1.f);
      auto lerp = [&](sf::Uint8 x, sf::Uint8 y) -> sf::Uint8
      {
        return sf::Uint8(float(x) + (float(y) - float(x)) * t);
      };
      return sf::Color(lerp(a.r, b.r), lerp(a.g, b.g), lerp(a.b, b.b), lerp(a.a, b.a));
    }

    void invalidAction(const std::string &msg) const
    {
      m_errorMsg = msg;
      m_errorT = m_errorDur;
      m_shakeT = m_shakeDur;
      m_shakePhase = 0.f;
    }

    void animate(float dt) const
    {
      if (m_shakeT > 0.f)
      {
        m_shakeT = std::max(0.f, m_shakeT - dt);
        m_shakePhase += dt * 55.f;
      }

      if (m_errorT > 0.f)
        m_errorT = std::max(0.f, m_errorT - dt);

      const float k = dt * 12.f;

      m_hvWhite = approach(m_hvWhite, (m_hoverLeft && *m_hoverLeft == LeftBtn::White) ? 1.f : 0.f, k);
      m_hvBlack = approach(m_hvBlack, (m_hoverLeft && *m_hoverLeft == LeftBtn::Black) ? 1.f : 0.f, k);
      m_hvAdd = approach(m_hvAdd, (m_hoverLeft && *m_hoverLeft == LeftBtn::Add) ? 1.f : 0.f, k);
      m_hvMove = approach(m_hvMove, (m_hoverLeft && *m_hoverLeft == LeftBtn::Move) ? 1.f : 0.f, k);
      m_hvClear = approach(m_hvClear, (m_hoverLeft && *m_hoverLeft == LeftBtn::Clear) ? 1.f : 0.f, k);
      m_hvReset = approach(m_hvReset, (m_hoverLeft && *m_hoverLeft == LeftBtn::Reset) ? 1.f : 0.f, k);
      m_hvShort = approach(m_hvShort, (m_hoverLeft && *m_hoverLeft == LeftBtn::Shortcuts) ? 1.f : 0.f, k);

      for (int i = 0; i < 12; ++i)
      {
        const char pc = pieceCharFromIndex(i);
        const bool hov = (m_hoverPiece && *m_hoverPiece == pc);
        m_hvPiece[i] = approach(m_hvPiece[i], hov ? 1.f : 0.f, k);
      }
    }

    // ---------- king rule ----------
    void refreshKingCounts()
    {
      int wk = 0, bk = 0;
      for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; ++x)
        {
          const char p = at(x, y);
          if (p == 'K')
            ++wk;
          else if (p == 'k')
            ++bk;
        }
      m_whiteKings = wk;
      m_blackKings = bk;
    }

    bool wouldViolateKingUniqueness(int x, int y, char newP) const
    {
      if (newP != 'K' && newP != 'k')
        return false;

      const char old = at(x, y);
      if (old == newP)
        return false;

      int count = 0;
      for (int yy = 0; yy < 8; ++yy)
        for (int xx = 0; xx < 8; ++xx)
        {
          if (xx == x && yy == y)
            continue;
          if (at(xx, yy) == newP)
            ++count;
        }
      return (count >= 1);
    }

    bool trySet(int x, int y, char p, bool remember)
    {
      if (p == 'K' || p == 'k')
      {
        if (wouldViolateKingUniqueness(x, y, p))
          return false;
      }

      set(x, y, p);
      refreshKingCounts();
      if (remember)
        rememberCurrent();
      return true;
    }

    // ---------- geometry (symmetry + safe text areas) ----------
    void rebuildGeometry()
    {
      if (m_bounds.width <= 0.f || m_bounds.height <= 0.f)
        return;

      const float pad = 14.f;
      const float gap = 14.f;
      const float topInset = 16.f;

      // Reserve below-board space for shortcuts so it never clips (even when hidden).
      const float shortcutsGap = 14.f;
      const float shortcutsH = 84.f;

      // Symmetric side panels: equal widths.
      const float sideW = std::clamp(m_bounds.width * 0.20f, 160.f, 240.f);

      float availW = m_bounds.width - pad * 2.f - sideW * 2.f - gap * 2.f;
      float availHTotal = m_bounds.height - pad * 2.f - topInset;

      float boardMaxH = std::max(240.f, availHTotal - (shortcutsGap + shortcutsH));
      float boardSize = std::min(availW, boardMaxH);
      boardSize = std::max(240.f, boardSize);

      m_sq = boardSize / 8.f;
      m_pieceYOffset = m_sq * 0.03f;

      // Center the entire block (board + shortcuts) vertically.
      const float blockH = boardSize + shortcutsGap + shortcutsH;
      const float blockTop = m_bounds.top + pad + topInset + (availHTotal - blockH) * 0.5f;

      // Center the board horizontally between symmetric side panels.
      const float midLeft = m_bounds.left + pad + sideW + gap;
      const float midRight = m_bounds.left + m_bounds.width - pad - sideW - gap;
      const float midW = (midRight - midLeft);

      const float boardLeft = midLeft + (midW - boardSize) * 0.5f;
      const float boardTop = blockTop;

      m_boardRect = {boardLeft, boardTop, boardSize, boardSize};

      m_leftRect = {m_bounds.left + pad, boardTop, sideW, boardSize};
      m_rightRect = {m_bounds.left + m_bounds.width - pad - sideW, boardTop, sideW, boardSize};

      m_shortcutsRect = {m_boardRect.left, m_boardRect.top + m_boardRect.height + shortcutsGap,
                         m_boardRect.width, shortcutsH};

      // Left panel layout
      {
        const float x = m_leftRect.left + 10.f;
        const float w = m_leftRect.width - 20.f;

        const float h = 34.f;
        const float g = 10.f;

        float y = m_leftRect.top + 52.f; // safe below title

        // Color segmented
        const float half = std::floor(w * 0.5f);
        m_btnWhite = {x, y, half, h};
        m_btnBlack = {x + half, y, w - half, h};
        y += (h + g);

        // State segmented (Add/Move)
        m_btnAdd = {x, y, half, h};
        m_btnMove = {x + half, y, w - half, h};
        y += (h + g);

        // Actions
        y += 6.f;
        m_btnClear = {x, y, w, h};
        y += (h + g);
        m_btnReset = {x, y, w, h};

        // Shortcuts button anchored to bottom (requested)
        const float bottomY = (m_leftRect.top + m_leftRect.height) - 10.f - h;
        m_btnShortcuts = {x, bottomY, w, h};
      }

      // Right panel piece grid + status at bottom
      {
        const float padR = 10.f;
        const float left = m_rightRect.left + padR;
        const float w = m_rightRect.width - padR * 2.f;

        const float titleZone = 54.f;
        const float reserveBottom = 76.f;

        const float top = m_rightRect.top + titleZone;
        const float bottom = m_rightRect.top + m_rightRect.height - reserveBottom;
        const float gridH = std::max(0.f, bottom - top);

        const float cellGap = 10.f;
        const float sep = 18.f;

        const float cellMaxW = std::floor((w - cellGap * 2.f) / 3.f);
        const float cellMaxH = std::floor((gridH - (cellGap * 3.f + sep)) / 4.f);
        const float cell = std::clamp(std::min(cellMaxW, cellMaxH), 40.f, 78.f);

        auto rectAt = [&](int col, int row, float baseY) -> sf::FloatRect
        {
          return {left + col * (cell + cellGap), baseY + row * (cell + cellGap), cell, cell};
        };

        float y0 = top;
        m_pieceBtns[0] = rectAt(0, 0, y0);
        m_pieceBtns[1] = rectAt(1, 0, y0);
        m_pieceBtns[2] = rectAt(2, 0, y0);
        m_pieceBtns[3] = rectAt(0, 1, y0);
        m_pieceBtns[4] = rectAt(1, 1, y0);
        m_pieceBtns[5] = rectAt(2, 1, y0);

        float y1 = y0 + 2.f * cell + cellGap + sep;
        m_pieceBtns[6] = rectAt(0, 0, y1);
        m_pieceBtns[7] = rectAt(1, 0, y1);
        m_pieceBtns[8] = rectAt(2, 0, y1);
        m_pieceBtns[9] = rectAt(0, 1, y1);
        m_pieceBtns[10] = rectAt(1, 1, y1);
        m_pieceBtns[11] = rectAt(2, 1, y1);
      }

      m_texReady = false;
      refreshKingCounts();
    }

    // ---------- texture helpers ----------
    static int typeIndexFromLower(char lower)
    {
      switch (lower)
      {
      case 'p':
        return 0;
      case 'b':
        return 1;
      case 'n':
        return 2;
      case 'r':
        return 3;
      case 'q':
        return 4;
      case 'k':
        return 5;
      default:
        return -1;
      }
    }

    static std::string pieceFilenameFromChar(char p)
    {
      const bool white = std::isupper(static_cast<unsigned char>(p));
      const char lower = char(std::tolower(static_cast<unsigned char>(p)));
      const int t = typeIndexFromLower(lower);
      if (t < 0)
        return {};

      const int colorIdx = white ? 0 : 1;
      const int idx = t + 6 * colorIdx;
      return std::string{constant::path::PIECES_DIR} + "/piece_" + std::to_string(idx) + ".png";
    }

    static int pieceSlotFromChar(char p)
    {
      const bool white = std::isupper(static_cast<unsigned char>(p));
      const char lower = char(std::tolower(static_cast<unsigned char>(p)));
      const int t = typeIndexFromLower(lower);
      if (t < 0)
        return -1;
      return t + (white ? 0 : 6);
    }

    void ensureTextures() const
    {
      if (m_texReady)
        return;

      m_texWhite = &TextureTable::getInstance().get(std::string{constant::tex::WHITE});
      m_texBlack = &TextureTable::getInstance().get(std::string{constant::tex::BLACK});

      if (m_texWhite && m_texWhite->getSize().x > 0)
      {
        m_sqWhite.setTexture(*m_texWhite, true);
        const auto sz = m_texWhite->getSize();
        m_sqWhite.setScale(m_sq / float(sz.x), m_sq / float(sz.y));
      }
      if (m_texBlack && m_texBlack->getSize().x > 0)
      {
        m_sqBlack.setTexture(*m_texBlack, true);
        const auto sz = m_texBlack->getSize();
        m_sqBlack.setScale(m_sq / float(sz.x), m_sq / float(sz.y));
      }

      m_pieceTex.fill(nullptr);
      for (int i = 0; i < 12; ++i)
        m_pieceTpl[i] = sf::Sprite{};

      auto load = [&](char p)
      {
        const int slot = pieceSlotFromChar(p);
        if (slot < 0 || slot >= 12)
          return;

        const std::string fn = pieceFilenameFromChar(p);
        if (fn.empty())
          return;

        const sf::Texture &t = TextureTable::getInstance().get(fn);
        m_pieceTex[slot] = &t;

        sf::Sprite spr;
        spr.setTexture(t, true);

        const auto ts = t.getSize();
        spr.setOrigin(float(ts.x) * 0.5f, float(ts.y) * 0.5f);

        const float target = m_sq * 0.92f;
        const float scale = (ts.y > 0) ? (target / float(ts.y)) : 1.f;
        spr.setScale(scale, scale);

        m_pieceTpl[slot] = spr;
      };

      load('P');
      load('B');
      load('N');
      load('R');
      load('Q');
      load('K');
      load('p');
      load('b');
      load('n');
      load('r');
      load('q');
      load('k');

      m_texReady = true;
    }

    sf::Sprite spriteForPiece(char p) const
    {
      ensureTextures();
      const int slot = pieceSlotFromChar(p);
      if (slot < 0 || slot >= 12 || !m_pieceTex[slot])
        return sf::Sprite{};
      return m_pieceTpl[slot];
    }

    // ---------- mapping ----------
    static char applyColorToPieceType(char lowerPiece, bool white)
    {
      const char l = char(std::tolower(static_cast<unsigned char>(lowerPiece)));
      return white ? char(std::toupper(static_cast<unsigned char>(l))) : l;
    }

    static char pieceCharFromIndex(int idx)
    {
      static constexpr std::array<char, 12> pieces{
          'P', 'B', 'N', 'R', 'Q', 'K',
          'p', 'b', 'n', 'r', 'q', 'k'};
      return pieces[idx];
    }

    std::optional<std::pair<int, int>> squareFromMouse(sf::Vector2f localMouse) const
    {
      if (!m_boardRect.contains(localMouse))
        return std::nullopt;

      const int x = int((localMouse.x - m_boardRect.left) / m_sq);
      const int y = int((localMouse.y - m_boardRect.top) / m_sq);
      if (x < 0 || x > 7 || y < 0 || y > 7)
        return std::nullopt;
      return std::make_pair(x, y);
    }

    std::optional<LeftBtn> hitLeft(sf::Vector2f p) const
    {
      if (m_btnWhite.contains(p))
        return LeftBtn::White;
      if (m_btnBlack.contains(p))
        return LeftBtn::Black;
      if (m_btnAdd.contains(p))
        return LeftBtn::Add;
      if (m_btnMove.contains(p))
        return LeftBtn::Move;
      if (m_btnClear.contains(p))
        return LeftBtn::Clear;
      if (m_btnReset.contains(p))
        return LeftBtn::Reset;
      if (m_btnShortcuts.contains(p))
        return LeftBtn::Shortcuts;
      return std::nullopt;
    }

    std::optional<char> hitPiece(sf::Vector2f p) const
    {
      for (int i = 0; i < 12; ++i)
        if (m_pieceBtns[i].contains(p))
          return pieceCharFromIndex(i);
      return std::nullopt;
    }

    void onLeftButton(LeftBtn id)
    {
      switch (id)
      {
      case LeftBtn::White:
        m_placeWhite = true;
        if (m_selected.kind == ToolKind::Piece)
          m_selected.pieceChar = applyColorToPieceType(m_lastAddPieceLower, true);
        break;

      case LeftBtn::Black:
        m_placeWhite = false;
        if (m_selected.kind == ToolKind::Piece)
          m_selected.pieceChar = applyColorToPieceType(m_lastAddPieceLower, false);
        break;

      case LeftBtn::Add:
        enterAddDefault();
        break;

      case LeftBtn::Move:
        m_selected = ToolSelection::move();
        break;

      case LeftBtn::Clear:
        clear(true);
        break;

      case LeftBtn::Reset:
        resetToStart(true);
        break;

      case LeftBtn::Shortcuts:
        m_showShortcuts = !m_showShortcuts;
        break;
      }
    }

    // ---------- drawing ----------
    void drawSidePanel(sf::RenderTarget &rt, sf::Vector2f offset, const sf::FloatRect &r,
                       const std::string &title, bool left, sf::Vector2f shake) const
    {
      sf::RectangleShape box({r.width, r.height});
      box.setPosition(ui::snap({r.left + offset.x + shake.x * (left ? 0.18f : 0.12f),
                                r.top + offset.y}));
      box.setFillColor(withA(m_theme->panelBorder, 28));
      box.setOutlineThickness(1.f);
      box.setOutlineColor(withA(m_theme->panelBorder, 95));
      rt.draw(box);

      sf::Text t(title, *m_font, 14);
      t.setFillColor(m_theme->text);
      t.setPosition(ui::snap({r.left + offset.x + 10.f + shake.x * (left ? 0.18f : 0.12f),
                              r.top + offset.y + 10.f}));
      rt.draw(t);

      if (left)
        drawLeftButtons(rt, offset, shake);
      else
      {
        drawPieceButtons(rt, offset, shake);
        drawPlayableStatus(rt, offset, shake);
      }
    }

    void drawSegmented2(sf::RenderTarget &rt, sf::Vector2f offset,
                        const sf::FloatRect &leftR, const sf::FloatRect &rightR,
                        const std::string &leftLabel, const std::string &rightLabel,
                        bool leftActive, bool rightActive,
                        float leftHover, float rightHover,
                        sf::Vector2f shake, bool emphasize) const
    {
      // Group outline (unified control)
      sf::FloatRect g = leftR;
      g.width = (rightR.left + rightR.width) - leftR.left;
      g.height = std::max(leftR.height, rightR.height);

      const float shakeX = shake.x * (emphasize ? 0.22f : 0.18f);

      sf::RectangleShape group({g.width, g.height});
      group.setPosition(ui::snap({g.left + offset.x + shakeX, g.top + offset.y}));
      group.setFillColor(withA(m_theme->panelBorder, emphasize ? 36 : 30));
      group.setOutlineThickness(emphasize ? 2.f : 1.f);
      group.setOutlineColor(withA(m_theme->panelBorder, emphasize ? 140 : 110));
      rt.draw(group);

      auto drawSeg = [&](const sf::FloatRect &r, const std::string &lbl, bool active, float hov)
      {
        sf::Color base = withA(m_theme->panelBorder, 26);
        sf::Color hover = mix(base, withA(m_theme->accent, 80), hov);
        sf::Color fill = active ? mix(hover, withA(m_theme->accent, 160), 0.72f) : hover;

        sf::RectangleShape seg({r.width, r.height});
        seg.setPosition(ui::snap({r.left + offset.x + shakeX, r.top + offset.y}));
        seg.setFillColor(fill);
        seg.setOutlineThickness(0.f);
        rt.draw(seg);

        sf::Text t(lbl, *m_font, emphasize ? 12 : 12);
        t.setFillColor(m_theme->text);
        centerText(t, {r.left + offset.x + shakeX, r.top + offset.y, r.width, r.height});
        rt.draw(t);
      };

      drawSeg(leftR, leftLabel, leftActive, leftHover);
      drawSeg(rightR, rightLabel, rightActive, rightHover);

      // Divider line
      sf::RectangleShape div({1.f, g.height - 8.f});
      div.setPosition(ui::snap({rightR.left + offset.x + shakeX, g.top + offset.y + 4.f}));
      div.setFillColor(withA(m_theme->panelBorder, 110));
      rt.draw(div);
    }

    void drawButton(sf::RenderTarget &rt, sf::Vector2f offset, const sf::FloatRect &r,
                    const std::string &label, bool active, float hoverT, sf::Vector2f shake,
                    bool danger = false) const
    {
      const sf::Color dangerAccent = sf::Color(200, 80, 95);
      const sf::Color accent = danger ? dangerAccent : m_theme->accent;

      sf::Color base = withA(m_theme->panelBorder, 32);
      sf::Color hov = mix(base, withA(accent, 85), hoverT);
      sf::Color fill = active ? mix(hov, withA(accent, 150), 0.70f) : hov;

      sf::RectangleShape box({r.width, r.height});
      box.setPosition(ui::snap({r.left + offset.x + shake.x * 0.18f, r.top + offset.y}));
      box.setFillColor(fill);
      box.setOutlineThickness(1.f);
      box.setOutlineColor(withA(m_theme->panelBorder, active ? 175 : 110));
      rt.draw(box);

      sf::Text t(label, *m_font, 12);
      t.setFillColor(danger ? sf::Color(255, 210, 215) : m_theme->text);
      centerText(t, {r.left + offset.x + shake.x * 0.18f, r.top + offset.y, r.width, r.height});
      rt.draw(t);
    }

    float keyCapWidthPx(const std::string &txt) const
    {
      sf::Text t(txt, *m_font, 11);
      auto b = t.getLocalBounds();
      const float padX = 6.f;
      return b.width + padX * 2.f;
    }

    void drawKeyCap(sf::RenderTarget &rt, sf::Vector2f pos, const std::string &txt) const
    {
      sf::Text t(txt, *m_font, 11);
      t.setFillColor(m_theme->text);

      auto b = t.getLocalBounds();
      const float padX = 6.f, padY = 3.f;
      sf::FloatRect r{pos.x, pos.y, b.width + padX * 2.f, b.height + padY * 2.f};

      sf::RectangleShape box({r.width, r.height});
      box.setPosition(ui::snap({r.left, r.top}));
      box.setFillColor(withA(m_theme->panelBorder, 34));
      box.setOutlineThickness(1.f);
      box.setOutlineColor(withA(m_theme->panelBorder, 110));
      rt.draw(box);

      t.setPosition(ui::snap({r.left + padX, r.top + padY - 1.f}));
      rt.draw(t);
    }

    void drawShortcutItem(sf::RenderTarget &rt, sf::Vector2f start,
                          const std::string &key, const std::string &label) const
    {
      drawKeyCap(rt, start, key);

      sf::Text t(label, *m_font, 11);
      t.setFillColor(m_theme->subtle);
      t.setPosition(ui::snap({start.x + keyCapWidthPx(key) + 8.f, start.y + 2.f}));
      rt.draw(t);
    }

    float shortcutItemWidthPx(const std::string &key, const std::string &label) const
    {
      sf::Text t(label, *m_font, 11);
      auto b = t.getLocalBounds();
      return keyCapWidthPx(key) + 8.f + b.width;
    }

    void drawLeftButtons(sf::RenderTarget &rt, sf::Vector2f offset, sf::Vector2f shake) const
    {
      // Color segmented control
      drawSegmented2(rt, offset,
                     m_btnWhite, m_btnBlack,
                     "White", "Black",
                     m_placeWhite, !m_placeWhite,
                     m_hvWhite, m_hvBlack,
                     shake, false);

      // Add/Move segmented control (emphasized state switch)
      const bool addActive = (m_selected.kind == ToolKind::Piece);
      const bool moveActive = (m_selected.kind == ToolKind::Move);

      drawSegmented2(rt, offset,
                     m_btnAdd, m_btnMove,
                     "Add", "Move",
                     addActive, moveActive,
                     m_hvAdd, m_hvMove,
                     shake, true);

      // Actions
      drawButton(rt, offset, m_btnClear, "Clear", false, m_hvClear, shake, true);
      drawButton(rt, offset, m_btnReset, "Reset", false, m_hvReset, shake);

      // Shortcuts anchored to bottom (toggle style)
      drawButton(rt, offset, m_btnShortcuts, "Shortcuts", m_showShortcuts, m_hvShort, shake);
    }

    void drawPieceButtons(sf::RenderTarget &rt, sf::Vector2f offset, sf::Vector2f shake) const
    {
      sf::Text labelW("White", *m_font, 12);
      labelW.setFillColor(m_theme->subtle);
      labelW.setPosition(ui::snap({m_rightRect.left + offset.x + 10.f + shake.x * 0.12f,
                                   m_rightRect.top + offset.y + 32.f}));
      rt.draw(labelW);

      sf::Text labelB("Black", *m_font, 12);
      labelB.setFillColor(m_theme->subtle);
      labelB.setPosition(ui::snap({m_pieceBtns[6].left + offset.x + shake.x * 0.12f,
                                   m_pieceBtns[6].top + offset.y - 18.f}));
      rt.draw(labelB);

      for (int i = 0; i < 12; ++i)
      {
        const char pc = pieceCharFromIndex(i);
        const bool active = (m_selected.kind == ToolKind::Piece && m_selected.pieceChar == pc);

        sf::FloatRect r = m_pieceBtns[i];
        float hov = m_hvPiece[i];

        sf::Color base = withA(m_theme->panelBorder, 30);
        sf::Color hover = mix(base, withA(m_theme->accent, 90), hov);
        sf::Color fill = active ? mix(hover, withA(m_theme->accent, 150), 0.55f) : hover;

        sf::RectangleShape box({r.width, r.height});
        box.setPosition(ui::snap({r.left + offset.x + shake.x * 0.12f, r.top + offset.y}));
        box.setFillColor(fill);
        box.setOutlineThickness(1.f);
        box.setOutlineColor(withA(m_theme->panelBorder, active ? 190 : 110));
        rt.draw(box);

        sf::Sprite spr = spriteForPiece(pc);
        if (spr.getTexture())
        {
          const float scaleBump = active ? 1.04f : (1.0f + 0.03f * hov);
          spr.setScale(spr.getScale().x * scaleBump, spr.getScale().y * scaleBump);
          spr.setPosition(ui::snap({r.left + offset.x + shake.x * 0.12f + r.width * 0.5f,
                                    r.top + offset.y + r.height * 0.5f + m_pieceYOffset * 0.25f}));
          rt.draw(spr);
        }
      }
    }

    void drawPlayableStatus(sf::RenderTarget &rt, sf::Vector2f offset, sf::Vector2f shake) const
    {
      const bool ok = kingsOk();
      const sf::Color good = sf::Color(122, 205, 164);
      const sf::Color bad = sf::Color(200, 80, 95);

      sf::FloatRect r = {m_rightRect.left + 10.f,
                         m_rightRect.top + m_rightRect.height - 60.f,
                         m_rightRect.width - 20.f,
                         44.f};

      sf::RectangleShape outline({r.width, r.height});
      outline.setPosition(ui::snap({r.left + offset.x + shake.x * 0.12f, r.top + offset.y}));
      outline.setFillColor(sf::Color(0, 0, 0, 0));
      outline.setOutlineThickness(1.f);
      outline.setOutlineColor(withA(m_theme->panelBorder, 110));
      rt.draw(outline);

      sf::CircleShape ring(8.f);
      ring.setPosition(ui::snap({r.left + offset.x + shake.x * 0.12f + 10.f, r.top + offset.y + 14.f}));
      ring.setFillColor(sf::Color(0, 0, 0, 0));
      ring.setOutlineThickness(2.f);
      ring.setOutlineColor(ok ? good : bad);
      rt.draw(ring);

      sf::Text t(ok ? "Playable" : "Not playable", *m_font, 12);
      t.setFillColor(ok ? good : bad);
      t.setPosition(ui::snap({r.left + offset.x + shake.x * 0.12f + 34.f, r.top + offset.y + 9.f}));
      rt.draw(t);

      sf::Text sub(ok ? "Ready to use" : "Need 1 king each side", *m_font, 11);
      sub.setFillColor(m_theme->subtle);
      sub.setPosition(ui::snap({r.left + offset.x + shake.x * 0.12f + 34.f, r.top + offset.y + 25.f}));
      rt.draw(sub);
    }

    void drawShortcutsOverlay(sf::RenderTarget &rt, sf::Vector2f offset) const
    {
      sf::RectangleShape bg({m_shortcutsRect.width, m_shortcutsRect.height});
      bg.setPosition(ui::snap({m_shortcutsRect.left + offset.x, m_shortcutsRect.top + offset.y}));
      bg.setFillColor(withA(m_theme->panelBorder, 22));
      bg.setOutlineThickness(1.f);
      bg.setOutlineColor(withA(m_theme->panelBorder, 90));
      rt.draw(bg);

      sf::Text title("Shortcuts", *m_font, 12);
      title.setFillColor(m_theme->subtle);
      title.setPosition(ui::snap({m_shortcutsRect.left + offset.x + 10.f, m_shortcutsRect.top + offset.y + 8.f}));
      rt.draw(title);

      struct Item
      {
        const char *k;
        const char *v;
      };

      // ASCII-only keys/labels to avoid "[][][]" glyph issues.
      static constexpr std::array<Item, 10> items{{
          {"A", "Add (pawn)"},
          {"M", "Move"},
          {"Tab", "Toggle color"},
          {"1-6", "Pieces"},
          {"W", "White"},
          {"B", "Black"},
          {"RMB", "Delete square"},
          {"C", "Clear"},
          {"R", "Reset"},
          {"Shift", "Black pieces"},
      }};

      const float left = m_shortcutsRect.left + offset.x + 10.f;
      const float top = m_shortcutsRect.top + offset.y + 28.f;
      const float maxW = m_shortcutsRect.width - 20.f;

      float x = left;
      float y = top;

      const float lineH = 18.f;
      const float gapX = 18.f;

      for (const auto &it : items)
      {
        const float w = shortcutItemWidthPx(it.k, it.v);
        if (x - left + w > maxW)
        {
          x = left;
          y += lineH;
        }

        // If we still overflow vertically (very small window), stop drawing instead of clipping.
        if (y + lineH > (m_shortcutsRect.top + offset.y + m_shortcutsRect.height - 8.f))
          break;

        drawShortcutItem(rt, {x, y}, it.k, it.v);
        x += (w + gapX);
      }
    }

    void drawBoard(sf::RenderTarget &rt, sf::Vector2f offset, sf::Vector2f shake) const
    {
      sf::RectangleShape boardFrame({m_boardRect.width, m_boardRect.height});
      boardFrame.setPosition(ui::snap({m_boardRect.left + offset.x + shake.x, m_boardRect.top + offset.y}));
      boardFrame.setFillColor(sf::Color::Transparent);
      boardFrame.setOutlineThickness(1.f);
      boardFrame.setOutlineColor(m_theme->panelBorder);
      rt.draw(boardFrame);

      const bool previewing = (!m_dragging && m_selected.kind == ToolKind::Piece && m_hoverSquare.has_value());

      for (int y = 0; y < 8; ++y)
      {
        for (int x = 0; x < 8; ++x)
        {
          const bool dark = ((x + y) % 2) == 1;
          sf::Sprite sq = dark ? m_sqBlack : m_sqWhite;
          sq.setPosition(ui::snap({m_boardRect.left + offset.x + shake.x + x * m_sq,
                                   m_boardRect.top + offset.y + y * m_sq}));
          rt.draw(sq);

          const char p = at(x, y);
          if (p != '.')
          {
            bool dimUnder = false;
            if (previewing && m_hoverSquare)
            {
              auto [hx, hy] = *m_hoverSquare;
              dimUnder = (hx == x && hy == y);
            }
            drawPiece(rt, offset, shake, x, y, p, dimUnder);
          }
        }
      }

      if (m_hoverSquare)
      {
        auto [hx, hy] = *m_hoverSquare;

        sf::RectangleShape h({m_sq, m_sq});
        h.setPosition(ui::snap({m_boardRect.left + offset.x + shake.x + hx * m_sq,
                                m_boardRect.top + offset.y + hy * m_sq}));
        h.setFillColor(sf::Color(255, 255, 255, 0));
        h.setOutlineThickness(2.f);
        h.setOutlineColor(sf::Color(255, 255, 255, 90));
        rt.draw(h);

        if (!m_dragging && m_selected.kind == ToolKind::Piece)
        {
          if (at(hx, hy) != '.')
          {
            sf::RectangleShape shade({m_sq, m_sq});
            shade.setPosition(ui::snap({m_boardRect.left + offset.x + shake.x + hx * m_sq,
                                        m_boardRect.top + offset.y + hy * m_sq}));
            shade.setFillColor(sf::Color(0, 0, 0, 70));
            rt.draw(shade);
          }

          sf::Sprite ghost = spriteForPiece(m_selected.pieceChar);
          if (ghost.getTexture())
          {
            const bool illegal = wouldViolateKingUniqueness(hx, hy, m_selected.pieceChar);

            sf::Sprite shadow = ghost;
            shadow.setColor(sf::Color(0, 0, 0, 120));
            shadow.setPosition(ui::snap({m_boardRect.left + offset.x + shake.x + hx * m_sq + m_sq * 0.5f + 2.f,
                                         m_boardRect.top + offset.y + hy * m_sq + m_sq * 0.5f + m_pieceYOffset + 3.f}));
            rt.draw(shadow);

            ghost.setColor(illegal ? sf::Color(255, 120, 120, 200) : sf::Color(255, 255, 255, 220));
            ghost.setPosition(ui::snap({m_boardRect.left + offset.x + shake.x + hx * m_sq + m_sq * 0.5f,
                                        m_boardRect.top + offset.y + hy * m_sq + m_sq * 0.5f + m_pieceYOffset}));
            rt.draw(ghost);
          }
        }
      }

      if (m_dragging && m_dragPiece != '.')
      {
        sf::Sprite ghost = spriteForPiece(m_dragPiece);
        if (ghost.getTexture())
        {
          sf::Sprite shadow = ghost;
          shadow.setColor(sf::Color(0, 0, 0, 130));
          shadow.setPosition(ui::snap({m_mouseGlobal.x + 2.f, m_mouseGlobal.y + m_pieceYOffset + 3.f}));
          rt.draw(shadow);

          ghost.setColor(sf::Color(255, 255, 255, 230));
          ghost.setPosition(ui::snap({m_mouseGlobal.x, m_mouseGlobal.y + m_pieceYOffset}));
          rt.draw(ghost);
        }
      }
    }

    void drawPiece(sf::RenderTarget &rt, sf::Vector2f offset, sf::Vector2f shake,
                   int x, int y, char p, bool dimUnder) const
    {
      sf::Sprite spr = spriteForPiece(p);
      if (!spr.getTexture())
        return;

      spr.setPosition(ui::snap({m_boardRect.left + offset.x + shake.x + x * m_sq + m_sq * 0.5f,
                                m_boardRect.top + offset.y + y * m_sq + m_sq * 0.5f + m_pieceYOffset}));

      if (dimUnder)
        spr.setColor(sf::Color(255, 255, 255, 85));

      rt.draw(spr);
    }

    void drawErrorToast(sf::RenderTarget &rt, sf::Vector2f offset) const
    {
      const float a = std::clamp(m_errorT / m_errorDur, 0.f, 1.f);
      const float w = std::min(520.f, m_boardRect.width * 0.92f);
      const float h = 44.f;

      sf::FloatRect r{m_boardRect.left + (m_boardRect.width - w) * 0.5f,
                      m_boardRect.top - 54.f,
                      w, h};

      sf::RectangleShape box({r.width, r.height});
      box.setPosition(ui::snap({r.left + offset.x, r.top + offset.y}));
      box.setFillColor(sf::Color(200, 70, 70, sf::Uint8(180 * a)));
      box.setOutlineThickness(1.f);
      box.setOutlineColor(sf::Color(0, 0, 0, sf::Uint8(80 * a)));
      rt.draw(box);

      sf::Text t(m_errorMsg, *m_font, 12);
      t.setFillColor(sf::Color(255, 255, 255, sf::Uint8(255 * a)));
      t.setPosition(ui::snap({r.left + offset.x + 10.f, r.top + offset.y + 6.f}));
      rt.draw(t);
    }

    static void centerText(sf::Text &t, const sf::FloatRect &r)
    {
      auto b = t.getLocalBounds();
      t.setOrigin(b.left + b.width * 0.5f, b.top + b.height * 0.5f);
      t.setPosition(ui::snap({r.left + r.width * 0.5f, r.top + r.height * 0.5f}));
    }

    // ---------- fen helpers ----------
    char at(int x, int y) const { return m_board[y][x]; }
    void set(int x, int y, char p) { m_board[y][x] = p; }

    void setFromFen(const std::string &fenStr, bool remember)
    {
      clear(false);

      auto sp = fenStr.find(' ');
      std::string placementStr = (sp == std::string::npos) ? fenStr : fenStr.substr(0, sp);

      int x = 0, y = 0;
      for (char c : placementStr)
      {
        if (c == '/')
        {
          ++y;
          x = 0;
          continue;
        }
        if (std::isdigit(static_cast<unsigned char>(c)))
        {
          x += (c - '0');
          continue;
        }
        if (x >= 0 && x < 8 && y >= 0 && y < 8)
        {
          m_board[y][x] = c;
          ++x;
        }
      }

      refreshKingCounts();
      if (remember)
        rememberCurrent();
    }

    std::string placement() const
    {
      std::string out;
      for (int y = 0; y < 8; ++y)
      {
        int empties = 0;
        for (int x = 0; x < 8; ++x)
        {
          char p = at(x, y);
          if (p == '.')
          {
            ++empties;
            continue;
          }
          if (empties)
          {
            out.push_back(char('0' + empties));
            empties = 0;
          }
          out.push_back(p);
        }
        if (empties)
          out.push_back(char('0' + empties));
        if (y != 7)
          out.push_back('/');
      }
      return out;
    }
  };

} // namespace lilia::view
