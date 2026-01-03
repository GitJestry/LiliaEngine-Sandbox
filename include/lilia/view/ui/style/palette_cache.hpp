#pragma once

#include <cstdint>
#include <functional>
#include <unordered_map>
#include <vector>

#include "color_palette_manager.hpp"
#include "col_palette/color_palette.hpp"

namespace lilia::view
{

  class PaletteCache final
  {
  public:
    using ListenerID = std::uint64_t;

    static PaletteCache &get();

    const PaletteColors &colors() const noexcept { return m_colors; }

    [[nodiscard]] PaletteCRef palette() const noexcept { return PaletteCRef{m_colors}; }
    [[nodiscard]] const sf::Color &color(ColorId id) const noexcept { return palette()[id]; }

    ListenerID addListener(std::function<void()> fn);
    void removeListener(ListenerID id);

  private:
    PaletteCache();
    ~PaletteCache();

    PaletteCache(const PaletteCache &) = delete;
    PaletteCache &operator=(const PaletteCache &) = delete;

    bool refreshFromManager(); // returns true if snapshot changed
    void notifyListeners();

    PaletteColors m_colors{};
    ColorPaletteManager::ListenerID m_mgrListener{0};

    ListenerID m_next{1};
    std::unordered_map<ListenerID, std::function<void()>> m_listeners;
  };

} // namespace lilia::view
