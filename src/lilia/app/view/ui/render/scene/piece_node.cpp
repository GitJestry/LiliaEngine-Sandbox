#include "lilia/app/view/ui/render/scene/piece_node.hpp"

namespace lilia::app::view::ui
{

  PieceNode::PieceNode(chess::Color color, chess::PieceType type, const sf::Texture &texture)
      : m_color(color), m_type(type), Entity(texture) {}
  PieceNode::PieceNode(chess::Color color, chess::PieceType type, const sf::Texture &texture,
                       MousePos pos)
      : m_color(color), m_type(type), Entity(texture, pos) {}

  void PieceNode::setColor(chess::Color color)
  {
    m_color = color;
  }
  chess::Color PieceNode::getColor() const
  {
    return m_color;
  }
  void PieceNode::setType(chess::PieceType type)
  {
    m_type = type;
  }
  chess::PieceType PieceNode::getType() const
  {
    return m_type;
  }

  PieceNode::PieceNode(const PieceNode &other)
      // Construct base with texture+position so a NEW id is generated
      : Entity(other.getTexture(), other.getPosition()),
        m_color(other.m_color),
        m_type(other.m_type)
  {
    setOriginToCenter();
  }

  PieceNode &PieceNode::operator=(const PieceNode &other)
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

}
