#include "lilia/app/app.hpp"

#include <SFML/Graphics/RenderWindow.hpp>
#include <SFML/Window/Event.hpp>

#include "lilia/bot/bot_info.hpp"
#include "lilia/controller/game_controller.hpp"
#include "lilia/engine/engine.hpp"
#include "lilia/model/chess_game.hpp"
#include "lilia/view/game_view.hpp"
#include "lilia/view/start_screen.hpp"
#include "lilia/view/texture_table.hpp"

namespace lilia::app {

void drawVerticalGradient(sf::RenderWindow &window, sf::Color top, sf::Color bottom) {
  sf::VertexArray va(sf::TriangleStrip, 4);
  auto size = window.getSize();
  va[0].position = {0.f, 0.f};
  va[1].position = {static_cast<float>(size.x), 0.f};
  va[2].position = {0.f, static_cast<float>(size.y)};
  va[3].position = {static_cast<float>(size.x), static_cast<float>(size.y)};
  va[0].color = va[1].color = top;
  va[2].color = va[3].color = bottom;
  window.draw(va);
}

int App::run() {
  engine::Engine::init();
  lilia::view::TextureTable::getInstance().preLoad();

  sf::RenderWindow window(sf::VideoMode(lilia::view::constant::WINDOW_TOTAL_WIDTH,
                                        lilia::view::constant::WINDOW_TOTAL_HEIGHT),
                          "Lilia", sf::Style::Titlebar | sf::Style::Close);

  while (window.isOpen()) {
    lilia::view::StartScreen startScreen(window);
    auto cfg = startScreen.run();
    bool m_white_is_bot = cfg.whiteIsBot;
    bool m_black_is_bot = cfg.blackIsBot;
    std::string m_start_fen = cfg.fen;
    int baseSeconds = cfg.timeBaseSeconds;
    int incrementSeconds = cfg.timeIncrementSeconds;
    bool timeEnabled = cfg.timeEnabled;

    auto whiteCfg = getBotConfig(cfg.whiteBot);
    auto blackCfg = getBotConfig(cfg.blackBot);
    int whiteDepth = whiteCfg.depth;
    int whiteThinkMs = whiteCfg.thinkTimeMs;
    int blackDepth = blackCfg.depth;
    int blackThinkMs = blackCfg.thinkTimeMs;

    lilia::controller::NextAction action =
        lilia::controller::NextAction::None;

    do {
      action = lilia::controller::NextAction::None;
      lilia::model::ChessGame chessGame;
      lilia::view::GameView gameView(window, m_black_is_bot, m_white_is_bot);
      lilia::controller::GameController gameController(gameView, chessGame);

      gameController.startGame(m_start_fen, m_white_is_bot, m_black_is_bot, whiteThinkMs,
                               whiteDepth, blackThinkMs, blackDepth, timeEnabled, baseSeconds,
                               incrementSeconds);

      sf::Clock clock;
      while (window.isOpen() && gameController.getNextAction() ==
                                    lilia::controller::NextAction::None) {
        float deltaSeconds = clock.restart().asSeconds();
        sf::Event event;
        while (window.pollEvent(event)) {
          if (event.type == sf::Event::Closed) window.close();
          gameController.handleEvent(event);
        }
        gameController.update(deltaSeconds);
        window.clear(sf::Color::Black);
        drawVerticalGradient(window, view::constant::COL_BG_TOP, view::constant::COL_BG_BOTTOM);
        gameController.render();
        window.display();
      }

      if (!window.isOpen()) return 0;

      action = gameController.getNextAction();

    } while (action == lilia::controller::NextAction::Rematch && window.isOpen());

    if (action != lilia::controller::NextAction::NewBot) break;
  }

  return 0;
}

}  // namespace lilia::app
