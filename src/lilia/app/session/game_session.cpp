#include "lilia/app/session/game_session.hpp"

#include <SFML/Window/Event.hpp>

#include "lilia/app/controller/game_controller.hpp"
#include "lilia/chess/chess_game.hpp"
#include "lilia/app/view/ui/screens/game_view.hpp"
#include "lilia/app/domain/notation/pgn_reader.hpp"

namespace lilia::app::session
{
  controller::NextAction runSession(sf::RenderWindow &window,
                                    const domain::analysis::config::StartConfig &cfg)
  {

    chess::ChessGame chessGame;

    const bool whiteIsBot = (cfg.white.kind == domain::analysis::config::SideKind::Engine);
    const bool blackIsBot = (cfg.black.kind == domain::analysis::config::SideKind::Engine);

    view::ui::GameView gameView(window, blackIsBot, whiteIsBot);
    controller::GameController gameController(gameView, chessGame);

    if (cfg.replay.enabled)
    {
      domain::GameRecord rec;
      std::string err;
      if (domain::notation::parsePgnToRecord(cfg.replay.pgnText, rec, &err))
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
    while (window.isOpen() && gameController.getNextAction() == controller::NextAction::None)
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
      return controller::NextAction::None;

    return gameController.getNextAction();
  }
} // namespace app
