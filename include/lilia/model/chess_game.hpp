#pragma once

#include <optional>
#include <string>
#include <vector>

#include "../constants.hpp"
#include "move_generator.hpp"
#include "position.hpp"
#include "zobrist.hpp"

namespace lilia::model
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
    bool doMove(core::Square from, core::Square to,
                core::PieceType promotion = core::PieceType::None);
    bool doMoveUCI(const std::string &uciMove);

    bb::Piece getPiece(core::Square sq);
    const GameState &getGameState();
    const std::vector<Move> &generateLegalMoves();
    const std::vector<Move> &generatePseudoLegalMoves();
    std::optional<Move> getMove(core::Square from, core::Square to);

    bool isKingInCheck(core::Color from) const;
    core::Square getRookSquareFromCastleside(CastleSide castleSide, core::Color side);
    core::Square getKingSquare(core::Color color);
    core::GameResult getResult();
    void setResult(core::GameResult res);
    Position &getPositionRefForBot();

    std::string getFen() const;

    void checkGameResult();

  private:
    MoveGenerator m_move_gen;
    Position m_position;
    core::GameResult m_result;
    std::vector<Move> m_pseudo_moves;
    std::vector<Move> m_legal_moves;
  };

} // namespace lilia::model
