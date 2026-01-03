#pragma once

#include <SFML/Graphics.hpp>
#include <string>

#include "lilia/constants.hpp"
#include "lilia/view/ui/style/modals/bot_catalog_modal.hpp"
#include "lilia/view/ui/style/theme_cache.hpp"

namespace lilia::view
{

  struct StartConfig
  {
    bool whiteIsBot{false};
    bool blackIsBot{true};

    EngineChoice whiteEngine{};
    EngineChoice blackEngine{};

    std::string fen;

    int timeBaseSeconds{300};
    int timeIncrementSeconds{0};
    bool timeEnabled{false};
  };

  class StartScreen
  {
  public:
    explicit StartScreen(sf::RenderWindow &window);
    ~StartScreen() = default;

    StartConfig run();

  private:
    sf::RenderWindow &m_window;
    sf::Font m_font;

    sf::Texture m_logoTex;
    sf::Sprite m_logo;

    ui::ThemeCache m_theme; // stable cache; updates automatically on palette changes
  };

} // namespace lilia::view
