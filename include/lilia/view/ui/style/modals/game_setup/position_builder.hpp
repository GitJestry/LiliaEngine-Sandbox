#pragma once

#include <SFML/Graphics.hpp>
#include <SFML/Window/Keyboard.hpp>

#include <algorithm>
#include <array>
#include <cctype>
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
    void setTheme(const ui::Theme *t)
    {
      m_theme = t;
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

    void resetToStart()
    {
      clear();
      setFromFen(core::START_FEN);
    }

    void clear()
    {
      for (auto &r : m_board)
        r.fill('.');
      m_dragging = false;
      m_dragPiece = '.';
      m_dragFrom.reset();
    }

    void updateHover(sf::Vector2f mouse, sf::Vector2f offset = {})
    {
      m_mouseGlobal = mouse;
      m_offset = offset;

      sf::Vector2f local = {mouse.x - offset.x, mouse.y - offset.y};
      m_hoverSquare = squareFromMouse(local);
      m_hoverPalette = paletteCellFromMouse(local);
    }

    // Tooling:
    //   * Palette click: Move / Erase / choose piece (white or black)
    //   * M: Move tool
    //   * X / Backspace / Delete: Erase tool
    //   * 1..6: Pawn, Bishop, Knight, Rook, Queen, King (uses current color; Tab flips)
    //   * Tab: toggles current color (also swaps selected piece color when a piece is selected)
    //   * Right click: erase square
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

        // Hotkeys 1..6 select piece type; Shift selects black, otherwise current color.
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

      // Right click clears (board only)
      if (e.type == sf::Event::MouseButtonPressed && e.mouseButton.button == sf::Mouse::Right)
      {
        auto sq = squareFromMouse(local);
        if (!sq)
          return false;
        auto [x, y] = *sq;
        set(x, y, '.');
        return true;
      }

      // Left click: palette selection OR board interaction
      if (e.type == sf::Event::MouseButtonPressed && e.mouseButton.button == sf::Mouse::Left)
      {
        // Palette click has priority
        if (auto cell = paletteCellFromMouse(local))
        {
          setSelectedFromPalette(*cell);
          return true;
        }

        // Board click
        auto sq = squareFromMouse(local);
        if (!sq)
          return false;

        auto [x, y] = *sq;

        if (m_selected.kind == ToolKind::Erase)
        {
          set(x, y, '.');
          return true;
        }

        if (m_selected.kind == ToolKind::Piece)
        {
          set(x, y, m_selected.pieceChar);
          return true;
        }

        // Move tool -> drag existing
        char p = at(x, y);
        if (p != '.')
        {
          m_dragging = true;
          m_dragPiece = p;
          m_dragFrom = std::make_pair(x, y);
          set(x, y, '.');
          return true;
        }

        return false;
      }

      // Left release: drop dragged piece (if any)
      if (e.type == sf::Event::MouseButtonReleased && e.mouseButton.button == sf::Mouse::Left)
      {
        if (!m_dragging)
          return false;

        m_dragging = false;

        auto sq = squareFromMouse(local);
        if (sq)
        {
          auto [tx, ty] = *sq;
          set(tx, ty, m_dragPiece);
        }
        else if (m_dragFrom)
        {
          // Professional behavior: if dropped outside, restore to origin.
          auto [ox, oy] = *m_dragFrom;
          set(ox, oy, m_dragPiece);
        }

        m_dragPiece = '.';
        m_dragFrom.reset();
        return true;
      }

      return false;
    }

    std::string fen() const { return placement() + " w - - 0 1"; } // placeholder fields

    void draw(sf::RenderTarget &rt, sf::Vector2f offset = {}) const
    {
      if (!m_theme || !m_font)
        return;

      ensureTextures();

      // Panel background
      sf::RectangleShape panel({m_bounds.width, m_bounds.height});
      panel.setPosition(ui::snap({m_bounds.left + offset.x, m_bounds.top + offset.y}));
      panel.setFillColor(m_theme->panel);
      panel.setOutlineThickness(1.f);
      panel.setOutlineColor(m_theme->panelBorder);
      rt.draw(panel);

      // Board frame
      sf::RectangleShape boardFrame({m_boardRect.width, m_boardRect.height});
      boardFrame.setPosition(ui::snap({m_boardRect.left + offset.x, m_boardRect.top + offset.y}));
      boardFrame.setFillColor(sf::Color::Transparent);
      boardFrame.setOutlineThickness(1.f);
      boardFrame.setOutlineColor(m_theme->panelBorder);
      rt.draw(boardFrame);

      // Squares + pieces
      for (int y = 0; y < 8; ++y)
      {
        for (int x = 0; x < 8; ++x)
        {
          const bool dark = ((x + y) % 2) == 1;

          sf::Sprite sq = dark ? m_sqBlack : m_sqWhite;
          sq.setPosition(
              ui::snap({m_boardRect.left + offset.x + x * m_sq, m_boardRect.top + offset.y + y * m_sq}));
          rt.draw(sq);

          const char p = at(x, y);
          if (p != '.')
            drawPiece(rt, offset, x, y, p);
        }
      }

      // Hover outline (board)
      if (m_hoverSquare)
      {
        auto [hx, hy] = *m_hoverSquare;
        sf::RectangleShape h({m_sq, m_sq});
        h.setPosition(ui::snap(
            {m_boardRect.left + offset.x + hx * m_sq, m_boardRect.top + offset.y + hy * m_sq}));
        h.setFillColor(sf::Color(255, 255, 255, 0));
        h.setOutlineThickness(2.f);
        h.setOutlineColor(sf::Color(255, 255, 255, 90));
        rt.draw(h);

        // Preview placement (if tool is piece)
        if (!m_dragging && m_selected.kind == ToolKind::Piece)
        {
          sf::Sprite ghost = spriteForPiece(m_selected.pieceChar);
          if (ghost.getTexture())
          {
            ghost.setColor(sf::Color(255, 255, 255, 140));
            ghost.setPosition(ui::snap({m_boardRect.left + offset.x + hx * m_sq + m_sq * 0.5f,
                                        m_boardRect.top + offset.y + hy * m_sq + m_sq * 0.5f +
                                            m_pieceYOffset}));
            rt.draw(ghost);
          }
        }
      }

      // Drag ghost
      if (m_dragging && m_dragPiece != '.')
      {
        sf::Sprite ghost = spriteForPiece(m_dragPiece);
        if (ghost.getTexture())
        {
          ghost.setColor(sf::Color(255, 255, 255, 170));
          ghost.setPosition(ui::snap({m_mouseGlobal.x, m_mouseGlobal.y + m_pieceYOffset}));
          rt.draw(ghost);
        }
      }

      // Palette (optional – shows if bounds allow it)
      if (m_hasPalette)
        drawPalette(rt, offset);
    }

  private:
    const ui::Theme *m_theme{nullptr};
    const sf::Font *m_font{nullptr};

    sf::FloatRect m_bounds{};
    float m_sq{44.f};

    // Board and palette are laid out inside bounds
    sf::FloatRect m_boardRect{};
    sf::FloatRect m_paletteRect{};
    bool m_hasPalette{false};

    // Palette cell sizing
    float m_palTile{44.f};
    float m_palGap{6.f};

    // Piece sprite vertical tweak
    float m_pieceYOffset{0.f};

    std::array<std::array<char, 8>, 8> m_board{};

    bool m_placeWhite{true};

    // Drag state
    bool m_dragging{false};
    char m_dragPiece{'.'};
    std::optional<std::pair<int, int>> m_dragFrom{};

    // Hover state
    mutable sf::Vector2f m_mouseGlobal{};
    mutable sf::Vector2f m_offset{};
    std::optional<std::pair<int, int>> m_hoverSquare{};
    std::optional<std::pair<int, int>> m_hoverPalette{}; // (col,row)

    // Textures / sprites (cache)
    mutable bool m_texReady{false};
    mutable const sf::Texture *m_texWhite{nullptr};
    mutable const sf::Texture *m_texBlack{nullptr};
    mutable std::array<const sf::Texture *, 12> m_pieceTex{};
    mutable sf::Sprite m_sqWhite{};
    mutable sf::Sprite m_sqBlack{};
    mutable std::array<sf::Sprite, 12> m_pieceTpl{};

    // Tool selection
    enum class ToolKind
    {
      Move,
      Erase,
      Piece
    };

    struct ToolSelection
    {
      ToolKind kind{ToolKind::Move};
      char pieceChar{'.'}; // valid only if kind==Piece

      static ToolSelection move() { return ToolSelection{ToolKind::Move, '.'}; }
      static ToolSelection erase() { return ToolSelection{ToolKind::Erase, '.'}; }
      static ToolSelection make_piece(char p) { return ToolSelection{ToolKind::Piece, p}; }
    };

    ToolSelection m_selected = ToolSelection::move();

    // -------- geometry --------
    void rebuildGeometry()
    {
      if (m_bounds.width <= 0.f || m_bounds.height <= 0.f)
        return;

      // First pass: assume no palette, board is square anchored top-left.
      float boardW = std::min(m_bounds.width, m_bounds.height);
      float sq = boardW / 8.f;

      // Estimate palette size from this square size (kept readable).
      const float tile = std::clamp(sq * 0.85f, 32.f, 56.f);
      const float gap = 6.f;

      // Palette grid: 7 cols x 2 rows
      const float palW = tile * 7.f + gap * 6.f + 12.f; // padding
      const float palH = tile * 2.f + gap * 1.f + 12.f;

      // Try right-side palette first
      const float sideGap = 12.f;
      bool placeRight = (m_bounds.width >= (sq * 8.f + sideGap + palW));

      // Try bottom palette if right doesn't fit
      bool placeBottom = (!placeRight) && (m_bounds.height >= (sq * 8.f + sideGap + palH));

      if (placeRight)
      {
        boardW = std::min(m_bounds.height, m_bounds.width - sideGap - palW);
        sq = boardW / 8.f;

        m_boardRect = {m_bounds.left, m_bounds.top, sq * 8.f, sq * 8.f};
        m_paletteRect = {m_boardRect.left + m_boardRect.width + sideGap, m_bounds.top,
                         m_bounds.width - (m_boardRect.width + sideGap), m_boardRect.height};
        m_hasPalette = true;
      }
      else if (placeBottom)
      {
        boardW = std::min(m_bounds.width, m_bounds.height - sideGap - palH);
        sq = boardW / 8.f;

        m_boardRect = {m_bounds.left, m_bounds.top, sq * 8.f, sq * 8.f};
        m_paletteRect = {m_bounds.left, m_boardRect.top + m_boardRect.height + sideGap,
                         m_bounds.width, m_bounds.height - (m_boardRect.height + sideGap)};
        m_hasPalette = true;
      }
      else
      {
        // No palette, maximize board
        boardW = std::min(m_bounds.width, m_bounds.height);
        sq = boardW / 8.f;
        m_boardRect = {m_bounds.left, m_bounds.top, sq * 8.f, sq * 8.f};
        m_paletteRect = {};
        m_hasPalette = false;
      }

      m_sq = sq;
      m_palTile = std::clamp(m_sq * 0.85f, 32.f, 56.f);
      m_palGap = 6.f;

      m_pieceYOffset = m_sq * 0.03f;

      // Refresh cached sprite scales when geometry changes
      m_texReady = false;
    }

    // -------- texture helpers --------
    static int typeIndexFromLower(char lower)
    {
      // IMPORTANT:
      // This mapping must match your piece asset ordering used by PieceManager:
      //   index = PieceType + 6 * Color
      // If your enum/asset order differs, adjust this mapping accordingly.
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

      // Board squares use the same texture keys as BoardView.
      m_texWhite = &TextureTable::getInstance().get(std::string{constant::tex::WHITE});
      m_texBlack = &TextureTable::getInstance().get(std::string{constant::tex::BLACK});

      // Prepare square sprites scaled to current square size.
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

      // Piece textures (12)
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

        // Centered origin for robust scaling
        const auto ts = t.getSize();
        spr.setOrigin(float(ts.x) * 0.5f, float(ts.y) * 0.5f);

        // Fit to square
        const float target = m_sq * 0.90f;
        const float scale = (ts.y > 0) ? (target / float(ts.y)) : 1.f;
        spr.setScale(scale, scale);

        m_pieceTpl[slot] = spr;
      };

      // White pieces
      load('P');
      load('B');
      load('N');
      load('R');
      load('Q');
      load('K');
      // Black pieces
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
      return m_pieceTpl[slot]; // copy
    }

    // -------- input mapping --------
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

    // Palette cell mapping: 7 cols x 2 rows inside palette rect, with padding.
    std::optional<std::pair<int, int>> paletteCellFromMouse(sf::Vector2f localMouse) const
    {
      if (!m_hasPalette)
        return std::nullopt;

      const float pad = 6.f;
      const float gridW = m_palTile * 7.f + m_palGap * 6.f;
      const float gridH = m_palTile * 2.f + m_palGap * 1.f;

      const float gx = m_paletteRect.left + (m_paletteRect.width - gridW) * 0.5f;
      const float gy = m_paletteRect.top + pad;

      sf::FloatRect grid{gx, gy, gridW, gridH};
      if (!grid.contains(localMouse))
        return std::nullopt;

      const float relX = localMouse.x - grid.left;
      const float relY = localMouse.y - grid.top;

      for (int row = 0; row < 2; ++row)
      {
        const float y0 = row * (m_palTile + m_palGap);
        if (relY < y0 || relY > y0 + m_palTile)
          continue;

        for (int col = 0; col < 7; ++col)
        {
          const float x0 = col * (m_palTile + m_palGap);
          if (relX < x0 || relX > x0 + m_palTile)
            continue;
          return std::make_pair(col, row);
        }
      }

      return std::nullopt;
    }

    void setSelectedFromPalette(std::pair<int, int> cell)
    {
      // Layout:
      // col 0: Move (row 0), Erase (row 1)
      // col 1..6: Pawn,Bishop,Knight,Rook,Queen,King for White row0 / Black row1
      const int col = cell.first;
      const int row = cell.second;

      if (col == 0)
      {
        m_selected = (row == 0) ? ToolSelection::move() : ToolSelection::erase();
        return;
      }

      static constexpr std::array<char, 6> typesLower{'p', 'b', 'n', 'r', 'q', 'k'};
      const int idx = col - 1;
      if (idx < 0 || idx >= 6)
        return;

      const bool white = (row == 0);
      m_placeWhite = white;
      const char piece = applyColorToPieceType(typesLower[idx], white);
      m_selected = ToolSelection::make_piece(piece);
    }

    // -------- draw helpers --------
    char at(int x, int y) const { return m_board[y][x]; }
    void set(int x, int y, char p) { m_board[y][x] = p; }

    void drawPiece(sf::RenderTarget &rt, sf::Vector2f offset, int x, int y, char p) const
    {
      sf::Sprite spr = spriteForPiece(p);
      if (spr.getTexture())
      {
        spr.setPosition(ui::snap({m_boardRect.left + offset.x + x * m_sq + m_sq * 0.5f,
                                  m_boardRect.top + offset.y + y * m_sq + m_sq * 0.5f + m_pieceYOffset}));
        rt.draw(spr);
      }
    }

    void drawPalette(sf::RenderTarget &rt, sf::Vector2f offset) const
    {
      sf::RectangleShape card({m_paletteRect.width, m_paletteRect.height});
      card.setPosition(ui::snap({m_paletteRect.left + offset.x, m_paletteRect.top + offset.y}));
      card.setFillColor(ui::darken(m_theme->panel, 6));
      card.setOutlineThickness(1.f);
      card.setOutlineColor(m_theme->panelBorder);
      rt.draw(card);

      sf::Text title("Tools", *m_font, 14);
      title.setFillColor(m_theme->text);
      title.setPosition(ui::snap({m_paletteRect.left + offset.x + 10.f, m_paletteRect.top + offset.y + 8.f}));
      rt.draw(title);

      const float gridW = m_palTile * 7.f + m_palGap * 6.f;
      const float gx = m_paletteRect.left + (m_paletteRect.width - gridW) * 0.5f;
      const float gy = m_paletteRect.top + 28.f;

      auto cellRect = [&](int col, int row) -> sf::FloatRect
      {
        return {gx + col * (m_palTile + m_palGap), gy + row * (m_palTile + m_palGap), m_palTile, m_palTile};
      };

      for (int row = 0; row < 2; ++row)
      {
        for (int col = 0; col < 7; ++col)
        {
          sf::FloatRect r = cellRect(col, row);

          sf::RectangleShape box({r.width, r.height});
          box.setPosition(ui::snap({r.left + offset.x, r.top + offset.y}));
          box.setFillColor(ui::lighten(m_theme->panel, 6));
          box.setOutlineThickness(1.f);
          box.setOutlineColor(ui::darken(m_theme->panelBorder, 30));
          rt.draw(box);

          if (col == 0)
          {
            sf::Text t((row == 0) ? "Move" : "Erase", *m_font, 12);
            t.setFillColor(m_theme->subtle);
            ui::centerText(t, {r.left + offset.x, r.top + offset.y, r.width, r.height}, 0.f);
            rt.draw(t);
          }
          else
          {
            static constexpr std::array<char, 6> typesLower{'p', 'b', 'n', 'r', 'q', 'k'};
            const int idx = col - 1;
            const char piece = applyColorToPieceType(typesLower[idx], row == 0);

            sf::Sprite spr = spriteForPiece(piece);
            if (spr.getTexture())
            {
              spr.setPosition(ui::snap({r.left + offset.x + r.width * 0.5f,
                                        r.top + offset.y + r.height * 0.5f + m_pieceYOffset * 0.4f}));
              spr.setScale(spr.getScale().x * 0.82f, spr.getScale().y * 0.82f);
              rt.draw(spr);
            }
          }
        }
      }

      sf::Text hint("Hotkeys: M Move | X Erase | 1..6 piece | Tab toggles color", *m_font, 12);
      hint.setFillColor(m_theme->subtle);
      hint.setPosition(ui::snap({m_paletteRect.left + offset.x + 10.f,
                                 m_paletteRect.top + offset.y + m_paletteRect.height - 18.f}));
      rt.draw(hint);
    }

    // -------- fen helpers --------
    void setFromFen(const std::string &fen)
    {
      clear();
      auto sp = fen.find(' ');
      std::string placement = (sp == std::string::npos) ? fen : fen.substr(0, sp);

      int x = 0, y = 0;
      for (char c : placement)
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
