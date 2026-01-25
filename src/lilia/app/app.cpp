#include "lilia/app/app.hpp"

#include <SFML/Graphics/RenderWindow.hpp>

#include "lilia/view/ui/render/resource_table.hpp"
#include "lilia/view/ui/screens/start_screen.hpp"

#include "lilia/app/game_session.hpp"
#include "lilia/view/ui/render/render_constants.hpp"

namespace lilia::app
{
  int App::run()
  {
    lilia::view::ResourceTable::getInstance().preLoad();

    sf::RenderWindow window(
        sf::VideoMode(lilia::view::constant::WINDOW_TOTAL_WIDTH,
                      lilia::view::constant::WINDOW_TOTAL_HEIGHT),
        "Lilia", sf::Style::Titlebar | sf::Style::Close);

    while (window.isOpen())
    {
      lilia::view::StartScreen startScreen(window);
      const auto cfg = startScreen.run();

      for (;;)
      {
        const auto action = lilia::app::runSession(window, cfg);
        if (!window.isOpen())
          return 0;

        if (action == lilia::controller::NextAction::Rematch)
          continue;

        if (action == lilia::controller::NextAction::NewBot)
          break;

        return 0;
      }
    }

    return 0;
  }
} // namespace lilia::app
