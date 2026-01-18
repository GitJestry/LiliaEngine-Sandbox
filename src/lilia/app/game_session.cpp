#include "lilia/app/game_session.hpp"

#include <SFML/Window/Event.hpp>

#include "lilia/controller/game_controller.hpp"
#include "lilia/model/chess_game.hpp"
#include "lilia/view/ui/screens/game_view.hpp"
#include "lilia/model/analysis/pgn_reader.hpp"

namespace lilia::app
{
  lilia::controller::NextAction runSession(sf::RenderWindow &window,
                                           const lilia::config::StartConfig &cfg)
  {
    using lilia::controller::NextAction;

    lilia::model::ChessGame chessGame;

    const bool whiteIsBot = (cfg.white.kind == lilia::config::SideKind::Engine);
    const bool blackIsBot = (cfg.black.kind == lilia::config::SideKind::Engine);

    lilia::view::GameView gameView(window, blackIsBot, whiteIsBot);
    lilia::controller::GameController gameController(gameView, chessGame);

    if (cfg.replay.enabled)
    {
      lilia::model::analysis::GameRecord rec;
      std::string err;
      if (lilia::model::analysis::parsePgnToRecord(cfg.replay.pgnText, rec, &err))
      {
        gameController.startReplay(rec);
      }
      else
      {
        // fallback: start a normal game from cfg.game.startFen
        gameController.startGame(cfg);
      }
    }
    else
    {
      gameController.startGame(cfg);
    }

    sf::Clock clock;
    while (window.isOpen() && gameController.getNextAction() == NextAction::None)
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

      window.clear();
      gameController.render();
      window.display();
    }

    if (!window.isOpen())
      return NextAction::None;

    return gameController.getNextAction();
  }
} // namespace lilia::app
