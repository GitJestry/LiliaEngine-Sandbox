#pragma once

#include <optional>
#include <string>
#include <vector>

#include "../constants.hpp"
#include "move_generator.hpp"
#include "position.hpp"
#include "zobrist.hpp"

namespace lilia::model {

class ChessGame {
 public:
  ChessGame();

  void setPosition(const std::string& fen);
  void buildHash();
  bool doMove(core::Square from, core::Square to,
              core::PieceType promotion = core::PieceType::None);
  bool doMoveUCI(const std::string& uciMove);

  bb::Piece getPiece(core::Square sq);
  const GameState& getGameState();
  const std::vector<Move>& generateLegalMoves();
  std::optional<Move> getMove(core::Square from, core::Square to);

  bool isKingInCheck(core::Color from) const;
  core::Square getRookSquareFromCastleside(CastleSide castleSide, core::Color side);
  core::Square getKingSquare(core::Color color);
  core::GameResult getResult();
  void setResult(core::GameResult res);
  Position& getPositionRefForBot();

  std::string getFen() const; 

  void checkGameResult();

 private:
  MoveGenerator m_move_gen;
  Position m_position;
  core::GameResult m_result;
  std::vector<Move> m_pseudo_moves;
  std::vector<Move> m_legal_moves;
};

}  // namespace lilia::model
