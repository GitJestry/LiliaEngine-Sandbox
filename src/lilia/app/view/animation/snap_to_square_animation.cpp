#include "lilia/app/view/animation/snap_to_square_animation.hpp"

namespace lilia::app::view::animation
{

  SnapToSquareAnim::SnapToSquareAnim(ui::PieceManager &pieceMgrRef, chess::Square pieceSq,
                                     MousePos s, MousePos e)
      : m_piece_manager_ref(pieceMgrRef), m_piece_square(pieceSq), m_start_pos(s), m_end_pos(e) {}
  void SnapToSquareAnim::update(float dt)
  {
    m_elapsed += dt;
    float t = std::min(m_elapsed / m_duration, 1.f);
    MousePos pos = m_start_pos + t * (m_end_pos - m_start_pos);
    m_piece_manager_ref.setPieceToScreenPos(m_piece_square, pos);

    if (t >= 1.f)
    {
      m_finish = true;
    }
  }
  void SnapToSquareAnim::draw(sf::RenderWindow &window)
  {
    m_piece_manager_ref.renderPiece(m_piece_square, window);
  }
  [[nodiscard]] inline bool SnapToSquareAnim::isFinished() const
  {
    return m_finish;
  }

};
