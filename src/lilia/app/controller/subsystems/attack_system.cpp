#include "lilia/app/controller/subsystems/attack_system.hpp"

#include "lilia/chess/chess_game.hpp"
#include "lilia/app/view/ui/screens/game_view.hpp"
#include "lilia/app/controller/subsystems/legal_move_cache.hpp"

namespace lilia::app::controller
{

  AttackSystem::AttackSystem(view::ui::GameView &view, chess::ChessGame &game, LegalMoveCache &legal)
      : m_view(view), m_game(game), m_legal(legal)
  {
    m_pseudo.reserve(64);
    m_out.reserve(64);
  }

  const std::vector<chess::Square> &AttackSystem::attacks(chess::Square pieceSq) const
  {
    m_out.clear();
    if (!chess::validSquare(pieceSq))
      return m_out;

    const chess::PieceType vType = m_view.getPieceType(pieceSq);
    const chess::Color vCol = m_view.getPieceColor(pieceSq);
    const bool hasVirtual = (vType != chess::PieceType::None);
    const bool premoveContext = hasVirtual && (vCol != m_game.getGameState().sideToMove);

    if (premoveContext)
    {
      chess::Board board;
      board.clear();
      board.setPiece(pieceSq, {vType, vCol});

      chess::GameState st{};
      st.sideToMove = vCol;
      st.castlingRights = 0;
      st.enPassantSquare = chess::NO_SQUARE;

      if (vType == chess::PieceType::Pawn)
      {
        const int file = static_cast<int>(pieceSq) & 7;
        const int forward = (vCol == chess::Color::White) ? 8 : -8;
        const chess::Piece dummy{chess::PieceType::Pawn, ~vCol};
        if (file > 0)
          board.setPiece(static_cast<chess::Square>(static_cast<int>(pieceSq) + forward - 1), dummy);
        if (file < 7)
          board.setPiece(static_cast<chess::Square>(static_cast<int>(pieceSq) + forward + 1), dummy);
      }

      m_pseudo.clear();
      m_movegen.generatePseudoLegalMoves(board, st, m_pseudo);
      for (const auto &m : m_pseudo)
        if (m.from() == pieceSq)
          m_out.push_back(m.to());
      return m_out;
    }

    for (const auto &m : m_legal.legal())
      if (m.from() == pieceSq)
        m_out.push_back(m.to());
    return m_out;
  }

} // namespace lilia::controller
