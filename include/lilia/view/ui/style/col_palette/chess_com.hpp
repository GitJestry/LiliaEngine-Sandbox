#pragma once
#include "color_palette.hpp"

namespace lilia::view {

// Returns the Chess.com color palette. The palette is constructed on first use
// and cached for subsequent calls.
const ColorPalette& chessComPalette();

}  // namespace lilia::view
