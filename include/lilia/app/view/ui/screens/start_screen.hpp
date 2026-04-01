#pragma once

#include <SFML/Graphics.hpp>
#include <string>

#include "lilia/app/view/ui/style/modals/bot_catalog_modal.hpp"
#include "lilia/app/view/ui/style/theme_cache.hpp"
#include "lilia/app/domain/analysis/config/start_config.hpp"

namespace lilia::app::view::ui
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

    domain::analysis::config::StartConfig run();

  private:
    sf::RenderWindow &m_window;
    sf::Font m_font;

    sf::Texture m_logoTex;
    sf::Sprite m_logo;

    ThemeCache m_theme; // stable cache; updates automatically on palette changes
  };
}
