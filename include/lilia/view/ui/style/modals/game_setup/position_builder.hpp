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
      // Always initialize to start position.
      resetToStart(false);

      // If the user had a previous builder position, restore it (requested behavior).
      if (!s_lastFen.empty())
        setFromFen(s_lastFen, false);
    }

    void onOpen()
    {
      // Called when the builder is opened again.
      // If user already built something before, restore; otherwise startpos.
      if (!s_lastFen.empty())
        setFromFen(s_lastFen, false);
      else
        resetToStart(false);
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

    // Two deletion options
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

    // Returns a FEN string (always), even if kings are missing.
    std::string fen() const { return placement() + " w - - 0 1"; }

    // For GameSetup "Use Position":
    // Return empty if invalid by builder rules (exactly 1 king per color required).
    std::string fenForUse() const
    {
      if (!kingsOk())
        return {};
      return fen();
    }

    // For UI feedback
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
      m_hoverRight = hitRight(local);
      m_hoverPiece = hitPiece(local);
    }

    bool handleEvent(const sf::Event &e, sf::Vector2f mouse, sf::Vector2f offset = {})
    {
      if (!m_theme || !m_font)
        return false;

      m_mouseGlobal = mouse;
      m_offset = offset;
      sf::Vector2f local = {mouse.x - offset.x, mouse.y - offset.y};

      // Keyboard tools
      if (e.type == sf::Event::KeyPressed)
      {
        const bool shift = e.key.shift;

        if (e.key.code == sf::Keyboard::Tab)
        {
          m_placeWhite = !m_placeWhite;
          if (m_selected.kind == ToolKind::Piece)
            m_selected.pieceChar = applyColorToPieceType(m_selected.pieceChar, m_placeWhite);
          return true;
        }

        if (e.key.code == sf::Keyboard::M)
        {
          m_selected = ToolSelection::move();
          return true;
        }

        if (e.key.code == sf::Keyboard::X || e.key.code == sf::Keyboard::BackSpace ||
            e.key.code == sf::Keyboard::Delete)
        {
          m_selected = ToolSelection::erase();
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
          const bool white = shift ? false : m_placeWhite;
          placed = applyColorToPieceType(placed, white);
          m_selected = ToolSelection::make_piece(placed);
          return true;
        }
      }

      // Right click clears square
      if (e.type == sf::Event::MouseButtonPressed && e.mouseButton.button == sf::Mouse::Right)
      {
        auto sq = squareFromMouse(local);
        if (!sq)
          return false;
        auto [x, y] = *sq;
        trySet(x, y, '.', true);
        return true;
      }

      // Left click
      if (e.type == sf::Event::MouseButtonPressed && e.mouseButton.button == sf::Mouse::Left)
      {
        // Side panel buttons (left)
        if (auto id = hitLeft(local))
        {
          onLeftButton(*id);
          return true;
        }

        // Side panel buttons (right non-piece)
        if (auto id = hitRight(local))
        {
          onRightButton(*id);
          return true;
        }

        // Piece buttons (right)
        if (auto p = hitPiece(local))
        {
          m_selected = ToolSelection::make_piece(*p);
          m_placeWhite = std::isupper(static_cast<unsigned char>(*p));
          return true;
        }

        // Board click
        auto sq = squareFromMouse(local);
        if (!sq)
          return false;

        auto [x, y] = *sq;

        if (m_selected.kind == ToolKind::Erase)
        {
          trySet(x, y, '.', true);
          return true;
        }

        if (m_selected.kind == ToolKind::Piece)
        {
          if (!trySet(x, y, m_selected.pieceChar, true))
            invalidAction("Kings must be unique per color.\nUse Move tool to reposition an existing king.");
          return true;
        }

        // Move tool -> drag
        char p = at(x, y);
        if (p != '.')
        {
          m_dragging = true;
          m_dragPiece = p;
          m_dragFrom = std::make_pair(x, y);
          // Temporarily remove from board
          set(x, y, '.');
          refreshKingCounts(); // kings may go to 0 temporarily while dragging
          return true;
        }

        return false;
      }

      // Left release: drop dragged piece
      if (e.type == sf::Event::MouseButtonReleased && e.mouseButton.button == sf::Mouse::Left)
      {
        if (!m_dragging)
          return false;

        m_dragging = false;

        auto sq = squareFromMouse(local);
        if (sq)
        {
          auto [tx, ty] = *sq;

          // Restore and validate king uniqueness
          if (!trySet(tx, ty, m_dragPiece, true))
          {
            // If invalid drop (e.g., second king), restore to origin if possible
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
          // Restore to origin when dropped outside
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

      // --- animate (self-contained; no external dt required) ---
      const float dt = clampDt(m_animClock.restart().asSeconds());
      animate(dt);

      // background
      sf::RectangleShape panel({m_bounds.width, m_bounds.height});
      panel.setPosition(ui::snap({m_bounds.left + offset.x, m_bounds.top + offset.y}));
      panel.setFillColor(m_theme->panel);
      panel.setOutlineThickness(1.f);
      panel.setOutlineColor(m_theme->panelBorder);
      rt.draw(panel);

      // board shake feedback
      sf::Vector2f shake = {0.f, 0.f};
      if (m_shakeT > 0.f)
      {
        const float a = (m_shakeT / m_shakeDur);
        const float s = std::sin(m_shakePhase) * (6.f * a);
        shake.x = s;
      }

      // side panels
      drawSidePanel(rt, offset, m_leftRect, "Tools", true, shake);
      drawSidePanel(rt, offset, m_rightRect, "Pieces", false, shake);

      // board
      drawBoard(rt, offset, shake);

      // error toast
      if (m_errorT > 0.f)
        drawErrorToast(rt, offset);
    }

  private:
    // persisted between openings
    static inline std::string s_lastFen{};

    const ui::Theme *m_theme{nullptr};
    const sf::Font *m_font{nullptr};

    sf::FloatRect m_bounds{};

    // layout
    sf::FloatRect m_boardRect{};
    sf::FloatRect m_leftRect{};
    sf::FloatRect m_rightRect{};
    float m_sq{44.f};
    float m_pieceYOffset{0.f};

    // board
    std::array<std::array<char, 8>, 8> m_board{};

    // king counts
    int m_whiteKings{0};
    int m_blackKings{0};

    // tool selection
    enum class ToolKind
    {
      Move,
      Erase,
      Piece
    };

    struct ToolSelection
    {
      ToolKind kind{ToolKind::Move};
      char pieceChar{'.'};

      static ToolSelection move() { return ToolSelection{ToolKind::Move, '.'}; }
      static ToolSelection erase() { return ToolSelection{ToolKind::Erase, '.'}; }
      static ToolSelection make_piece(char p) { return ToolSelection{ToolKind::Piece, p}; }
    };

    ToolSelection m_selected = ToolSelection::move();
    bool m_placeWhite{true};

    // dragging
    bool m_dragging{false};
    char m_dragPiece{'.'};
    std::optional<std::pair<int, int>> m_dragFrom{};

    // hover
    mutable sf::Vector2f m_mouseGlobal{};
    mutable sf::Vector2f m_offset{};
    std::optional<std::pair<int, int>> m_hoverSquare{};

    // --- UI buttons (simple, builder-internal) ---
    enum class LeftBtn
    {
      White,
      Black,
      Move,
      Erase,
      Clear,
      Start
    };

    enum class RightBtn
    {
      None
    };

    // button rects
    sf::FloatRect m_btnWhite{};
    sf::FloatRect m_btnBlack{};
    sf::FloatRect m_btnMove{};
    sf::FloatRect m_btnErase{};
    sf::FloatRect m_btnClear{};
    sf::FloatRect m_btnStart{};

    // piece buttons (always shown for both colors)
    // order: P,B,N,R,Q,K (white row), p,b,n,r,q,k (black row)
    std::array<sf::FloatRect, 12> m_pieceBtns{};

    // hovered ids (for animation)
    std::optional<LeftBtn> m_hoverLeft{};
    std::optional<RightBtn> m_hoverRight{};
    std::optional<char> m_hoverPiece{};

    // animation states (0..1)
    mutable float m_hvWhite{0}, m_hvBlack{0}, m_hvMove{0}, m_hvErase{0}, m_hvClear{0}, m_hvStart{0};
    mutable std::array<float, 12> m_hvPiece{};

    // feedback
    mutable sf::Clock m_animClock{};
    mutable float m_shakeT{0.f};
    mutable float m_shakeDur{0.18f};
    mutable float m_shakePhase{0.f};

    mutable float m_errorT{0.f};
    mutable float m_errorDur{1.1f};
    mutable std::string m_errorMsg{};

    // textures
    mutable bool m_texReady{false};
    mutable const sf::Texture *m_texWhite{nullptr};
    mutable const sf::Texture *m_texBlack{nullptr};
    mutable std::array<const sf::Texture *, 12> m_pieceTex{};
    mutable sf::Sprite m_sqWhite{};
    mutable sf::Sprite m_sqBlack{};
    mutable std::array<sf::Sprite, 12> m_pieceTpl{};

    // ---------- helpers ----------
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
      // exponential-ish smoothing (k ~ dt*speed)
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
      // shake
      if (m_shakeT > 0.f)
      {
        m_shakeT = std::max(0.f, m_shakeT - dt);
        m_shakePhase += dt * 55.f;
      }

      // error fade
      if (m_errorT > 0.f)
        m_errorT = std::max(0.f, m_errorT - dt);

      // hovers
      const float k = dt * 12.f;

      m_hvWhite = approach(m_hvWhite, (m_hoverLeft && *m_hoverLeft == LeftBtn::White) ? 1.f : 0.f, k);
      m_hvBlack = approach(m_hvBlack, (m_hoverLeft && *m_hoverLeft == LeftBtn::Black) ? 1.f : 0.f, k);
      m_hvMove = approach(m_hvMove, (m_hoverLeft && *m_hoverLeft == LeftBtn::Move) ? 1.f : 0.f, k);
      m_hvErase = approach(m_hvErase, (m_hoverLeft && *m_hoverLeft == LeftBtn::Erase) ? 1.f : 0.f, k);
      m_hvClear = approach(m_hvClear, (m_hoverLeft && *m_hoverLeft == LeftBtn::Clear) ? 1.f : 0.f, k);
      m_hvStart = approach(m_hvStart, (m_hoverLeft && *m_hoverLeft == LeftBtn::Start) ? 1.f : 0.f, k);

      for (int i = 0; i < 12; ++i)
      {
        const char pc = pieceCharFromIndex(i);
        const bool hov = (m_hoverPiece && *m_hoverPiece == pc);
        m_hvPiece[i] = approach(m_hvPiece[i], hov ? 1.f : 0.f, k);
      }
    }

    static char pieceCharFromIndex(int idx)
    {
      static constexpr std::array<char, 12> pieces{
          'P', 'B', 'N', 'R', 'Q', 'K',
          'p', 'b', 'n', 'r', 'q', 'k'};
      return pieces[idx];
    }

    void rememberCurrent()
    {
      s_lastFen = fen();
    }

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

      // if placing K/k onto a square already containing same king, that's fine
      const char old = at(x, y);
      if (old == newP)
        return false;

      // count existing kings of that color elsewhere
      int count = 0;
      for (int yy = 0; yy < 8; ++yy)
        for (int xx = 0; xx < 8; ++xx)
        {
          if (xx == x && yy == y)
            continue;
          if (at(xx, yy) == newP)
            ++count;
        }

      // if there is already one king, placing another is illegal
      return (count >= 1);
    }

    // returns false if blocked by king uniqueness rule
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

    // ---------- geometry ----------
    void rebuildGeometry()
    {
      if (m_bounds.width <= 0.f || m_bounds.height <= 0.f)
        return;

      const float pad = 14.f;
      const float gap = 14.f;

      // side panels
      float leftW = std::clamp(m_bounds.width * 0.16f, 120.f, 170.f);
      float rightW = std::clamp(m_bounds.width * 0.22f, 170.f, 240.f);

      float availW = m_bounds.width - pad * 2.f - leftW - rightW - gap * 2.f;
      float availH = m_bounds.height - pad * 2.f;

      float boardSize = std::min(availW, availH);
      boardSize = std::max(240.f, boardSize); // keep usable

      m_sq = boardSize / 8.f;
      m_pieceYOffset = m_sq * 0.03f;

      // center board in the available "middle band"
      const float midLeft = m_bounds.left + pad + leftW + gap;
      const float midRight = m_bounds.left + m_bounds.width - pad - rightW - gap;
      const float midW = (midRight - midLeft);

      const float boardLeft = midLeft + (midW - boardSize) * 0.5f;
      const float boardTop = m_bounds.top + pad + (availH - boardSize) * 0.5f;

      m_boardRect = {boardLeft, boardTop, boardSize, boardSize};

      // side panel rects aligned to board
      m_leftRect = {m_bounds.left + pad, boardTop, leftW, boardSize};
      m_rightRect = {m_boardRect.left + m_boardRect.width + gap, boardTop, rightW, boardSize};

      // left buttons layout
      {
        const float x = m_leftRect.left + 10.f;
        float y = m_leftRect.top + 34.f;
        const float w = m_leftRect.width - 20.f;
        const float h = 34.f;
        const float g = 10.f;

        // color row (two half buttons)
        const float half = (w - 8.f) * 0.5f;
        m_btnWhite = {x, y, half, h};
        m_btnBlack = {x + half + 8.f, y, half, h};
        y += (h + g);

        m_btnMove = {x, y, w, h};
        y += (h + g);
        m_btnErase = {x, y, w, h};
        y += (h + g);

        y += 8.f;

        m_btnClear = {x, y, w, h};
        y += (h + g);
        m_btnStart = {x, y, w, h};
      }

      // right piece grid + status
      {
        const float padR = 10.f;
        const float top = m_rightRect.top + 34.f;
        const float left = m_rightRect.left + padR;
        const float w = m_rightRect.width - padR * 2.f;

        const float cellGap = 10.f;
        const float cols = 3.f;
        const float cell = std::floor((w - cellGap * (cols - 1.f)) / cols);

        // two rows white + black (3x2 each row -> 6 per row? we need 6 pieces per color; use 3 cols x 2 rows = 6)
        auto rectAt = [&](int col, int row, float baseY) -> sf::FloatRect
        {
          return {left + col * (cell + cellGap), baseY + row * (cell + cellGap), cell, cell};
        };

        // White pieces block (2 rows)
        float y0 = top;
        m_pieceBtns[0] = rectAt(0, 0, y0); // P
        m_pieceBtns[1] = rectAt(1, 0, y0); // // B
        m_pieceBtns[2] = rectAt(2, 0, y0); // N
        m_pieceBtns[3] = rectAt(0, 1, y0); // R
        m_pieceBtns[4] = rectAt(1, 1, y0); // Q
        m_pieceBtns[5] = rectAt(2, 1, y0); // K

        // Black pieces block below
        float y1 = y0 + 2.f * cell + cellGap + 16.f;
        m_pieceBtns[6] = rectAt(0, 0, y1);  // p
        m_pieceBtns[7] = rectAt(1, 0, y1);  // b
        m_pieceBtns[8] = rectAt(2, 0, y1);  // n
        m_pieceBtns[9] = rectAt(0, 1, y1);  // r
        m_pieceBtns[10] = rectAt(1, 1, y1); // q
        m_pieceBtns[11] = rectAt(2, 1, y1); // k
      }

      m_texReady = false; // rescale sprites
      refreshKingCounts();
    }

    // ---------- texture helpers ----------
    static int typeIndexFromLower(char lower)
    {
      // Must match your PieceManager asset indexing:
      //   idx = PieceType + 6 * Color
      // Adjust if your PieceType enum order differs.
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

      // Board squares
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

      // Pieces
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

    // ---------- input mapping ----------
    static char applyColorToPieceType(char lowerPiece, bool white)
    {
      const char l = char(std::tolower(static_cast<unsigned char>(lowerPiece)));
      return white ? char(std::toupper(static_cast<unsigned char>(l))) : l;
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
      if (m_btnMove.contains(p))
        return LeftBtn::Move;
      if (m_btnErase.contains(p))
        return LeftBtn::Erase;
      if (m_btnClear.contains(p))
        return LeftBtn::Clear;
      if (m_btnStart.contains(p))
        return LeftBtn::Start;
      return std::nullopt;
    }

    std::optional<RightBtn> hitRight(sf::Vector2f) const
    {
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
          m_selected.pieceChar = applyColorToPieceType(m_selected.pieceChar, true);
        break;
      case LeftBtn::Black:
        m_placeWhite = false;
        if (m_selected.kind == ToolKind::Piece)
          m_selected.pieceChar = applyColorToPieceType(m_selected.pieceChar, false);
        break;
      case LeftBtn::Move:
        m_selected = ToolSelection::move();
        break;
      case LeftBtn::Erase:
        m_selected = ToolSelection::erase();
        break;
      case LeftBtn::Clear:
        clear(true);
        break;
      case LeftBtn::Start:
        resetToStart(true);
        break;
      }
    }

    void onRightButton(RightBtn) {}

    // ---------- draw ----------
    void drawSidePanel(sf::RenderTarget &rt, sf::Vector2f offset, const sf::FloatRect &r,
                       const std::string &title, bool left, sf::Vector2f shake) const
    {
      sf::RectangleShape box({r.width, r.height});
      box.setPosition(ui::snap({r.left + offset.x + shake.x * (left ? 0.25f : 0.15f),
                                r.top + offset.y}));
      box.setFillColor(withA(m_theme->panelBorder, 35));
      box.setOutlineThickness(1.f);
      box.setOutlineColor(withA(m_theme->panelBorder, 90));
      rt.draw(box);

      sf::Text t(title, *m_font, 14);
      t.setFillColor(m_theme->text);
      t.setPosition(ui::snap({r.left + offset.x + 10.f + shake.x * (left ? 0.25f : 0.15f),
                              r.top + offset.y + 8.f}));
      rt.draw(t);

      if (left)
      {
        drawLeftButtons(rt, offset, shake);
      }
      else
      {
        drawPieceButtons(rt, offset, shake);
        drawValidation(rt, offset, shake);
      }
    }

    void drawLeftButtons(sf::RenderTarget &rt, sf::Vector2f offset, sf::Vector2f shake) const
    {
      // active states
      const bool whiteActive = m_placeWhite;
      const bool blackActive = !m_placeWhite;
      const bool moveActive = (m_selected.kind == ToolKind::Move);
      const bool eraseActive = (m_selected.kind == ToolKind::Erase);

      drawButton(rt, offset, m_btnWhite, "White", whiteActive, m_hvWhite, shake);
      drawButton(rt, offset, m_btnBlack, "Black", blackActive, m_hvBlack, shake);
      drawButton(rt, offset, m_btnMove, "Move (M)", moveActive, m_hvMove, shake);
      drawButton(rt, offset, m_btnErase, "Erase (X)", eraseActive, m_hvErase, shake);
      drawButton(rt, offset, m_btnClear, "Clear", false, m_hvClear, shake);
      drawButton(rt, offset, m_btnStart, "Start", false, m_hvStart, shake);

      sf::Text hint("Hotkeys: 1..6 pieces | Tab color\nRight-click: erase square", *m_font, 12);
      hint.setFillColor(m_theme->subtle);
      hint.setPosition(ui::snap({m_leftRect.left + offset.x + 10.f + shake.x * 0.25f,
                                 m_leftRect.top + offset.y + m_leftRect.height - 38.f}));
      rt.draw(hint);
    }

    void drawPieceButtons(sf::RenderTarget &rt, sf::Vector2f offset, sf::Vector2f shake) const
    {
      for (int i = 0; i < 12; ++i)
      {
        const char pc = pieceCharFromIndex(i);
        const bool active = (m_selected.kind == ToolKind::Piece && m_selected.pieceChar == pc);

        sf::FloatRect r = m_pieceBtns[i];
        float hov = m_hvPiece[i];

        // button background
        sf::Color base = withA(m_theme->panelBorder, 35);
        sf::Color hover = mix(base, withA(m_theme->accent, 80), hov);
        sf::Color fill = active ? mix(hover, withA(m_theme->accent, 120), 0.6f) : hover;

        sf::RectangleShape box({r.width, r.height});
        box.setPosition(ui::snap({r.left + offset.x + shake.x * 0.15f, r.top + offset.y}));
        box.setFillColor(fill);
        box.setOutlineThickness(1.f);
        box.setOutlineColor(withA(m_theme->panelBorder, active ? 180 : 110));
        rt.draw(box);

        // piece sprite
        sf::Sprite spr = spriteForPiece(pc);
        if (spr.getTexture())
        {
          const float scaleBump = active ? 1.04f : (1.0f + 0.03f * hov);
          spr.setScale(spr.getScale().x * scaleBump, spr.getScale().y * scaleBump);
          spr.setPosition(ui::snap({r.left + offset.x + shake.x * 0.15f + r.width * 0.5f,
                                    r.top + offset.y + r.height * 0.5f + m_pieceYOffset * 0.25f}));
          rt.draw(spr);
        }
      }

      sf::Text labelW("White", *m_font, 12);
      labelW.setFillColor(m_theme->subtle);
      labelW.setPosition(ui::snap({m_rightRect.left + offset.x + 10.f + shake.x * 0.15f,
                                   m_rightRect.top + offset.y + 18.f}));
      rt.draw(labelW);

      sf::Text labelB("Black", *m_font, 12);
      labelB.setFillColor(m_theme->subtle);
      // place above the black grid
      labelB.setPosition(ui::snap({m_pieceBtns[6].left + offset.x + shake.x * 0.15f,
                                   m_pieceBtns[6].top + offset.y - 16.f}));
      rt.draw(labelB);
    }

    void drawValidation(sf::RenderTarget &rt, sf::Vector2f offset, sf::Vector2f shake) const
    {
      const std::string s = "Kings: W " + std::to_string(m_whiteKings) + "/1, B " +
                            std::to_string(m_blackKings) + "/1";
      const bool ok = kingsOk();

      sf::FloatRect r = {m_rightRect.left + 10.f, m_rightRect.top + m_rightRect.height - 46.f,
                         m_rightRect.width - 20.f, 32.f};

      sf::RectangleShape pill({r.width, r.height});
      pill.setPosition(ui::snap({r.left + offset.x + shake.x * 0.15f, r.top + offset.y}));
      pill.setFillColor(ok ? withA(m_theme->accent, 80) : sf::Color(200, 70, 70, 90));
      pill.setOutlineThickness(1.f);
      pill.setOutlineColor(withA(m_theme->panelBorder, 140));
      rt.draw(pill);

      sf::Text t(s, *m_font, 12);
      t.setFillColor(m_theme->text);
      t.setPosition(ui::snap({r.left + offset.x + 10.f + shake.x * 0.15f, r.top + offset.y + 8.f}));
      rt.draw(t);
    }

    void drawBoard(sf::RenderTarget &rt, sf::Vector2f offset, sf::Vector2f shake) const
    {
      // frame
      sf::RectangleShape boardFrame({m_boardRect.width, m_boardRect.height});
      boardFrame.setPosition(ui::snap({m_boardRect.left + offset.x + shake.x, m_boardRect.top + offset.y}));
      boardFrame.setFillColor(sf::Color::Transparent);
      boardFrame.setOutlineThickness(1.f);
      boardFrame.setOutlineColor(m_theme->panelBorder);
      rt.draw(boardFrame);

      // squares + pieces
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
            drawPiece(rt, offset, shake, x, y, p);
        }
      }

      // hover outline + ghost
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
          sf::Sprite ghost = spriteForPiece(m_selected.pieceChar);
          if (ghost.getTexture())
          {
            // pre-check king uniqueness for ghost: show red tint if illegal
            const bool illegal = wouldViolateKingUniqueness(hx, hy, m_selected.pieceChar);
            ghost.setColor(illegal ? sf::Color(255, 120, 120, 150) : sf::Color(255, 255, 255, 140));
            ghost.setPosition(ui::snap({m_boardRect.left + offset.x + shake.x + hx * m_sq + m_sq * 0.5f,
                                        m_boardRect.top + offset.y + hy * m_sq + m_sq * 0.5f + m_pieceYOffset}));
            rt.draw(ghost);
          }
        }
      }

      // drag ghost
      if (m_dragging && m_dragPiece != '.')
      {
        sf::Sprite ghost = spriteForPiece(m_dragPiece);
        if (ghost.getTexture())
        {
          ghost.setColor(sf::Color(255, 255, 255, 180));
          ghost.setPosition(ui::snap({m_mouseGlobal.x, m_mouseGlobal.y + m_pieceYOffset}));
          rt.draw(ghost);
        }
      }
    }

    void drawPiece(sf::RenderTarget &rt, sf::Vector2f offset, sf::Vector2f shake,
                   int x, int y, char p) const
    {
      sf::Sprite spr = spriteForPiece(p);
      if (!spr.getTexture())
        return;

      spr.setPosition(ui::snap({m_boardRect.left + offset.x + shake.x + x * m_sq + m_sq * 0.5f,
                                m_boardRect.top + offset.y + y * m_sq + m_sq * 0.5f + m_pieceYOffset}));
      rt.draw(spr);
    }

    void drawButton(sf::RenderTarget &rt, sf::Vector2f offset, const sf::FloatRect &r,
                    const std::string &label, bool active, float hoverT, sf::Vector2f shake) const
    {
      sf::Color base = withA(m_theme->panelBorder, 35);
      sf::Color hov = mix(base, withA(m_theme->accent, 80), hoverT);
      sf::Color fill = active ? mix(hov, withA(m_theme->accent, 130), 0.7f) : hov;

      sf::RectangleShape box({r.width, r.height});
      box.setPosition(ui::snap({r.left + offset.x + shake.x * 0.25f, r.top + offset.y}));
      box.setFillColor(fill);
      box.setOutlineThickness(1.f);
      box.setOutlineColor(withA(m_theme->panelBorder, active ? 180 : 110));
      rt.draw(box);

      sf::Text t(label, *m_font, 12);
      t.setFillColor(m_theme->text);
      centerText(t, {r.left + offset.x + shake.x * 0.25f, r.top + offset.y, r.width, r.height});
      rt.draw(t);
    }

    void drawErrorToast(sf::RenderTarget &rt, sf::Vector2f offset) const
    {
      // anchored top-center of board
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

    void setFromFen(const std::string &fen, bool remember)
    {
      clear(false);

      auto sp = fen.find(' ');
      std::string placementStr = (sp == std::string::npos) ? fen : fen.substr(0, sp);

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
