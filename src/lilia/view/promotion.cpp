#include "lilia/view/ui/render/scene/promotion.hpp"

#include "lilia/view/ui/render/render_constants.hpp"
#include "lilia/view/ui/render/texture_table.hpp"

namespace lilia::view
{

  Promotion::Promotion(Entity::Position pos, core::PieceType type, core::Color color)
      : Entity(pos), m_type(type)
  {
    std::uint8_t numTypes = 6;
    std::string filename = std::string{constant::path::PIECES_DIR} + "/piece_" +
                           std::to_string(static_cast<std::uint8_t>(type) +
                                          numTypes * static_cast<std::uint8_t>(color)) +
                           ".png";

    const sf::Texture &texture = TextureTable::getInstance().get(filename);
    setTexture(texture);
    setScale(constant::PIECE_SCALE, constant::PIECE_SCALE);
    setOriginToCenter();
  }

  core::PieceType Promotion::getType()
  {
    return m_type;
  }

} // namespace lilia::view
