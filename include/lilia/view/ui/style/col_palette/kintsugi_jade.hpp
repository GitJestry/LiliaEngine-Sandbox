#pragma once
#include "color_palette.hpp"

namespace lilia::view {

// Returns the Rose Noir color palette. The palette is constructed on first use
// and cached for subsequent calls.
const ColorPalette& kintsugiJadePalette();

}  // namespace lilia::view
