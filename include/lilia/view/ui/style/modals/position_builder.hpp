#pragma once

#include <SFML/Graphics.hpp>
#include <algorithm>
#include <array>
#include <cctype>
#include <optional>
#include <string>

#include "lilia/constants.hpp"
#include "../style.hpp"
#include "../theme.hpp"

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
    void setFont(const sf::Font *f) { m_font = f; }
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
    }

    void updateHover(sf::Vector2f mouse, sf::Vector2f offset = {})
    {
      m_mouseGlobal = mouse;
      m_offset = offset;
      sf::Vector2f local = {mouse.x - offset.x, mouse.y - offset.y};
      m_hoverSquare = squareFromMouse(local);
    }

    bool handleEvent(const sf::Event &e, sf::Vector2f mouse, sf::Vector2f offset = {})
    {
      if (!m_theme || !m_font)
        return false;

      m_mouseGlobal = mouse;
      m_offset = offset;

      sf::Vector2f local = {mouse.x - offset.x, mouse.y - offset.y};

      // Drag/drop (ruleless)
      if (e.type == sf::Event::MouseButtonPressed && e.mouseButton.button == sf::Mouse::Left)
      {
        if (!m_bounds.contains(local))
          return false;
        auto sq = squareFromMouse(local);
        if (!sq)
          return false;
        auto [fx, fy] = *sq;
        char p = at(fx, fy);
        if (p != '.')
        {
          m_dragging = true;
          m_dragPiece = p;
          set(fx, fy, '.');
          return true;
        }
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
          set(tx, ty, m_dragPiece);
        }
        m_dragPiece = '.';
        return true;
      }

      // Right click clears
      if (e.type == sf::Event::MouseButtonPressed && e.mouseButton.button == sf::Mouse::Right)
      {
        if (!m_bounds.contains(local))
          return false;
        auto sq = squareFromMouse(local);
        if (!sq)
          return false;
        auto [x, y] = *sq;
        set(x, y, '.');
        return true;
      }

      // Hotkeys 1..6 place piece on hovered square; Tab toggles color
      if (e.type == sf::Event::KeyPressed)
      {
        if (e.key.code == sf::Keyboard::Tab)
        {
          m_placeWhite = !m_placeWhite;
          return true;
        }

        auto sq = m_hoverSquare;
        if (!sq)
          return false;

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
        if (placed == '.')
          return false;

        if (m_placeWhite)
          placed = char(std::toupper(unsigned(placed)));

        auto [x, y] = *sq;
        set(x, y, placed);
        return true;
      }

      return false;
    }

    std::string fen() const { return placement() + " w - - 0 1"; } // placeholder fields

    void draw(sf::RenderTarget &rt, sf::Vector2f offset = {}) const
    {
      if (!m_theme || !m_font)
        return;

      // Outer
      sf::RectangleShape bg({m_sq * 8.f, m_sq * 8.f});
      bg.setPosition(ui::snap({m_bounds.left + offset.x, m_bounds.top + offset.y}));
      bg.setFillColor(m_theme->panel);
      bg.setOutlineThickness(1.f);
      bg.setOutlineColor(m_theme->panelBorder);
      rt.draw(bg);

      // Squares + pieces
      for (int y = 0; y < 8; ++y)
      {
        for (int x = 0; x < 8; ++x)
        {
          sf::RectangleShape sqr({m_sq, m_sq});
          sqr.setPosition(
              ui::snap({m_bounds.left + offset.x + x * m_sq, m_bounds.top + offset.y + y * m_sq}));
          bool dark = ((x + y) % 2) == 1;
          sqr.setFillColor(dark ? ui::darken(m_theme->panel, 14) : ui::lighten(m_theme->panel, 14));
          rt.draw(sqr);

          char p = at(x, y);
          if (p != '.')
            drawPiece(rt, offset, x, y, p);
        }
      }

      // Hover outline
      if (m_hoverSquare)
      {
        auto [hx, hy] = *m_hoverSquare;
        sf::RectangleShape h({m_sq, m_sq});
        h.setPosition(
            ui::snap({m_bounds.left + offset.x + hx * m_sq, m_bounds.top + offset.y + hy * m_sq}));
        h.setFillColor(sf::Color::Transparent);
        h.setOutlineThickness(2.f);
        h.setOutlineColor(m_theme->accent);
        rt.draw(h);
      }

      // Drag ghost
      if (m_dragging && m_dragPiece != '.')
      {
        sf::CircleShape ghost(m_sq * 0.32f);
        ghost.setOrigin(ghost.getRadius(), ghost.getRadius());
        ghost.setPosition(ui::snap(m_mouseGlobal));
        sf::Color c = std::isupper(unsigned(m_dragPiece)) ? ui::lighten(m_theme->text, 40)
                                                          : ui::darken(m_theme->text, 20);
        c.a = 180;
        ghost.setFillColor(c);
        rt.draw(ghost);
      }

      // Status below (local -> global)
      sf::Text status(m_placeWhite ? "Place: White (Tab toggles)" : "Place: Black (Tab toggles)",
                      *m_font, 14);
      status.setFillColor(m_theme->subtle);
      status.setPosition(
          ui::snap({m_bounds.left + offset.x, m_bounds.top + offset.y + m_sq * 8.f + 8.f}));
      rt.draw(status);
    }

  private:
    const ui::Theme *m_theme{nullptr};
    const sf::Font *m_font{nullptr};

    sf::FloatRect m_bounds{};
    float m_sq{44.f};

    std::array<std::array<char, 8>, 8> m_board{};

    bool m_placeWhite{true};

    bool m_dragging{false};
    char m_dragPiece{'.'};

    mutable sf::Vector2f m_mouseGlobal{};
    mutable sf::Vector2f m_offset{};
    std::optional<std::pair<int, int>> m_hoverSquare{};

    void rebuildGeometry()
    {
      if (m_bounds.width <= 0 || m_bounds.height <= 0)
        return;
      m_sq = std::min(m_bounds.width, m_bounds.height) / 8.f;
    }

    char at(int x, int y) const { return m_board[y][x]; }
    void set(int x, int y, char p) { m_board[y][x] = p; }

    std::optional<std::pair<int, int>> squareFromMouse(sf::Vector2f localMouse) const
    {
      if (!m_bounds.contains(localMouse))
        return std::nullopt;
      int x = int((localMouse.x - m_bounds.left) / m_sq);
      int y = int((localMouse.y - m_bounds.top) / m_sq);
      if (x < 0 || x > 7 || y < 0 || y > 7)
        return std::nullopt;
      return std::make_pair(x, y);
    }

    void drawPiece(sf::RenderTarget &rt, sf::Vector2f offset, int x, int y, char p) const
    {
      sf::CircleShape disc(m_sq * 0.34f);
      disc.setOrigin(disc.getRadius(), disc.getRadius());
      disc.setPosition(ui::snap({m_bounds.left + offset.x + x * m_sq + m_sq * 0.5f,
                                 m_bounds.top + offset.y + y * m_sq + m_sq * 0.5f}));
      bool white = std::isupper(unsigned(p));
      disc.setFillColor(white ? ui::lighten(m_theme->text, 60) : ui::darken(m_theme->text, 40));
      rt.draw(disc);

      sf::Text t(std::string(1, char(std::toupper(unsigned(p)))), *m_font, unsigned(m_sq * 0.42f));
      t.setFillColor(white ? sf::Color(20, 20, 20) : sf::Color(240, 240, 240));
      ui::centerText(
          t, {disc.getPosition().x - m_sq * 0.5f, disc.getPosition().y - m_sq * 0.5f, m_sq, m_sq},
          -2.f);
      rt.draw(t);
    }

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
        if (std::isdigit(unsigned(c)))
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
