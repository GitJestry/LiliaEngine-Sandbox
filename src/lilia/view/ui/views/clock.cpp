#include "lilia/view/ui/views/clock.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <string>

#include "lilia/view/ui/style/palette_cache.hpp"
#include "lilia/view/ui/render/render_constants.hpp"
#include "lilia/view/ui/style/style.hpp"

namespace lilia::view
{

  namespace
  {

    // layout
    constexpr float kScale = 0.80f; // 20% smaller
    constexpr float kPadX = 10.f;
    constexpr float kIconRadius = 6.f;
    constexpr float kIconOffsetX = kIconRadius + 12.f;
    constexpr float kActiveStripW = 3.f;

    static std::string formatTime(float seconds)
    {
      if (seconds < 20.f)
      {
        int tenths = static_cast<int>(seconds * 10.f); // truncated
        int totalSec = tenths / 10;
        int h = totalSec / 3600;
        int m = (totalSec % 3600) / 60;
        int sec = totalSec % 60;

        std::ostringstream oss;
        oss << std::setw(2) << std::setfill('0');
        if (h > 0)
        {
          oss << h << ':' << std::setw(2) << m << ':' << std::setw(2) << sec << '.' << (tenths % 10);
        }
        else
        {
          oss << m << ':' << std::setw(2) << sec << '.' << (tenths % 10);
        }
        return oss.str();
      }

      int s = static_cast<int>(seconds + 0.5f);
      int h = s / 3600;
      int m = (s % 3600) / 60;
      int sec = s % 60;

      std::ostringstream oss;
      oss << std::setw(2) << std::setfill('0');
      if (h > 0)
      {
        oss << h << ':' << std::setw(2) << m << ':' << std::setw(2) << sec;
      }
      else
      {
        oss << m << ':' << std::setw(2) << sec;
      }
      return oss.str();
    }

  } // namespace

  Clock::Clock()
  {
    const float baseW = WIDTH * kScale;
    const float baseH = HEIGHT * kScale;

    m_box.setSize({baseW, baseH});
    m_box.setOutlineThickness(1.f);

    m_overlay.setSize({baseW, baseH});

    m_icon_circle.setRadius(kIconRadius);
    m_icon_circle.setOrigin(kIconRadius, kIconRadius);
    m_icon_circle.setFillColor(sf::Color::Transparent);
    m_icon_circle.setOutlineThickness(2.f);

    m_icon_hand.setSize({kIconRadius - 2.f, 1.f});
    m_icon_hand.setOrigin(0.f, 0.5f);
    m_icon_hand.setRotation(-90.f); // up

    m_font.loadFromFile(std::string{constant::path::FONT_DIR});
    m_font.setSmooth(false);

    m_text.setFont(m_font);
    m_text.setCharacterSize(18);
    m_text.setStyle(sf::Text::Style::Bold);

    updateVisualState();
  }

