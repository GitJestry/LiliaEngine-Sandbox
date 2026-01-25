#include "lilia/view/ui/render/scene/board_node.hpp"

#include <SFML/Graphics/Font.hpp>
#include <SFML/Graphics/RenderTexture.hpp>
#include <SFML/Graphics/RenderWindow.hpp>
#include <SFML/Graphics/Text.hpp>

#include <cmath>
#include <string>

#include "lilia/view/ui/render/render_constants.hpp"

namespace lilia::view
{

  static inline float snapf(float v) { return std::round(v); }
  static inline sf::Vector2f snapv(sf::Vector2f v) { return {snapf(v.x), snapf(v.y)}; }

  Board::Board(Entity::Position pos) : Entity(pos), m_flipped(false) {}

  Entity::Position Board::boardOffset() const
  {
    return {
        getPosition().x - constant::WINDOW_PX_SIZE / 2 + constant::SQUARE_PX_SIZE / 2,
        getPosition().y - constant::WINDOW_PX_SIZE / 2 + constant::SQUARE_PX_SIZE / 2,
    };
  }

  void Board::positionLabels(Entity::Position board_offset)
  {
    // FILE labels (a-h) bottom right
    for (int file = 0; file < constant::BOARD_SIZE; ++file)
    {
      auto &label = m_file_labels[m_flipped ? constant::BOARD_SIZE - 1 - file : file];
      auto size = label.getOriginalSize();
      float scale = (constant::SQUARE_PX_SIZE * 0.25f) / size.x;
      float w = size.x * scale;
      float h = size.y * scale;

      float cx = board_offset.x + file * constant::SQUARE_PX_SIZE;
      float cy = board_offset.y + (constant::BOARD_SIZE - 1 - 0) * constant::SQUARE_PX_SIZE;

      // UPDATED: snap to pixel grid to avoid “swimmy”/blurry label sprites
      label.setPosition(snapv(
          {cx + constant::SQUARE_PX_SIZE * 0.5f - w,
           cy + constant::SQUARE_PX_SIZE * 0.45f - h}));
    }

    // RANK labels (1-8) top left
    for (int rank = 0; rank < constant::BOARD_SIZE; ++rank)
    {
      auto &label = m_rank_labels[m_flipped ? constant::BOARD_SIZE - 1 - rank : rank];
      float cx = board_offset.x; // file 0
      float cy = board_offset.y + (constant::BOARD_SIZE - 1 - rank) * constant::SQUARE_PX_SIZE;

      // UPDATED: snap to pixel grid
      label.setPosition(snapv(
          {cx - constant::SQUARE_PX_SIZE * 0.5f,
           cy - constant::SQUARE_PX_SIZE * 0.45f}));
    }
  }

  void Board::rebuildLabelTextures(sf::Color outlineCol)
  {
    static sf::Font s_font;
    static bool s_loaded = false;
    if (!s_loaded)
    {
      s_loaded = s_font.loadFromFile(std::string{constant::path::FONT_DIR});
      if (s_loaded)
      {
        // UPDATED: was setSmooth(false) -> caused pixelated glyph edges/outlines.
        s_font.setSmooth(true);
      }
    }
    if (!s_loaded)
      return;

    constexpr float outlineThick = 4.f;
    constexpr int textSize = 55;

    // FILE labels (a-h)
    for (int file = 0; file < constant::BOARD_SIZE; ++file)
    {
      sf::Text t(std::string(1, static_cast<char>('a' + file)), s_font, textSize);
      t.setFillColor(sf::Color::Transparent);
      t.setOutlineColor(outlineCol);
      t.setOutlineThickness(outlineThick);

      auto bounds = t.getLocalBounds();

      sf::RenderTexture rt;
      const unsigned w = static_cast<unsigned>(std::max(1.f, bounds.width + 20.f));
      const unsigned h = static_cast<unsigned>(std::max(1.f, bounds.height + 20.f));
      if (!rt.create(w, h))
        continue;

      rt.clear(sf::Color::Transparent);

      t.setPosition(-bounds.left + 10.f, -bounds.top + 10.f);
      rt.draw(t);
      rt.display();

      m_file_textures[file] = rt.getTexture();
      // Keep smoothing enabled here; snapping positions above prevents “swimmy blur”.
      m_file_textures[file].setSmooth(true);

      m_file_labels[file].setTexture(m_file_textures[file]);

      auto size = m_file_labels[file].getOriginalSize();
      float scale = (constant::SQUARE_PX_SIZE * 0.25f) / size.x;
      m_file_labels[file].setScale(scale, scale);
    }

    // RANK labels (1-8)
    for (int rank = 0; rank < constant::BOARD_SIZE; ++rank)
    {
      sf::Text t(std::to_string(rank + 1), s_font, textSize);
      t.setFillColor(sf::Color::Transparent);
      t.setOutlineColor(outlineCol);
      t.setOutlineThickness(outlineThick);

      auto bounds = t.getLocalBounds();

      sf::RenderTexture rt;
      const unsigned w = static_cast<unsigned>(std::max(1.f, bounds.width + 20.f));
      const unsigned h = static_cast<unsigned>(std::max(1.f, bounds.height + 20.f));
      if (!rt.create(w, h))
        continue;

      rt.clear(sf::Color::Transparent);

      t.setPosition(-bounds.left + 10.f, -bounds.top + 10.f);
      rt.draw(t);
      rt.display();

      m_rank_textures[rank] = rt.getTexture();
      m_rank_textures[rank].setSmooth(true);

      m_rank_labels[rank].setTexture(m_rank_textures[rank]);

      auto size = m_rank_labels[rank].getOriginalSize();
      float scale = (constant::SQUARE_PX_SIZE * 0.25f) / size.x;
      m_rank_labels[rank].setScale(scale, scale);
    }
  }

