#pragma once

#include "lilia/chess/chess_types.hpp"
#include "lilia/app/view/ui/render/scene/piece_manager.hpp"
#include "i_animation.hpp"

namespace lilia::app::view::animation
{

  class SnapToSquareAnim : public IAnimation
  {
  public:
    explicit SnapToSquareAnim(ui::PieceManager &pieceMgrRef, chess::Square pieceSq, MousePos s,
                              MousePos e);
    void update(float dt) override;
    void draw(sf::RenderWindow &window) override;
    [[nodiscard]] inline bool isFinished() const override;

  private:
    ui::PieceManager &m_piece_manager_ref;
    chess::Square m_piece_square;
    MousePos m_start_pos;
    MousePos m_end_pos;
    float m_elapsed = 0.f;
    float m_duration = ui::constant::ANIM_SNAP_SPEED;
    bool m_finish = false;
  };

}
