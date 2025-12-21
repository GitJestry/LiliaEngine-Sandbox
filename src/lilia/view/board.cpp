#include "lilia/view/board.hpp"

#include <SFML/Graphics/Font.hpp>
#include <SFML/Graphics/RenderTexture.hpp>
#include <SFML/Graphics/RenderWindow.hpp>
#include <SFML/Graphics/Text.hpp>
#include <string>

#include "lilia/view/render_constants.hpp"

namespace lilia::view {

Board::Board(Entity::Position pos) : Entity(pos), m_flipped(false) {}

void Board::positionLabels(Entity::Position board_offset) {
  // FILE-Labels bottom right
  for (int file = 0; file < constant::BOARD_SIZE; ++file) {
    auto &label = m_file_labels[m_flipped ? constant::BOARD_SIZE - 1 - file : file];
    auto size = label.getOriginalSize();
    float scale = (constant::SQUARE_PX_SIZE * 0.25f) / size.x;
    float w = size.x * scale;
    float h = size.y * scale;

    float cx = board_offset.x + file * constant::SQUARE_PX_SIZE;
    float cy = board_offset.y + (constant::BOARD_SIZE - 1 - 0) * constant::SQUARE_PX_SIZE;

    label.setPosition(
        {cx + constant::SQUARE_PX_SIZE * 0.5f - w, cy + constant::SQUARE_PX_SIZE * 0.45f - h});
  }

  // RANK-Labels top left
  for (int rank = 0; rank < constant::BOARD_SIZE; ++rank) {
    auto &label = m_rank_labels[m_flipped ? constant::BOARD_SIZE - 1 - rank : rank];
    float cx = board_offset.x;  // file 0
    float cy = board_offset.y + (constant::BOARD_SIZE - 1 - rank) * constant::SQUARE_PX_SIZE;
    label.setPosition(
        {cx - constant::SQUARE_PX_SIZE * 0.5f, cy - constant::SQUARE_PX_SIZE * 0.45f});
  }
}

void Board::init(const sf::Texture &textureWhite, const sf::Texture &textureBlack,
                 const sf::Texture &textureBoard) {
  setTexture(textureBoard);
  setScale(constant::WINDOW_PX_SIZE, constant::WINDOW_PX_SIZE);

  sf::Vector2f board_offset(
      getPosition().x - constant::WINDOW_PX_SIZE / 2 + constant::SQUARE_PX_SIZE / 2,
      getPosition().y - constant::WINDOW_PX_SIZE / 2 + constant::SQUARE_PX_SIZE / 2);

  // build up squares
  for (int rank = 0; rank < constant::BOARD_SIZE; ++rank) {
    for (int file = 0; file < constant::BOARD_SIZE; ++file) {
      int index = file + rank * constant::BOARD_SIZE;

      float x = board_offset.x + file * constant::SQUARE_PX_SIZE;
      float y = board_offset.y + (constant::BOARD_SIZE - 1 - rank) * constant::SQUARE_PX_SIZE;

      m_squares[index].setPosition({x, y});
      if ((rank + file) % 2 == 0)
        m_squares[index].setTexture(textureBlack);
      else
        m_squares[index].setTexture(textureWhite);
      m_squares[index].setScale(constant::SQUARE_PX_SIZE, constant::SQUARE_PX_SIZE);
      m_squares[index].setOriginToCenter();
    }
  }

  static sf::Font s_font;
  static bool s_font_loaded = false;
  if (!s_font_loaded) {
    s_font_loaded = s_font.loadFromFile(constant::STR_FILE_PATH_FONT);
    if (s_font_loaded) s_font.setSmooth(false);
  }

  const sf::Color outlineCol = constant::COL_BOARD_OUTLINE;
  const float outlineThick = 4.f;
  const int textSize = 55;

  // FILE-Labels (a–h)
  for (int file = 0; file < constant::BOARD_SIZE; ++file) {
    sf::Text t(std::string(1, static_cast<char>('a' + file)), s_font, textSize);
    t.setFillColor(sf::Color::Transparent);
    t.setOutlineColor(outlineCol);
    t.setOutlineThickness(outlineThick);
    auto bounds = t.getLocalBounds();
    sf::RenderTexture rt;
    rt.create(static_cast<unsigned int>(bounds.width + 20),
              static_cast<unsigned int>(bounds.height + 20));
    rt.clear(sf::Color::Transparent);
    t.setPosition(-bounds.left + 10, -bounds.top + 10);
    rt.draw(t);
    rt.display();
    m_file_textures[file] = rt.getTexture();
    m_file_textures[file].setSmooth(true);
    m_file_labels[file].setTexture(m_file_textures[file]);
    auto size = m_file_labels[file].getOriginalSize();
    float scale = (constant::SQUARE_PX_SIZE * 0.25f) / size.x;
    m_file_labels[file].setScale(scale, scale);
  }

  // RANK-Labels (1–8)
  for (int rank = 0; rank < constant::BOARD_SIZE; ++rank) {
    sf::Text t(std::to_string(rank + 1), s_font, textSize);
    t.setFillColor(sf::Color::Transparent);
    t.setOutlineColor(outlineCol);
    t.setOutlineThickness(outlineThick);
    auto bounds = t.getLocalBounds();
    sf::RenderTexture rt;
    rt.create(static_cast<unsigned int>(bounds.width + 20),
              static_cast<unsigned int>(bounds.height + 20));
    rt.clear(sf::Color::Transparent);
    t.setPosition(-bounds.left + 10, -bounds.top + 10);
    rt.draw(t);
    rt.display();
    m_rank_textures[rank] = rt.getTexture();
    m_rank_textures[rank].setSmooth(true);
    m_rank_labels[rank].setTexture(m_rank_textures[rank]);
    auto size = m_rank_labels[rank].getOriginalSize();
    float scale = (constant::SQUARE_PX_SIZE * 0.25f) / size.x;
    m_rank_labels[rank].setScale(scale, scale);
  }

  positionLabels(board_offset);
}

[[nodiscard]] Entity::Position Board::getPosOfSquare(core::Square sq) const {
  return m_squares[static_cast<size_t>(sq)].getPosition();
}

void Board::draw(sf::RenderWindow &window) {
  Entity::draw(window);

  for (auto &s : m_squares) {
    s.draw(window);
  }
  for (auto &l : m_file_labels) {
    l.draw(window);
  }
  for (auto &l : m_rank_labels) {
    l.draw(window);
  }
}

void Board::setPosition(const Entity::Position &pos) {
  Entity::setPosition(pos);
  Entity::Position board_offset(
      getPosition().x - constant::WINDOW_PX_SIZE / 2 + constant::SQUARE_PX_SIZE / 2,
      getPosition().y - constant::WINDOW_PX_SIZE / 2 + constant::SQUARE_PX_SIZE / 2);

  // reposition Squares
  for (int rank = 0; rank < constant::BOARD_SIZE; ++rank) {
    for (int file = 0; file < constant::BOARD_SIZE; ++file) {
      int index = file + rank * constant::BOARD_SIZE;

      float x = board_offset.x + file * constant::SQUARE_PX_SIZE;
      float y = board_offset.y + (constant::BOARD_SIZE - 1 - rank) * constant::SQUARE_PX_SIZE;

      m_squares[index].setPosition({x, y});
    }
  }
  positionLabels(board_offset);
}

void Board::setFlipped(bool flipped) {
  m_flipped = flipped;
  setPosition(getPosition());
}

bool Board::isFlipped() const {
  return m_flipped;
}

}  // namespace lilia::view
