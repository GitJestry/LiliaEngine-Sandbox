#include "lilia/view/ui/views/board_view.hpp"

#include <SFML/Graphics.hpp>
#include <algorithm>
#include <cmath>
#include <limits>

#include "lilia/view/ui/style/palette_cache.hpp"
#include "lilia/view/ui/render/render_constants.hpp"
#include "lilia/view/ui/render/texture_table.hpp"
#include "lilia/view/ui/style/style.hpp"

namespace lilia::view
{

  namespace
  {

    void drawRadialShadow(sf::RenderTarget &t, sf::Vector2f center, float radius, sf::Color shadow)
    {
      const float squash = 0.75f;
      const int layers = 8;
      const float step = 1.8f;
      const float alpha0 = 48.f;

      for (int i = 0; i < layers; ++i)
      {
        float R = radius + i * step;
        sf::CircleShape s(R);
        s.setOrigin(R, R);
        s.setPosition(ui::snapf(center.x), ui::snapf(center.y + radius * 0.35f));
        s.setScale(1.f, squash);

        sf::Color c = shadow;
        c.a = static_cast<sf::Uint8>(std::max(0.f, alpha0 * (1.f - i / float(layers))));
        s.setFillColor(c);

        t.draw(s);
      }
    }

    const sf::Font *tooltipFont()
    {
      static sf::Font s_font;
      static bool s_loaded = false;
      if (!s_loaded)
      {
        s_loaded = s_font.loadFromFile(std::string{constant::path::FONT});
        if (s_loaded)
          s_font.setSmooth(false);
      }
      return s_loaded ? &s_font : nullptr;
    }

    void drawTooltip(sf::RenderWindow &win, const sf::Vector2f center, std::string_view label,
                     sf::Color textCol, sf::Color bg, sf::Color border, sf::Color shadow)
    {
      const sf::Font *f = tooltipFont();
      if (!f)
        return;

      constexpr float padX = 8.f, padY = 5.f, arrowH = 6.f;

      sf::Text t(std::string{label}, *f, 12);
      t.setFillColor(textCol);

      auto b = t.getLocalBounds();
      const float w = b.width + 2.f * padX;
      const float h = b.height + 2.f * padY;
      const float x = ui::snapf(center.x - w * 0.5f);
      const float y = ui::snapf(center.y - h - arrowH - 6.f);

      sf::RectangleShape sh({w, h});
      sh.setPosition(x + 2.f, y + 2.f);
      sh.setFillColor(shadow);
      win.draw(sh);

      sf::RectangleShape body({w, h});
      body.setPosition(x, y);
      body.setFillColor(bg);
      body.setOutlineThickness(1.f);
      body.setOutlineColor(border);
      win.draw(body);

      sf::ConvexShape arrow(3);
      arrow.setPoint(0, {center.x - 6.f, y + h});
      arrow.setPoint(1, {center.x + 6.f, y + h});
      arrow.setPoint(2, {center.x, y + h + arrowH});
      arrow.setFillColor(bg);
      win.draw(arrow);

      t.setPosition(ui::snapf(x + padX - b.left), ui::snapf(y + padY - b.top));
      win.draw(t);
    }

