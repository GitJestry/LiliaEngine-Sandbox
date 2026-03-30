#pragma once

#include "lilia/chess/chess_types.hpp"
#include "lilia/app/view/ui/render/entity.hpp"

namespace lilia::app::view::ui
{

  class Promotion : public Entity
  {
  public:
    Promotion(MousePos pos, chess::PieceType type, chess::Color color);
    chess::PieceType getType();
    Promotion() = default;

  private:
    chess::PieceType m_type;
  };

}
