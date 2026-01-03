#pragma once

#include "lilia/chess_types.hpp"
#include "lilia/view/ui/render/entity.hpp"

namespace lilia::view
{

  class Promotion : public Entity
  {
  public:
    Promotion(Entity::Position pos, core::PieceType type, core::Color color);
    core::PieceType getType();
    Promotion() = default;

  private:
    core::PieceType m_type;
  };

}
