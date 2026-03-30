#include "lilia/app/controller/selection_manager.hpp"

namespace lilia::app::controller
{

  SelectionManager::SelectionManager(view::ui::GameView &view)
      : m_view(view), m_selected_sq(chess::NO_SQUARE),
        m_hover_sq(chess::NO_SQUARE),
        m_last_move_squares{chess::NO_SQUARE, chess::NO_SQUARE} {}

  void SelectionManager::reset()
  {
    m_selected_sq = chess::NO_SQUARE;
    m_hover_sq = chess::NO_SQUARE;
    m_last_move_squares = {chess::NO_SQUARE, chess::NO_SQUARE};
  }

  void SelectionManager::highlightLastMove() const
  {
    if (m_last_move_squares.first != chess::NO_SQUARE)
      m_view.highlightSquare(m_last_move_squares.first);
    if (m_last_move_squares.second != chess::NO_SQUARE)
      m_view.highlightSquare(m_last_move_squares.second);
  }

  void SelectionManager::selectSquare(chess::Square sq)
  {
    m_view.highlightSquare(sq);
    m_selected_sq = sq;
  }

  void SelectionManager::deselectSquare()
  {
    m_view.clearNonPremoveHighlights();
    highlightLastMove();
    m_selected_sq = chess::NO_SQUARE;
  }

  void SelectionManager::clearLastMoveHighlight() const
  {
    if (m_last_move_squares.first != chess::NO_SQUARE)
      m_view.clearHighlightSquare(m_last_move_squares.first);
    if (m_last_move_squares.second != chess::NO_SQUARE)
      m_view.clearHighlightSquare(m_last_move_squares.second);
  }

  void SelectionManager::hoverSquare(chess::Square sq)
  {
    m_hover_sq = sq;
    m_view.highlightHoverSquare(m_hover_sq);
  }

  void SelectionManager::dehoverSquare()
  {
    if (m_hover_sq != chess::NO_SQUARE)
      m_view.clearHighlightHoverSquare(m_hover_sq);
    m_hover_sq = chess::NO_SQUARE;
  }

  void SelectionManager::setLastMove(chess::Square from, chess::Square to)
  {
    m_last_move_squares = {from, to};
  }

  std::pair<chess::Square, chess::Square> SelectionManager::getLastMove() const
  {
    return m_last_move_squares;
  }

  chess::Square SelectionManager::getSelectedSquare() const { return m_selected_sq; }

  chess::Square SelectionManager::getHoveredSquare() const { return m_hover_sq; }

} // namespace lilia::controller
