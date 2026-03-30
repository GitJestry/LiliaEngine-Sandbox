#pragma once
#include "lilia/chess/chess_types.hpp"
#include "../entity.hpp"

namespace lilia::app::view::ui
{

  class PieceNode : public Entity
  {
  public:
    PieceNode(chess::Color color, chess::PieceType type, const sf::Texture &texture);
    PieceNode(chess::Color color, chess::PieceType type, const sf::Texture &texture, MousePos pos);
    PieceNode() = default;

    PieceNode(const PieceNode &other);
    PieceNode &operator=(const PieceNode &other);

    void setColor(chess::Color color);
    chess::Color getColor() const;
    void setType(chess::PieceType type);
    chess::PieceType getType() const;

  private:
    chess::Color m_color;
    chess::PieceType m_type;
  };

}
