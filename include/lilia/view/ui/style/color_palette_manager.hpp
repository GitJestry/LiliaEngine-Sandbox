#pragma once
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "col_palette/color_palette.hpp"

namespace lilia::view
{

  class ColorPaletteManager
  {
  public:
    using ListenerID = std::size_t;

    static ColorPaletteManager &get();

    void registerPalette(std::string_view name, const ColorPalette &palette);
    void setPalette(std::string_view name);

    // Load overrides directly; unresolved entries fall back to defaults.
    void loadPalette(const ColorPalette &overrides);

    // Resolved colors (the thing everyone ultimately wants).
    const PaletteColors &palette() const noexcept { return m_current; }
    const PaletteColors &defaultPalette() const noexcept { return m_default; }

    const std::vector<std::string> &paletteNames() const noexcept { return m_order; }
    const std::string &activePalette() const noexcept { return m_active; }

    ListenerID addListener(std::function<void()> fn);
    void removeListener(ListenerID id);

    // Legacy escape hatch (discouraged): mutating this will NOT notify listeners.
    [[deprecated("Use loadPalette()/setPalette(). Direct mutation does not notify listeners.")]]
    PaletteColors &palette() noexcept
    {
      return m_current;
    }

  private:
    ColorPaletteManager();

    void notifyListeners();
    [[nodiscard]] bool isResolvedEqual(const PaletteColors &a, const PaletteColors &b) const noexcept;

    PaletteColors m_default{};
    PaletteColors m_current{};

    std::unordered_map<std::string, ColorPalette> m_palettes;
    std::vector<std::string> m_order;
    std::string m_active{};

    std::unordered_map<ListenerID, std::function<void()>> m_listeners;
    ListenerID m_nextListenerId{1};
  };

} // namespace lilia::view
