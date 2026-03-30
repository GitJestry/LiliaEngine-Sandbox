#include "lilia/app/app.hpp"

#include <SFML/Graphics/RenderWindow.hpp>

#include "lilia/app/view/ui/render/resource_table.hpp"
#include "lilia/app/view/ui/screens/start_screen.hpp"
#include "lilia/app/engines/builtin_bootstrap.hpp"

#include "lilia/app/session/game_session.hpp"
#include "lilia/app/view/ui/render/render_constants.hpp"
#include "lilia/engine/engine.hpp"

namespace lilia::app
{
  int App::run()
  {
    engines::bootstrapBuiltinEngines();
    engine::Engine::init();
    view::ui::ResourceTable::getInstance().preLoad();

    sf::RenderWindow window(
        sf::VideoMode(view::ui::constant::WINDOW_TOTAL_WIDTH,
                      view::ui::constant::WINDOW_TOTAL_HEIGHT),
        "Lilia", sf::Style::Titlebar | sf::Style::Close);

    while (window.isOpen())
    {
      view::ui::StartScreen startScreen(window);
      const auto cfg = startScreen.run();

      for (;;)
      {
        const auto action = session::runSession(window, cfg);
        if (!window.isOpen())
          return 0;

        if (action == controller::NextAction::Rematch)
          continue;

        if (action == controller::NextAction::NewBot)
          break;

        return 0;
      }
    }

    return 0;
  }
} // namespace lilia::app
