#pragma once

#include "lilia/view/ui/render/resource_table.hpp"
#include "lilia/chess_types.hpp"

namespace lilia::view::ui::render::utils
{

  const sf::Texture &getPieceTexture(core::PieceType type, core::Color color);

};