  void Board::init(const sf::Texture &textureWhite, const sf::Texture &textureBlack,
                   const sf::Texture &textureBoard, sf::Color labelOutline)
  {
    setTexture(textureBoard);
    setScale(constant::WINDOW_PX_SIZE, constant::WINDOW_PX_SIZE);

    const auto off = boardOffset();

    for (int rank = 0; rank < constant::BOARD_SIZE; ++rank)
    {
      for (int file = 0; file < constant::BOARD_SIZE; ++file)
      {
        int index = file + rank * constant::BOARD_SIZE;

        float x = off.x + file * constant::SQUARE_PX_SIZE;
        float y = off.y + (constant::BOARD_SIZE - 1 - rank) * constant::SQUARE_PX_SIZE;

        m_squares[index].setPosition({x, y});
        m_squares[index].setTexture(((rank + file) % 2 == 0) ? textureBlack : textureWhite);
        m_squares[index].setScale(constant::SQUARE_PX_SIZE, constant::SQUARE_PX_SIZE);
        m_squares[index].setOriginToCenter();
      }
    }

    m_labelOutline = labelOutline;
    m_labelOutlineInit = true;
    rebuildLabelTextures(labelOutline);
    positionLabels(off);
  }

  void Board::setLabelOutline(sf::Color outline)
  {
    if (m_labelOutlineInit && outline == m_labelOutline)
      return;
    m_labelOutline = outline;
    m_labelOutlineInit = true;

    rebuildLabelTextures(outline);
    positionLabels(boardOffset());
  }

  Entity::Position Board::getPosOfSquare(core::Square sq) const
  {
    return m_squares[static_cast<size_t>(sq)].getPosition();
  }

  void Board::draw(sf::RenderWindow &window)
  {
    Entity::draw(window);

    for (auto &s : m_squares)
      s.draw(window);
    for (auto &l : m_file_labels)
      l.draw(window);
    for (auto &l : m_rank_labels)
      l.draw(window);
  }

  void Board::setPosition(const Entity::Position &pos)
  {
    Entity::setPosition(pos);
    const auto off = boardOffset();

    for (int rank = 0; rank < constant::BOARD_SIZE; ++rank)
    {
      for (int file = 0; file < constant::BOARD_SIZE; ++file)
      {
        int index = file + rank * constant::BOARD_SIZE;

        float x = off.x + file * constant::SQUARE_PX_SIZE;
        float y = off.y + (constant::BOARD_SIZE - 1 - rank) * constant::SQUARE_PX_SIZE;

        m_squares[index].setPosition({x, y});
      }
    }
    positionLabels(off);
  }

  void Board::setFlipped(bool flipped)
  {
    m_flipped = flipped;
    setPosition(getPosition());
  }

  bool Board::isFlipped() const
  {
    return m_flipped;
  }

} // namespace lilia::view