  void Clock::updateVisualState()
  {
    const auto pal = PaletteCache::get().palette();

    const bool isLight = (m_playerColor == core::Color::White);

    const sf::Color baseFill = isLight ? pal[ColorId::COL_LIGHT_BG] : pal[ColorId::COL_DARK_BG];
    const sf::Color baseText = isLight ? pal[ColorId::COL_DARK_TEXT] : pal[ColorId::COL_LIGHT_TEXT];

    // Active modifies the neutral fill (but low-time overrides actual fill).
    sf::Color tweakedFill = baseFill;
    if (m_active)
    {
      tweakedFill = isLight ? ui::darken(baseFill, 18) : ui::lighten(baseFill, 16);
    }

    // Fill (low time overrides).
    m_box.setFillColor(m_low_time ? pal[ColorId::COL_LOW_TIME] : tweakedFill);

    // Outline + overlay
    if (m_active)
    {
      m_box.setOutlineThickness(2.f);
      m_box.setOutlineColor(ui::lerpColor(pal[ColorId::COL_BORDER], pal[ColorId::COL_CLOCK_ACCENT], 0.65f));

      sf::Color tint = pal[ColorId::COL_CLOCK_ACCENT];
      tint.a = 28;
      m_overlay.setFillColor(tint);
    }
    else
    {
      m_box.setOutlineThickness(1.f);
      m_box.setOutlineColor(pal[ColorId::COL_BORDER]);
      m_overlay.setFillColor(pal[ColorId::COL_OVERLAY_DIM]);
    }

    // Text (low time forces high-contrast light text as before)
    m_text.setFillColor(m_low_time ? pal[ColorId::COL_LIGHT_TEXT] : baseText);

    // Icon colors (derived from clock accent + text for contrast)
    const float mixT = isLight ? 0.45f : 0.25f;
    const sf::Color iconCol = ui::lerpColor(pal[ColorId::COL_CLOCK_ACCENT], baseText, mixT);

    m_icon_circle.setOutlineColor(iconCol);
    m_icon_hand.setFillColor(iconCol);
    m_icon_hand.setOutlineThickness(1.f);
    m_icon_hand.setOutlineColor(iconCol);
  }

  void Clock::setPlayerColor(core::Color color)
  {
    m_playerColor = color;
    updateVisualState();
  }

  void Clock::setPosition(const sf::Vector2f &pos)
  {
    m_box.setPosition({ui::snapf(pos.x), ui::snapf(pos.y)});
    m_overlay.setPosition(m_box.getPosition());

    const auto tb = m_text.getLocalBounds();
    const auto bs = m_box.getSize();

    const float tx = m_box.getPosition().x + bs.x - kPadX - tb.width;
    const float ty = m_box.getPosition().y + (bs.y - tb.height) * 0.5f - tb.top;
    m_text.setPosition({ui::snapf(tx), ui::snapf(ty)});

    const float iconX = m_box.getPosition().x + kIconOffsetX;
    const float iconY = m_box.getPosition().y + bs.y * 0.5f;
    m_icon_circle.setPosition({ui::snapf(iconX), ui::snapf(iconY)});
    m_icon_hand.setPosition({ui::snapf(iconX), ui::snapf(iconY)});
  }

  void Clock::setTime(float seconds)
  {
    m_text.setString(formatTime(seconds));
    m_low_time = (seconds <= 20.f);

    // Layout/size logic unchanged.
    const auto tb = m_text.getLocalBounds();
    auto size = m_box.getSize();

    const float minW = WIDTH * kScale;
    const float needW = tb.width + 2.f * kPadX;

    if (needW > size.x)
    {
      size.x = needW;
      m_box.setSize(size);
      m_overlay.setSize(size);
    }
    else if (size.x < minW)
    {
      size.x = minW;
      m_box.setSize(size);
      m_overlay.setSize(size);
    }

    updateVisualState();
    setPosition(m_box.getPosition());
  }

  void Clock::setActive(bool active)
  {
    m_active = active;
    if (!m_active)
    {
      m_icon_hand.setRotation(-90.f);
    }
    updateVisualState();
  }

  void Clock::render(sf::RenderWindow &window)
  {
    // Ensure palette changes are reflected even without explicit setters.
    updateVisualState();

    window.draw(m_box);
    window.draw(m_overlay);

    if (m_active)
    {
      const auto pal = PaletteCache::get().palette();

      sf::RectangleShape strip({kActiveStripW, m_box.getSize().y});
      strip.setPosition(m_box.getPosition());
      strip.setFillColor(pal[ColorId::COL_CLOCK_ACCENT]);
      window.draw(strip);

      static sf::Clock animClock;
      const int step = static_cast<int>(animClock.getElapsedTime().asSeconds()) % 4;
      const float angle = -90.f + 90.f * static_cast<float>(step);
      m_icon_hand.setRotation(angle);

      window.draw(m_icon_circle);
      window.draw(m_icon_hand);
    }

    window.draw(m_text);
  }

} // namespace lilia::view
