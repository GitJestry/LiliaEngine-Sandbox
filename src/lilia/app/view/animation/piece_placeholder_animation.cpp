#include "lilia/app/view/animation/piece_placeholder_animation.hpp"

namespace lilia::app::view::animation
{

  PiecePlaceholderAnim::PiecePlaceholderAnim(ui::PieceManager &pieceMgrRef, chess::Square pieceSq)
      : m_piece_manager_ref(pieceMgrRef), m_piece_square(pieceSq) {}
  void PiecePlaceholderAnim::update(float dt) {}
  void PiecePlaceholderAnim::draw(sf::RenderWindow &window)
  {
    m_piece_manager_ref.renderPiece(m_piece_square, window);
  }
  [[nodiscard]] inline bool PiecePlaceholderAnim::isFinished() const
  {
    return false;
  }

};
