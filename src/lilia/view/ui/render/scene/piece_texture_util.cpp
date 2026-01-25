#include "lilia/view/ui/render/scene/piece_texture_util.hpp"

const sf::Texture &lilia::view::ui::render::utils::getPieceTexture(core::PieceType type, core::Color color)
{
  constexpr std::uint8_t numTypes = 6;
  std::string filename = std::string("piece_") +
                         std::to_string(static_cast<std::uint8_t>(type) +
                                        numTypes * static_cast<std::uint8_t>(color)) +
                         ".png";
  return ResourceTable::getInstance().getAssetTexture(filename);
}
