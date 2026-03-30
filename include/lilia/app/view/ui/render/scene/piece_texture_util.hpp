#pragma once

#include "lilia/app/view/ui/render/resource_table.hpp"
#include "lilia/chess/chess_types.hpp"

namespace lilia::app::view::ui
{

  const sf::Texture &getPieceTexture(chess::PieceType type, chess::Color color);

};
