#pragma once

#include <deque>
#include <optional>
#include <string>
#include <vector>

#include "move_generator.hpp"
#include "position.hpp"
#include "zobrist.hpp"

namespace lilia::chess
{
  class ChessGame
  {
  public:
    ChessGame();

    void setPosition(const std::string &fen);
    void buildHash();

    bool doMove(Square from, Square to, PieceType promotion = PieceType::None);
    bool doMoveUCI(const std::string &uciMove);

    Piece getPiece(Square sq) const;
    const GameState &getGameState() const;

    // Generates pseudo-legal moves and filters them by make/unmake legality.
    const std::vector<Move> &generateLegalMoves();
    std::optional<Move> getMove(Square from, Square to);

    bool isKingInCheck(Color side) const;
    Square getRookSquareFromCastleside(CastleSide castleSide, Color side);
    Square getKingSquare(Color color);
    GameResult getResult() const;
    void setResult(GameResult res);

    Position &getPositionRefForBot();
    const Position &getPositionRefForBot() const;

    std::string getFen() const;
    void checkGameResult();

  private:
    MoveGenerator m_move_gen;
    Position m_position;
    GameResult m_result = GameResult::Ongoing;

    std::vector<Move> m_pseudo_moves;
    std::vector<Move> m_legal_moves;

    // Stable ownership for committed position states.
    std::deque<StateInfo> m_stateHistory;
  };

}
