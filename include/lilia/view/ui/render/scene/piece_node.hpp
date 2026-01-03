#pragma once
#include "lilia/chess_types.hpp"
#include "../entity.hpp"

namespace lilia::view
{

  class Piece : public Entity
  {
  public:
    Piece(core::Color color, core::PieceType type, const sf::Texture &texture);
    Piece(core::Color color, core::PieceType type, const sf::Texture &texture, Entity::Position pos);
    Piece() = default;

    Piece(const Piece &other);
    Piece &operator=(const Piece &other);

    void setColor(core::Color color);
    core::Color getColor() const;
    void setType(core::PieceType type);
    core::PieceType getType() const;

  private:
    core::Color m_color;
    core::PieceType m_type;
  };

}
