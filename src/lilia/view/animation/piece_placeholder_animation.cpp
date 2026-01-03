#include "lilia/view/animation/piece_placeholder_animation.hpp"

namespace lilia::view::animation {

PiecePlaceholderAnim::PiecePlaceholderAnim(PieceManager& pieceMgrRef, core::Square pieceSq)
    : m_piece_manager_ref(pieceMgrRef), m_piece_square(pieceSq) {}
void PiecePlaceholderAnim::update(float dt) {}
void PiecePlaceholderAnim::draw(sf::RenderWindow& window) {
  m_piece_manager_ref.renderPiece(m_piece_square, window);
}
[[nodiscard]] inline bool PiecePlaceholderAnim::isFinished() const {
  return false;
}

};  
