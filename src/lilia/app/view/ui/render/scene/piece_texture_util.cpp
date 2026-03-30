#include "lilia/app/view/ui/render/scene/piece_texture_util.hpp"

const sf::Texture &lilia::app::view::ui::getPieceTexture(lilia::chess::PieceType type, lilia::chess::Color color)
{
  constexpr std::uint8_t numTypes = 6;
  std::string filename = std::string("piece_") +
                         std::to_string(static_cast<std::uint8_t>(type) +
                                        numTypes * static_cast<std::uint8_t>(color)) +
                         ".png";
  return lilia::app::view::ui::ResourceTable::getInstance().getAssetTexture(filename);
}