    void drawFlipIcon(sf::RenderWindow &win, const sf::FloatRect &slot, bool hovered,
                      PaletteCRef pal)
    {
      const float size = std::min(slot.width, slot.height);
      const float cx = slot.left + slot.width * 0.5f;
      const float cy = slot.top + slot.height * 0.5f;

      drawRadialShadow(win, {cx, cy}, size * 0.48f, pal[ColorId::COL_SHADOW_MEDIUM]);

      const sf::Color discCol = hovered ? pal[ColorId::COL_DISC_HOVER] : pal[ColorId::COL_DISC];
      const sf::Color discOutline = hovered ? pal[ColorId::COL_ACCENT_OUTLINE] : pal[ColorId::COL_BORDER];

      const float R = size * 0.50f;
      sf::CircleShape disc(R);
      disc.setOrigin(R, R);
      disc.setPosition(ui::snapf(cx), ui::snapf(cy));
      disc.setFillColor(discCol);
      disc.setOutlineThickness(1.f);
      disc.setOutlineColor(discOutline);
      win.draw(disc);

      sf::CircleShape topHL(R - 1.f);
      topHL.setOrigin(R - 1.f, R - 1.f);
      topHL.setPosition(ui::snapf(cx), ui::snapf(cy));
      topHL.setFillColor(sf::Color::Transparent);
      topHL.setOutlineThickness(1.f);
      topHL.setOutlineColor(ui::lighten(discCol, 16));
      win.draw(topHL);

      sf::CircleShape bottomSH(R - 2.f);
      bottomSH.setOrigin(R - 2.f, R - 2.f);
      bottomSH.setPosition(ui::snapf(cx), ui::snapf(cy));
      bottomSH.setFillColor(sf::Color::Transparent);
      bottomSH.setOutlineThickness(1.f);
      bottomSH.setOutlineColor(ui::darken(discCol, 18));
      win.draw(bottomSH);

      const sf::Color ico = hovered ? pal[ColorId::COL_ACCENT_HOVER] : pal[ColorId::COL_TEXT];

      const float ringR = size * 0.34f;
      sf::CircleShape ring(ringR);
      ring.setOrigin(ringR, ringR);
      ring.setPosition(ui::snapf(cx), ui::snapf(cy));
      ring.setFillColor(sf::Color::Transparent);
      ring.setOutlineThickness(2.f);
      ring.setOutlineColor(ico);
      win.draw(ring);

      const float triS = size * 0.22f;

      // upper-right arrow
      {
        const float ax = cx + ringR * 0.85f;
        const float ay = cy - ringR * 0.85f;
        sf::ConvexShape a(3);
        a.setPoint(0, {ax + triS * 0.00f, ay - triS * 0.55f});
        a.setPoint(1, {ax + triS * 0.42f, ay - triS * 0.30f});
        a.setPoint(2, {ax + triS * 0.06f, ay - triS * 0.05f});
        a.setFillColor(ico);
        win.draw(a);
      }
      // lower-left arrow
      {
        const float bx = cx - ringR * 0.85f;
        const float by = cy + ringR * 0.85f;
        sf::ConvexShape a(3);
        a.setPoint(0, {bx - triS * 0.00f, by + triS * 0.55f});
        a.setPoint(1, {bx - triS * 0.42f, by + triS * 0.30f});
        a.setPoint(2, {bx - triS * 0.06f, by + triS * 0.05f});
        a.setFillColor(ico);
        win.draw(a);
      }
    }

  } // namespace

  BoardView::BoardView()
      : m_board({constant::WINDOW_PX_SIZE / 2, constant::WINDOW_PX_SIZE / 2}),
        m_flip_pos(),
        m_flip_size(0.f),
        m_flipped(false) {}

  void BoardView::init()
  {
    const auto pal = PaletteCache::get().palette();

    m_board.init(TextureTable::getInstance().get(std::string{constant::tex::WHITE}),
                 TextureTable::getInstance().get(std::string{constant::tex::BLACK}),
                 TextureTable::getInstance().get(std::string{constant::tex::TRANSPARENT}),
                 pal[ColorId::COL_BOARD_OUTLINE]);

    setPosition(getPosition());
  }

  void BoardView::renderBoard(sf::RenderWindow &window)
  {
    const auto pal = PaletteCache::get().palette();

    // Only labels need rebuilding on palette changes (square textures are updated in-place by TextureTable).
    m_board.setLabelOutline(pal[ColorId::COL_BOARD_OUTLINE]);

    m_board.draw(window);

    sf::Vector2i mousePx = sf::Mouse::getPosition(window);
    sf::Vector2f mouse = window.mapPixelToCoords(mousePx);

    sf::FloatRect slot(m_flip_pos.x - m_flip_size / 2.f, m_flip_pos.y - m_flip_size / 2.f,
                       m_flip_size, m_flip_size);
    bool hovered = slot.contains(mouse.x, mouse.y);

    drawFlipIcon(window, slot, hovered, pal);

    if (hovered)
    {
      const float cx = slot.left + slot.width * 0.5f;
      const float cy = slot.top + slot.height * 0.5f;

      drawTooltip(window, {cx, cy}, "Flip board",
                  pal[ColorId::COL_TEXT],
                  pal[ColorId::COL_TOOLTIP_BG],
                  pal[ColorId::COL_BORDER],
                  pal[ColorId::COL_SHADOW_LIGHT]);
    }
  }

  Entity::Position BoardView::getSquareScreenPos(core::Square sq) const
  {
    if (m_flipped)
    {
      return m_board.getPosOfSquare(
          static_cast<core::Square>(constant::BOARD_SIZE * constant::BOARD_SIZE - 1 - sq));
    }
    return m_board.getPosOfSquare(sq);
  }

