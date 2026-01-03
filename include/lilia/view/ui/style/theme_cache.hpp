#pragma once

#include "palette_cache.hpp"
#include "theme.hpp"

namespace lilia::view::ui
{

  class ThemeCache
  {
  public:
    ThemeCache()
    {
      rebuild();
      m_listenerId = PaletteCache::get().addListener([this]
                                                     { rebuild(); });
    }

    ~ThemeCache() { PaletteCache::get().removeListener(m_listenerId); }

    ThemeCache(const ThemeCache &) = delete;
    ThemeCache &operator=(const ThemeCache &) = delete;

    const PaletteColors &colors() const { return m_colors; }
    const ui::Theme &uiTheme() const { return m_ui; }

  private:
    // helper (lokal in ThemeCache.cpp/.hpp)
    static float luma(sf::Color c)
    {
      const float r = c.r / 255.f, g = c.g / 255.f, b = c.b / 255.f;
      return 0.2126f * r + 0.7152f * g + 0.0722f * b;
    }
    static sf::Color pickOn(sf::Color bg, sf::Color light, sf::Color dark)
    {
      return (luma(bg) < 0.55f) ? light : dark;
    }

    void rebuild()
    {
      m_colors = PaletteCache::get().colors();
      const auto p = PaletteCache::get().palette();

      m_ui.bgTop = p[ColorId::COL_BG_TOP];
      m_ui.bgBottom = p[ColorId::COL_BG_BOTTOM];

      m_ui.panel = p[ColorId::COL_PANEL_TRANS];
      m_ui.panelBorder = p[ColorId::COL_PANEL_BORDER_ALT];

      m_ui.button = p[ColorId::COL_BUTTON];
      m_ui.buttonHover = p[ColorId::COL_BUTTON_ACTIVE];
      m_ui.buttonActive = p[ColorId::COL_BUTTON_ACTIVE];

      m_ui.accent = p[ColorId::COL_ACCENT];

      m_ui.text = p[ColorId::COL_TEXT];
      m_ui.subtle = p[ColorId::COL_MUTED_TEXT];

      m_ui.inputBg = p[ColorId::COL_INPUT_BG];
      m_ui.inputBorder = p[ColorId::COL_INPUT_BORDER];
      m_ui.valid = p[ColorId::COL_VALID];
      m_ui.invalid = p[ColorId::COL_INVALID];

      m_ui.toastBg = p[ColorId::COL_PANEL_ALPHA220];

      m_ui.onButton = p[ColorId::COL_TEXT];
      m_ui.onAccent = pickOn(m_ui.accent, p[ColorId::COL_LIGHT_TEXT], p[ColorId::COL_DARK_TEXT]);
    }

    PaletteCache::ListenerID m_listenerId{0};
    PaletteColors m_colors{};
    ui::Theme m_ui{};
  };

} // namespace lilia::view
