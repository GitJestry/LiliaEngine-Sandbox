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
   * High-level game wrapper used by the app layer.
   * It owns a Position, exposes legal move execution, FEN import/export, and basic game-state queries.
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
    // Generates pseudo-legal moves and filters them by make/unmake legality.
    const std::vector<Move> &generateLegalMoves();
    std::optional<Move> getMove(Square from, Square to);

    bool isKingInCheck(Color from) const;
    Square getRookSquareFromCastleside(CastleSide castleSide, Color side);
    Square getKingSquare(Color color);
    GameResult getResult();
    void setResult(GameResult res);
    // Exposes the underlying position for engine-side consumers.
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
