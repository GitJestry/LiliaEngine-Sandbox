#pragma once

#include <optional>
#include <string>
#include <vector>

#include "move_generator.hpp"
#include "position.hpp"
#include "zobrist.hpp"

namespace lilia::chess
{

  /**
   * This is the definitive entry point for the controller to manage a game.
   * Beside this for move gen the MoveGenerator class can, and only should be used.
   * Any other class is not intended to be used by the controller and rather is for the engine itself.
   */
  class ChessGame
  {
  public:
    ChessGame();

    void setPosition(const std::string &fen);
    void buildHash();
    bool doMove(Square from, Square to,
                PieceType promotion = PieceType::None);
    bool doMoveUCI(const std::string &uciMove);

    Piece getPiece(Square sq);
    const GameState &getGameState();
    const std::vector<Move> &generateLegalMoves();
    std::optional<Move> getMove(Square from, Square to);

    bool isKingInCheck(Color from) const;
    Square getRookSquareFromCastleside(CastleSide castleSide, Color side);
    Square getKingSquare(Color color);
    GameResult getResult();
    void setResult(GameResult res);
    Position &getPositionRefForBot();

    std::string getFen() const;

    void checkGameResult();

  private:
    MoveGenerator m_move_gen;
    Position m_position;
    GameResult m_result;
    std::vector<Move> m_pseudo_moves;
    std::vector<Move> m_legal_moves;
  };

}
