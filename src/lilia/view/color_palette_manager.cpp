#include "lilia/view/color_palette_manager.hpp"

#include "lilia/view/col_palette/amethyst.hpp"
#include "lilia/view/col_palette/chess_com.hpp"
#include "lilia/view/col_palette/kintsugi_jade.hpp"
#include "lilia/view/col_palette/soft_pink.hpp"
#include "lilia/view/render_constants.hpp"
#include "lilia/view/texture_table.hpp"

namespace lilia::view {

ColorPaletteManager& ColorPaletteManager::get() {
  static ColorPaletteManager instance;
  return instance;
}

ColorPaletteManager::ColorPaletteManager() {
#define X(name, defaultValue)    \
  m_default.name = defaultValue; \
  m_current.name = defaultValue;
  LILIA_COLOR_PALETTE(X)
#undef X

  registerPalette(constant::STR_COL_PALETTE_DEFAULT, ColorPalette{});
  registerPalette(constant::STR_COL_PALETTE_Amethyst, amethystPalette());
  registerPalette(constant::STR_COL_PALETTE_GREEN_IVORY, chessComPalette());
  registerPalette(constant::STR_COL_PALETTE_SOFT_PINK, softPinkPalette());
  registerPalette(constant::STR_COL_PALETTE_KINTSUGI, kintsugiJadePalette());
  m_active = constant::STR_COL_PALETTE_DEFAULT;
}

void ColorPaletteManager::registerPalette(std::string_view name, const ColorPalette& palette) {
  std::string key{name};
  if (!m_palettes.count(key)) m_order.push_back(key);
  m_palettes[std::move(key)] = palette;
}

void ColorPaletteManager::setPalette(std::string_view name) {
  std::string key{name};
  auto it = m_palettes.find(key);
  if (it != m_palettes.end()) {
    loadPalette(it->second);
    m_active = std::move(key);
  }
}

void ColorPaletteManager::loadPalette(const ColorPalette& palette) {
#define X(name, defaultValue) m_current.name = palette.name.value_or(m_default.name);
  LILIA_COLOR_PALETTE(X)
#undef X

  TextureTable::getInstance().reloadForPalette();
  for (auto& [id, fn] : m_listeners) {
    if (fn) fn();
  }
}

ColorPaletteManager::ListenerID ColorPaletteManager::addListener(std::function<void()> listener) {
  ListenerID id = m_nextListenerId++;
  m_listeners[id] = std::move(listener);
  return id;
}

void ColorPaletteManager::removeListener(ListenerID id) {
  m_listeners.erase(id);
}

}  // namespace lilia::view
