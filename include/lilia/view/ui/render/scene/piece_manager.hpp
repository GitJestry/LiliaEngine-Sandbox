#pragma once

#include <string>
#include <unordered_map>
#include <unordered_set>

#include "lilia/controller/mousepos.hpp"
#include "lilia/view/ui/style/palette_cache.hpp"
#include "lilia/view/ui/views/board_view.hpp"
#include "piece_node.hpp"

namespace lilia::view
{

  namespace animation
  {
    class ChessAnimator;
  }

  class PieceManager
  {
  public:
    PieceManager(const BoardView &boardRef);
    ~PieceManager();

    void initFromFen(const std::string &fen);

    [[nodiscard]] Entity::ID_type getPieceID(core::Square pos) const;
    [[nodiscard]] bool isSameColor(core::Square sq1, core::Square sq2) const;

    void movePiece(core::Square from, core::Square to, core::PieceType promotion);
    void addPiece(core::PieceType type, core::Color color, core::Square pos);
    void removePiece(core::Square pos);
    void removeAll();

    // Resolve the piece type/color on the given square. These helpers consult
    // the main board state as well as any hidden or stashed pieces created
    // during premove previews so callers always receive the actual piece info.
    [[nodiscard]] core::PieceType getPieceType(core::Square pos) const;
    [[nodiscard]] core::Color getPieceColor(core::Square pos) const;
    [[nodiscard]] bool hasPieceOnSquare(core::Square pos) const;
    [[nodiscard]] Entity::Position getPieceSize(core::Square pos) const;
    void setPieceToSquareScreenPos(core::Square from, core::Square to);
    void setPieceToScreenPos(core::Square pos, core::MousePos mousePos);
    void setPieceToScreenPos(core::Square pos, Entity::Position entityPos);

    void renderPieces(sf::RenderWindow &window, const animation::ChessAnimator &chessAnimRef);
    void renderPiece(core::Square pos, sf::RenderWindow &window);

    // Visual-only helpers for premove previews
    // Optional promotion piece allows the ghost to differ from the original type
    void setPremovePiece(core::Square from, core::Square to,
                         core::PieceType promotion = core::PieceType::None);
    void clearPremovePieces(bool restore = true);
    void consumePremoveGhost(core::Square from, core::Square to);
    void applyPremoveInstant(core::Square from, core::Square to,
                             core::PieceType promotion = core::PieceType::None);
    void renderPremoveGhosts(sf::RenderWindow &window, const animation::ChessAnimator &chessAnimRef);
    void reconcileHiddenFromGhosts();

  private:
    Entity::Position createPiecePositon(core::Square pos);

    const BoardView &m_board_view_ref;

    std::unordered_map<core::Square, Piece> m_pieces;
    // Pieces rendered for premove visualization without affecting board state
    std::unordered_map<core::Square, Piece> m_premove_pieces;
    // Squares hidden from the main piece map during premove preview
    std::unordered_set<core::Square> m_hidden_squares;
    // Backup of pieces temporarily removed due to premove captures
    std::unordered_map<core::Square, Piece> m_captured_backup;
    std::unordered_map<core::Square, core::Square> m_premove_origin;

    // UPDATED: listener tied to PaletteCache (new system)
    PaletteCache::ListenerID m_paletteListener{0};

    void onPaletteChanged();
  };

} // namespace lilia::view