  void BoardView::toggleFlipped()
  {
    m_flipped = !m_flipped;
    m_board.setFlipped(m_flipped);
  }

  void BoardView::setFlipped(bool flipped)
  {
    m_flipped = flipped;
    m_board.setFlipped(m_flipped);
  }

  bool BoardView::isFlipped() const { return m_flipped; }

  void BoardView::setPosition(const Entity::Position &pos)
  {
    m_board.setPosition(pos);

    float iconOffset = constant::SQUARE_PX_SIZE * 0.2f;
    m_flip_size = constant::SQUARE_PX_SIZE * 0.3f;
    m_flip_pos = {pos.x + constant::WINDOW_PX_SIZE / 2.f + iconOffset,
                  pos.y - constant::WINDOW_PX_SIZE / 2.f + 2.f - iconOffset};
  }

  Entity::Position BoardView::getPosition() const { return m_board.getPosition(); }

  bool BoardView::isOnFlipIcon(core::MousePos mousePos) const
  {
    float left = m_flip_pos.x - m_flip_size / 2.f;
    float right = m_flip_pos.x + m_flip_size / 2.f;
    float top = m_flip_pos.y - m_flip_size / 2.f;
    float bottom = m_flip_pos.y + m_flip_size / 2.f;
    return mousePos.x >= left && mousePos.x <= right && mousePos.y >= top && mousePos.y <= bottom;
  }

  namespace
  {
    static inline int normalizeUnsignedToSigned(unsigned int u)
    {
      if (u <= static_cast<unsigned int>(std::numeric_limits<int>::max()))
        return static_cast<int>(u);
      return -static_cast<int>((std::numeric_limits<unsigned int>::max() - u) + 1u);
    }
    constexpr int clampInt(int v, int lo, int hi) noexcept
    {
      return (v < lo) ? lo : (v > hi ? hi : v);
    }
  } // namespace

  core::MousePos BoardView::clampPosToBoard(core::MousePos mousePos,
                                            Entity::Position pieceSize) const noexcept
  {
    const int sx = normalizeUnsignedToSigned(mousePos.x);
    const int sy = normalizeUnsignedToSigned(mousePos.y);

    auto boardCenter = getPosition();
    const float halfW = pieceSize.x / 2.f;
    const float halfH = pieceSize.y / 2.f;

    const int left =
        static_cast<int>(boardCenter.x - float(constant::WINDOW_PX_SIZE) / 2.f + halfW);
    const int top =
        static_cast<int>(boardCenter.y - float(constant::WINDOW_PX_SIZE) / 2.f + halfH);
    const int right =
        static_cast<int>(boardCenter.x + float(constant::WINDOW_PX_SIZE) / 2.f - 1.f - halfW);
    const int bottom =
        static_cast<int>(boardCenter.y + float(constant::WINDOW_PX_SIZE) / 2.f - 1.f - halfH);

    const int cx = clampInt(sx, left, right);
    const int cy = clampInt(sy, top, bottom);

    return {static_cast<unsigned>(cx), static_cast<unsigned>(cy)};
  }

  core::Square BoardView::mousePosToSquare(core::MousePos mousePos) const
  {
    auto boardCenter = getPosition();
    float originX = boardCenter.x - float(constant::WINDOW_PX_SIZE) / 2.f;
    float originY = boardCenter.y - float(constant::WINDOW_PX_SIZE) / 2.f;
    float right = originX + float(constant::WINDOW_PX_SIZE);
    float bottom = originY + float(constant::WINDOW_PX_SIZE);

    if (mousePos.x < originX || mousePos.x >= right || mousePos.y < originY || mousePos.y >= bottom)
    {
      return core::NO_SQUARE;
    }

    int fileSFML = static_cast<int>((mousePos.x - originX) / constant::SQUARE_PX_SIZE);
    int rankSFML = static_cast<int>((mousePos.y - originY) / constant::SQUARE_PX_SIZE);

    int fileFromWhite;
    int rankFromWhite;
    if (isFlipped())
    {
      fileFromWhite = 7 - fileSFML;
      rankFromWhite = rankSFML;
    }
    else
    {
      fileFromWhite = fileSFML;
      rankFromWhite = 7 - rankSFML;
    }

    return static_cast<core::Square>(rankFromWhite * 8 + fileFromWhite);
  }

} // namespace lilia::view
