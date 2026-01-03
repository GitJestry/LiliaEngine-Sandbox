#include "lilia/view/ui/render/scene/piece_node.hpp"

namespace lilia::view
{

  Piece::Piece(core::Color color, core::PieceType type, const sf::Texture &texture)
      : m_color(color), m_type(type), Entity(texture) {}
  Piece::Piece(core::Color color, core::PieceType type, const sf::Texture &texture,
               Entity::Position pos)
      : m_color(color), m_type(type), Entity(texture, pos) {}

  void Piece::setColor(core::Color color)
  {
    m_color = color;
  }
  core::Color Piece::getColor() const
  {
    return m_color;
  }
  void Piece::setType(core::PieceType type)
  {
    m_type = type;
  }
  core::PieceType Piece::getType() const
  {
    return m_type;
  }

  Piece::Piece(const Piece &other)
      // Construct base with texture+position so a NEW id is generated
      : Entity(other.getTexture(), other.getPosition()),
        m_color(other.m_color),
        m_type(other.m_type)
  {
    setOriginToCenter();
  }

  Piece &Piece::operator=(const Piece &other)
  {
    if (this == &other)
      return *this;

    // IMPORTANT: keep *this* Entity id; just copy visuals & data
    setTexture(other.getTexture());
    setPosition(other.getPosition());
    const auto orig = other.getOriginalSize();
    const auto cur = other.getCurrentSize();
    if (orig.x != 0.f && orig.y != 0.f)
    {
      setScale(cur.x / orig.x, cur.y / orig.y);
    }
    setOriginToCenter();
    m_color = other.m_color;
    m_type = other.m_type;
    return *this;
  }

} // namespace lilia::view
