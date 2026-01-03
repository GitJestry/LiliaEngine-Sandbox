#include "lilia/controller/selection_manager.hpp"

namespace lilia::controller {

SelectionManager::SelectionManager(view::GameView &view)
    : m_view(view), m_selected_sq(core::NO_SQUARE),
      m_hover_sq(core::NO_SQUARE),
      m_last_move_squares{core::NO_SQUARE, core::NO_SQUARE} {}

void SelectionManager::reset() {
  m_selected_sq = core::NO_SQUARE;
  m_hover_sq = core::NO_SQUARE;
  m_last_move_squares = {core::NO_SQUARE, core::NO_SQUARE};
}

void SelectionManager::highlightLastMove() const {
  if (m_last_move_squares.first != core::NO_SQUARE)
    m_view.highlightSquare(m_last_move_squares.first);
  if (m_last_move_squares.second != core::NO_SQUARE)
    m_view.highlightSquare(m_last_move_squares.second);
}

void SelectionManager::selectSquare(core::Square sq) {
  m_view.highlightSquare(sq);
  m_selected_sq = sq;
}

void SelectionManager::deselectSquare() {
  m_view.clearNonPremoveHighlights();
  highlightLastMove();
  m_selected_sq = core::NO_SQUARE;
}

void SelectionManager::clearLastMoveHighlight() const {
  if (m_last_move_squares.first != core::NO_SQUARE)
    m_view.clearHighlightSquare(m_last_move_squares.first);
  if (m_last_move_squares.second != core::NO_SQUARE)
    m_view.clearHighlightSquare(m_last_move_squares.second);
}

void SelectionManager::hoverSquare(core::Square sq) {
  m_hover_sq = sq;
  m_view.highlightHoverSquare(m_hover_sq);
}

void SelectionManager::dehoverSquare() {
  if (m_hover_sq != core::NO_SQUARE)
    m_view.clearHighlightHoverSquare(m_hover_sq);
  m_hover_sq = core::NO_SQUARE;
}

void SelectionManager::setLastMove(core::Square from, core::Square to) {
  m_last_move_squares = {from, to};
}

std::pair<core::Square, core::Square> SelectionManager::getLastMove() const {
  return m_last_move_squares;
}

core::Square SelectionManager::getSelectedSquare() const { return m_selected_sq; }

core::Square SelectionManager::getHoveredSquare() const { return m_hover_sq; }

} // namespace lilia::controller

