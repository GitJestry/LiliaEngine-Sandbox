#include "lilia/view/ui/style/color_palette_manager.hpp"

#include "lilia/view/ui/style/col_palette/amethyst.hpp"
#include "lilia/view/ui/style/col_palette/chess_com.hpp"
#include "lilia/view/ui/style/col_palette/kintsugi_jade.hpp"
#include "lilia/view/ui/style/col_palette/soft_pink.hpp"
#include "lilia/view/ui/render/render_constants.hpp"
namespace lilia::view
{

  ColorPaletteManager &ColorPaletteManager::get()
  {
    static ColorPaletteManager instance;
    return instance;
  }

  ColorPaletteManager::ColorPaletteManager()
  {
#define X(name, defaultValue) m_default.name = defaultValue;
    LILIA_COLOR_PALETTE(X)
#undef X
    m_current = m_default;

    registerPalette(constant::palette_name::DEFAULT, ColorPalette{});
    registerPalette(constant::palette_name::AMETHYST, amethystPalette());
    registerPalette(constant::palette_name::GREEN_IVORY, chessComPalette());
    registerPalette(constant::palette_name::SOFT_PINK, softPinkPalette());
    registerPalette(constant::palette_name::KINTSUGI, kintsugiJadePalette());

    m_active = std::string{constant::palette_name::DEFAULT};
  }

  void ColorPaletteManager::registerPalette(std::string_view name, const ColorPalette &palette)
  {
    std::string key{name};

    const bool existed = (m_palettes.find(key) != m_palettes.end());
    m_palettes[key] = palette;

    if (!existed)
      m_order.push_back(key);

    // If the active palette is being re-registered (e.g. dev hot-reload), apply it.
    if (m_active == key)
      loadPalette(palette);
  }

  void ColorPaletteManager::setPalette(std::string_view name)
  {
    std::string key{name};
    auto it = m_palettes.find(key);
    if (it == m_palettes.end())
      return;

    loadPalette(it->second);
    m_active = std::move(key);
  }

  bool ColorPaletteManager::isResolvedEqual(const PaletteColors &a,
                                            const PaletteColors &b) const noexcept
  {
#define X(name, defaultValue) \
  if (a.name != b.name)       \
    return false;
    LILIA_COLOR_PALETTE(X)
#undef X
    return true;
  }

  void ColorPaletteManager::loadPalette(const ColorPalette &overrides)
  {
    PaletteColors resolved = resolvePalette(overrides, m_default);

    if (isResolvedEqual(resolved, m_current))
      return;

    m_current = resolved;
    notifyListeners();
  }

  ColorPaletteManager::ListenerID ColorPaletteManager::addListener(std::function<void()> fn)
  {
    const ListenerID id = m_nextListenerId++;
    m_listeners.emplace(id, std::move(fn));
    return id;
  }

  void ColorPaletteManager::removeListener(ListenerID id)
  {
    m_listeners.erase(id);
  }

  void ColorPaletteManager::notifyListeners()
  {
    // Robust against listeners removing themselves during callbacks.
    std::vector<ListenerID> ids;
    ids.reserve(m_listeners.size());
    for (const auto &[id, _] : m_listeners)
      ids.push_back(id);

    for (ListenerID id : ids)
    {
      auto it = m_listeners.find(id);
      if (it != m_listeners.end() && it->second)
        it->second();
    }
  }

} // namespace lilia::view
