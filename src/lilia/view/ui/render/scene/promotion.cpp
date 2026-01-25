#include "lilia/view/ui/render/scene/promotion.hpp"

#include "lilia/view/ui/render/render_constants.hpp"
#include "lilia/view/ui/render/scene/piece_texture_util.hpp"

namespace lilia::view
{

  Promotion::Promotion(Entity::Position pos, core::PieceType type, core::Color color)
      : Entity(pos), m_type(type)
  {
    setTexture(ui::render::utils::getPieceTexture(type, color));
    setScale(constant::PIECE_SCALE, constant::PIECE_SCALE);
    setOriginToCenter();
  }

  core::PieceType Promotion::getType()
  {
    return m_type;
  }

} // namespace lilia::view
