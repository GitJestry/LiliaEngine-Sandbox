#include "lilia/controller/subsystems/attack_system.hpp"

#include "lilia/model/chess_game.hpp"
#include "lilia/model/position.hpp"
#include "lilia/view/game_view.hpp"
#include "lilia/controller/subsystems/legal_move_cache.hpp"

namespace lilia::controller {

namespace {
inline bool valid(core::Square sq) {
  return sq != core::NO_SQUARE;
}
}  // namespace

AttackSystem::AttackSystem(view::GameView& view, model::ChessGame& game, LegalMoveCache& legal)
    : m_view(view), m_game(game), m_legal(legal) {
  m_pseudo.reserve(64);
  m_out.reserve(64);
}

const std::vector<core::Square>& AttackSystem::attacks(core::Square pieceSq) const {
  m_out.clear();
  if (!valid(pieceSq)) return m_out;

  const core::PieceType vType = m_view.getPieceType(pieceSq);
  const core::Color vCol = m_view.getPieceColor(pieceSq);
  const bool hasVirtual = (vType != core::PieceType::None);
  const bool premoveContext = hasVirtual && (vCol != m_game.getGameState().sideToMove);

  if (premoveContext) {
    model::Board board;
    board.clear();
    board.setPiece(pieceSq, {vType, vCol});

    model::GameState st{};
    st.sideToMove = vCol;
    st.castlingRights = 0;
    st.enPassantSquare = core::NO_SQUARE;

    if (vType == core::PieceType::Pawn) {
      const int file = static_cast<int>(pieceSq) & 7;
      const int forward = (vCol == core::Color::White) ? 8 : -8;
      const model::bb::Piece dummy{core::PieceType::Pawn, ~vCol};
      if (file > 0)
        board.setPiece(static_cast<core::Square>(static_cast<int>(pieceSq) + forward - 1), dummy);
      if (file < 7)
        board.setPiece(static_cast<core::Square>(static_cast<int>(pieceSq) + forward + 1), dummy);
    }

    m_pseudo.clear();
    m_movegen.generatePseudoLegalMoves(board, st, m_pseudo);
    for (const auto& m : m_pseudo)
      if (m.from() == pieceSq) m_out.push_back(m.to());
    return m_out;
  }

  for (const auto& m : m_legal.legal())
    if (m.from() == pieceSq) m_out.push_back(m.to());
  return m_out;
}

}  // namespace lilia::controller
