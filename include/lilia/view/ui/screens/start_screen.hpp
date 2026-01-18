#pragma once

#include <SFML/Graphics.hpp>
#include <string>

#include "lilia/constants.hpp"
#include "lilia/view/ui/style/modals/bot_catalog_modal.hpp"
#include "lilia/view/ui/style/theme_cache.hpp"
#include "lilia/model/analysis/config/start_config.hpp"

namespace lilia::view
{
  enum class StartMode
  {
    NewGame,
    ReplayPgn
  };

  class StartScreen
  {
  public:
    explicit StartScreen(sf::RenderWindow &window);
    ~StartScreen() = default;

    lilia::config::StartConfig run();

  private:
    sf::RenderWindow &m_window;
    sf::Font m_font;

    sf::Texture m_logoTex;
    sf::Sprite m_logo;

    ui::ThemeCache m_theme; // stable cache; updates automatically on palette changes
  };
} // namespace lilia::view
