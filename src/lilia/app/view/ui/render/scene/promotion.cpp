#include "lilia/app/view/ui/render/scene/promotion.hpp"

#include "lilia/app/view/ui/render/render_constants.hpp"
#include "lilia/app/view/ui/render/scene/piece_texture_util.hpp"

namespace lilia::app::view::ui
{

  Promotion::Promotion(MousePos pos, chess::PieceType type, chess::Color color)
      : Entity(pos), m_type(type)
  {
    setTexture(getPieceTexture(type, color));
    setScale(constant::PIECE_SCALE, constant::PIECE_SCALE);
    setOriginToCenter();
  }

  chess::PieceType Promotion::getType()
  {
    return m_type;
  }

} // namespace lilia::view
