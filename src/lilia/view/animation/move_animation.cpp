#include "lilia/view/animation/move_animation.hpp"

#include <iostream>

namespace lilia::view::animation {

MoveAnim::MoveAnim(PieceManager& pieceMgrRef, Entity::Position s, Entity::Position e,
                   core::Square from, core::Square to, core::PieceType promotion,
                   std::function<void()> onComplete)
    : m_piece_manager_ref(pieceMgrRef),
      m_start_pos(s),
      m_end_pos(e),
      m_from(from),
      m_to(to),
      m_promotion(promotion),
      m_on_complete(std::move(onComplete)) {}

void MoveAnim::update(float dt) {
  m_elapsed += dt;
  float t = std::min(m_elapsed / m_duration, 1.f);
  Entity::Position pos = m_start_pos + t * (m_end_pos - m_start_pos);
  m_piece_manager_ref.setPieceToScreenPos(m_from, pos);

  if (t >= 1.f) {
    m_finish = true;
    m_piece_manager_ref.movePiece(m_from, m_to, m_promotion);
    if (m_on_complete) m_on_complete();
  }
}

void MoveAnim::draw(sf::RenderWindow& window) {
  if (!m_finish)
    m_piece_manager_ref.renderPiece(m_from, window);
  else
    m_piece_manager_ref.renderPiece(m_to, window);
}

[[nodiscard]] inline bool MoveAnim::isFinished() const {
  return m_finish;
}

};  // namespace lilia::view::animation
