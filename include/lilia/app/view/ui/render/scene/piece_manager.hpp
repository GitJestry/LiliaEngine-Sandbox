#pragma once

#include <string>
#include <unordered_map>
#include <unordered_set>

#include "lilia/app/mousepos.hpp"
#include "lilia/app/view/ui/style/palette_cache.hpp"
#include "lilia/app/view/ui/views/board_view.hpp"
#include "piece_node.hpp"

namespace lilia::app::view::animation
{
  class ChessAnimator;
}

namespace lilia::app::view::ui
{

  class PieceManager
  {
  public:
    PieceManager(const BoardView &boardRef);
    ~PieceManager();

    void initFromFen(const std::string &fen);

    [[nodiscard]] Entity::ID_type getPieceID(chess::Square pos) const;
    [[nodiscard]] bool isSameColor(chess::Square sq1, chess::Square sq2) const;

    void movePiece(chess::Square from, chess::Square to, chess::PieceType promotion);
    void addPiece(chess::PieceType type, chess::Color color, chess::Square pos);
    void removePiece(chess::Square pos);
    void removeAll();

    // Resolve the piece type/color on the given square. These helpers consult
    // the main board state as well as any hidden or stashed pieces created
    // during premove previews so callers always receive the actual piece info.
    [[nodiscard]] chess::PieceType getPieceType(chess::Square pos) const;
    [[nodiscard]] chess::Color getPieceColor(chess::Square pos) const;
    [[nodiscard]] bool hasPieceOnSquare(chess::Square pos) const;
    [[nodiscard]] MousePos getPieceSize(chess::Square pos) const;
    void setPieceToSquareScreenPos(chess::Square from, chess::Square to);
    void setPieceToScreenPos(chess::Square pos, MousePos mousePos);

    void renderPieces(sf::RenderWindow &window, const animation::ChessAnimator &chessAnimRef);
    void renderPiece(chess::Square pos, sf::RenderWindow &window);

    // Visual-only helpers for premove previews
    // Optional promotion piece allows the ghost to differ from the original type
    void setPremovePiece(chess::Square from, chess::Square to,
                         chess::PieceType promotion = chess::PieceType::None);
    void clearPremovePieces(bool restore = true);
    void consumePremoveGhost(chess::Square from, chess::Square to);
    void applyPremoveInstant(chess::Square from, chess::Square to,
                             chess::PieceType promotion = chess::PieceType::None);
    void renderPremoveGhosts(sf::RenderWindow &window, const animation::ChessAnimator &chessAnimRef);
    void reconcileHiddenFromGhosts();

  private:
    MousePos createPiecePositon(chess::Square pos);

    const BoardView &m_board_view_ref;

    std::unordered_map<chess::Square, PieceNode> m_pieces;
    // Pieces rendered for premove visualization without affecting board state
    std::unordered_map<chess::Square, PieceNode> m_premove_pieces;
    // Squares hidden from the main piece map during premove preview
    std::unordered_set<chess::Square> m_hidden_squares;
    // Backup of pieces temporarily removed due to premove captures
    std::unordered_map<chess::Square, PieceNode> m_captured_backup;
    std::unordered_map<chess::Square, chess::Square> m_premove_origin;

    // UPDATED: listener tied to PaletteCache (new system)
    PaletteCache::ListenerID m_paletteListener{0};

    void onPaletteChanged();
  };

} // namespace lilia::view
