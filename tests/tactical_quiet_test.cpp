#include <algorithm>
#include <cmath>
#include <atomic>
#include <cassert>
#include <iostream>
#include <memory>
#include <string>

#include "lilia/engine/bot_engine.hpp"
#include "lilia/engine/eval.hpp"
#include "lilia/engine/eval_shared.hpp"
#include "lilia/engine/eval_alias.hpp"
#include "lilia/engine/search.hpp"
#include "lilia/model/chess_game.hpp"
#include "lilia/model/tt5.hpp"
#include "lilia/uci/uci_helper.hpp"

using namespace lilia;

static core::Square sq(char file, int rank)
{
  int f = file - 'a';
  int r = rank - 1;
  return static_cast<core::Square>(r * 8 + f);
}

int main()
{
  engine::EngineConfig cfg;
  engine::BotEngine bot(cfg);

  // Quiet piece move giving check
  {
    model::ChessGame game;
    game.setPosition("4k3/8/8/8/4N3/8/8/4K3 w - - 0 1");
    auto res = bot.findBestMove(game, 2, 50);
    assert(res.bestMove);
    model::Move expected(sq('e', 4), sq('f', 6));
    assert(*res.bestMove == expected);
  }

  // UCI move parsing should stay compatible with Stockfish output
  {
    model::ChessGame game;
    game.setPosition(core::START_FEN);
    assert(game.doMoveUCI("e2e4"));
    assert(game.doMoveUCI("e7e5"));
    assert(game.doMoveUCI("g1f3"));
    assert(game.doMoveUCI("b8c6"));

    const std::string fen = game.getFen();
    const auto boardEnd = fen.find(' ');
    const std::string board = fen.substr(0, boardEnd);
    assert(board == "r1bqkbnr/pppp1ppp/2n5/4p3/4P3/5N2/PPPP1PPP/RNBQKB1R");

    // Trying to play the same pawn move again must fail.
    assert(!game.doMoveUCI("e2e4"));
  }

  // Quiet piece move threatening a rook
  {
    model::ChessGame game;
    game.setPosition("4r2k/8/6B1/8/8/8/8/4K3 w - - 0 1");
    auto res = bot.findBestMove(game, 2, 50);
    assert(res.bestMove);
    model::Move expected(sq('g', 6), sq('f', 7));
    assert(*res.bestMove == expected);
  }

  // Best move should match the first entry in topMoves even when TT suggests a different move
  {
    model::ChessGame game;
    game.setPosition("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
    auto &pos = game.getPositionRefForBot();

    model::TT5 tt;
    engine::Evaluator eval;
    auto evalPtr = std::shared_ptr<const engine::Evaluator>(&eval, [](const engine::Evaluator *) {});
    engine::Search search(tt, evalPtr, cfg);

    model::Move wrong(sq('a', 2), sq('a', 3));
    tt.store(pos.hash(), 0, 1, model::Bound::Exact, wrong);

    auto stop = std::make_shared<std::atomic<bool>>(false);
    search.search_root_single(pos, 2, stop, 0);
    const auto &stats = search.getStats();
    assert(!stats.topMoves.empty());
    assert(stats.bestMove == stats.topMoves[0].first);
  }

  // topMoves should report distinct scores for different moves
  {
    model::ChessGame game;
    game.setPosition("4k3/8/8/7Q/8/8/8/4K3 w - - 0 1");
    auto &pos = game.getPositionRefForBot();

    model::TT5 tt;
    engine::Evaluator eval;
    auto evalPtr = std::shared_ptr<const engine::Evaluator>(&eval, [](const engine::Evaluator *) {});
    engine::Search search(tt, evalPtr, cfg);

    auto stop = std::make_shared<std::atomic<bool>>(false);
    search.search_root_single(pos, 3, stop, 0);
    const auto &stats = search.getStats();
    assert(stats.topMoves.size() >= 2);
    assert(stats.topMoves[0].second != stats.topMoves[1].second);
  }

  // Stockfish now prefers the quiet queen lift h3h6 in this position
  {
    model::ChessGame game;
    game.setPosition("6k1/3b1ppp/p7/3R4/2P2p2/7q/4KQ2/8 b - - 1 66");
    auto res = bot.findBestMove(game, 3, 50);
    assert(res.bestMove);
    model::Move expected(sq('h', 3), sq('h', 6));
    assert(*res.bestMove == expected);
  }

  // Node batching should reset/flush between searches with node limits.
  {
    model::ChessGame game;
    game.setPosition("4k3/8/8/8/8/8/8/4K3 w - - 0 1");
    auto &pos = game.getPositionRefForBot();

    model::TT5 tt;
    engine::Evaluator eval;
    auto evalPtr = std::shared_ptr<const engine::Evaluator>(&eval, [](const engine::Evaluator *) {});
    engine::Search search(tt, evalPtr, cfg);

    constexpr std::uint64_t nodeLimit = 128;
    auto sharedCounter = std::make_shared<std::atomic<std::uint64_t>>(0);

    auto stop1 = std::make_shared<std::atomic<bool>>(false);
    search.set_node_limit(sharedCounter, nodeLimit);
    search.search_root_single(pos, 1, stop1, nodeLimit);
    engine::SearchStats stats1 = search.getStats();
    std::uint64_t actual1 = sharedCounter->load();
    assert(!stop1->load());
    assert(actual1 > 0);
    assert(stats1.nodes == actual1);

    auto stop2 = std::make_shared<std::atomic<bool>>(false);
    search.set_node_limit(sharedCounter, nodeLimit);
    search.search_root_single(pos, 1, stop2, nodeLimit);
    engine::SearchStats stats2 = search.getStats();
    std::uint64_t actual2 = sharedCounter->load();
    assert(!stop2->load());
    assert(actual2 == actual1);
    assert(stats2.nodes == actual2);
  }

  // Exchange sacrifice to free an advanced passer should be found
  {
    model::ChessGame game;
    game.setPosition("8/5k2/5p2/pp6/2pB4/P1P3K1/1n1r1P2/1R6 b - - 8 49");
    auto res = bot.findBestMove(game, 6, 0);
    assert(res.bestMove);
    model::Move expected(sq('d', 2), sq('d', 4));
    model::Move alt(sq('f', 6), sq('f', 5));
    auto inTop = std::any_of(res.topMoves.begin(), res.topMoves.end(), [&](const auto &mv)
                             { return mv.first == expected; });
    if (!inTop)
    {
      std::cerr << "Expected d2d4 to appear in top moves" << std::endl;
      return 1;
    }
    if (!res.bestMove)
    {
      std::cerr << "Expected a best move, got <none>\n";
      return 1;
    }

    if (*res.bestMove != expected && *res.bestMove != alt)
    {
      auto findMove = [&](const model::Move &mv)
      {
        return std::find_if(res.topMoves.begin(), res.topMoves.end(), [&](const auto &entry)
                            { return entry.first == mv; });
      };

      const auto expIt = findMove(expected);
      const auto bestIt = findMove(*res.bestMove);

      if (expIt == res.topMoves.end() || bestIt == res.topMoves.end())
      {
        std::cerr << "Unable to compare best move against expected top moves\n";
        return 1;
      }

      if (std::abs(bestIt->second - expIt->second) > 24)
      {
        std::cerr << "Best move " << move_to_uci(*res.bestMove)
                  << " differs too much in score from expected d2d4\n";
        return 1;
      }
    }
  }

  // Regression: avoid mate blunder in tactical FEN (should play Na5b3)
  {
    model::ChessGame game;
    game.setPosition("r1b1rk2/4qp2/p4R2/np4Q1/3PP3/PBPRp3/1P2N1Pb/7K b - - 0 27");
    auto res = bot.findBestMove(game, 4, 0);
    assert(res.bestMove);
    model::Move expected(sq('a', 5), sq('b', 3));
    if (!res.bestMove || *res.bestMove != expected)
    {
      std::cerr << "Expected best move a5b3, got "
                << (res.bestMove ? move_to_uci(*res.bestMove) : std::string("<none>")) << "\n";
      return 1;
    }

    bool found = std::any_of(res.topMoves.begin(), res.topMoves.end(), [&](const auto &mv)
                             { return mv.first == expected; });
    if (!found)
    {
      std::cerr << "Expected a5b3 to appear in top moves\n";
      return 1;
    }
  }

  // And the deeper search should keep h3h6 as the best move
  {
    model::ChessGame game;
    game.setPosition("6k1/3b1ppp/p7/3R4/2P2p2/7q/4KQ2/8 b - - 1 66");
    auto res = bot.findBestMove(game, 8, 0);
    assert(res.bestMove);
    model::Move expected(sq('h', 3), sq('h', 6));
    if (!res.bestMove || *res.bestMove != expected)
    {
      std::cerr << "Expected best move h3h6, got "
                << (res.bestMove ? move_to_uci(*res.bestMove) : std::string("<none>")) << "\n";
      return 1;
    }
  }

  // Stockfish-approved quiet queen retreat should be found
  {
    model::ChessGame game;
    game.setPosition("4kb1r/prQ1p1pp/4q3/3b1p2/1n1PP3/5P2/PP1N2PP/R1B1KB1R w KQk - 1 15");
    auto res = bot.findBestMove(game, 12, 0);
    assert(res.bestMove);
    model::Move expected(sq('c', 7), sq('c', 5));
    if (!res.bestMove || *res.bestMove != expected)
    {
      std::cerr << "Expected best move c7c5, got "
                << (res.bestMove ? move_to_uci(*res.bestMove) : std::string("<none>")) << "\n";
      return 1;
    }
  }

  // Regression: detect fianchetto bonus for a long-castled king protected by the b-pawn.
  {
    engine::Evaluator eval;

    auto evalFen = [&](const std::string &fen)
    {
      model::ChessGame game;
      game.setPosition(fen);
      auto &pos = game.getPositionRefForBot();
      return eval.evaluate(pos);
    };

    const std::string fenB2 = "4k3/8/8/8/8/8/1P6/2K5 w - - 0 1";
    const std::string fenB3 = "4k3/8/8/8/8/1P6/8/2K5 w - - 0 1";
    const std::string fenB4 = "4k3/8/8/8/1P6/8/8/2K5 w - - 0 1";

    const int scoreB2 = evalFen(fenB2);
    const int scoreB3 = evalFen(fenB3);
    const int scoreB4 = evalFen(fenB4);

    const int expectedSwing = FIANCHETTO_OK + FIANCHETTO_HOLE;

    assert(scoreB2 - scoreB4 >= expectedSwing - 2);
    assert(scoreB3 - scoreB4 >= expectedSwing - 2);
  }

  return 0;
}
