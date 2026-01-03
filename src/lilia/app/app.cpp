#include "lilia/app/app.hpp"

#include <SFML/Graphics/RenderWindow.hpp>
#include <SFML/Window/Event.hpp>

#include "lilia/bot/bot_info.hpp"
#include "lilia/controller/game_controller.hpp"
#include "lilia/engine/engine.hpp"
#include "lilia/model/chess_game.hpp"
#include "lilia/view/ui/screens/game_view.hpp"
#include "lilia/view/ui/screens/start_screen.hpp"
#include "lilia/view/ui/render/texture_table.hpp"

namespace lilia::app
{

  int App::run()
  {
    engine::Engine::init();
    lilia::view::TextureTable::getInstance().preLoad();

    sf::RenderWindow window(
        sf::VideoMode(lilia::view::constant::WINDOW_TOTAL_WIDTH,
                      lilia::view::constant::WINDOW_TOTAL_HEIGHT),
        "Lilia", sf::Style::Titlebar | sf::Style::Close);

    while (window.isOpen())
    {
      // Start screen is responsible for its own themed background.
      lilia::view::StartScreen startScreen(window);
      const auto cfg = startScreen.run();

      // Resolve bot parameters from EngineChoice (builtin-only for now; external is placeholder).
      const auto resolveBot = [](const lilia::view::EngineChoice &choice)
      {
        if (!choice.external)
          return getBotConfig(choice.builtin);

        // External engines are a placeholder in UI; fall back safely.
        return getBotConfig(BotType::Lilia);
      };

      const auto whiteCfg = resolveBot(cfg.whiteEngine);
      const auto blackCfg = resolveBot(cfg.blackEngine);

      const int whiteDepth = whiteCfg.depth;
      const int whiteThinkMs = whiteCfg.thinkTimeMs;
      const int blackDepth = blackCfg.depth;
      const int blackThinkMs = blackCfg.thinkTimeMs;

      const bool whiteIsBot = cfg.whiteIsBot;
      const bool blackIsBot = cfg.blackIsBot;

      const std::string startFen = cfg.fen;
      const int baseSeconds = cfg.timeBaseSeconds;
      const int incrementSeconds = cfg.timeIncrementSeconds;
      const bool timeEnabled = cfg.timeEnabled;

      lilia::controller::NextAction action = lilia::controller::NextAction::None;

      do
      {
        action = lilia::controller::NextAction::None;

        lilia::model::ChessGame chessGame;

        // GameView is responsible for themed rendering (background included).
        lilia::view::GameView gameView(window, blackIsBot, whiteIsBot);
        lilia::controller::GameController gameController(gameView, chessGame);

        gameController.startGame(startFen, whiteIsBot, blackIsBot,
                                 whiteThinkMs, whiteDepth,
                                 blackThinkMs, blackDepth,
                                 timeEnabled, baseSeconds, incrementSeconds);

        sf::Clock clock;
        while (window.isOpen() &&
               gameController.getNextAction() == lilia::controller::NextAction::None)
        {
          const float dt = clock.restart().asSeconds();

          sf::Event event;
          while (window.pollEvent(event))
          {
            if (event.type == sf::Event::Closed)
              window.close();
            gameController.handleEvent(event);
          }

          gameController.update(dt);

          // No hard-coded background colors here.
          // GameView/GameController render must draw a full frame.
          window.clear();
          gameController.render();
          window.display();
        }

        if (!window.isOpen())
          return 0;

        action = gameController.getNextAction();

      } while (action == lilia::controller::NextAction::Rematch && window.isOpen());

      if (action != lilia::controller::NextAction::NewBot)
        break;
    }

    return 0;
  }

} // namespace lilia::app
