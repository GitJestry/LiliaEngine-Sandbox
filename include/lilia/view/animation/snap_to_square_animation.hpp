#pragma once

#include "lilia/chess_types.hpp"
#include "lilia/view/ui/render/scene/piece_manager.hpp"
#include "i_animation.hpp"

namespace lilia::view::animation
{

  class SnapToSquareAnim : public IAnimation
  {
  public:
    explicit SnapToSquareAnim(PieceManager &pieceMgrRef, core::Square pieceSq, Entity::Position s,
                              Entity::Position e);
    void update(float dt) override;
    void draw(sf::RenderWindow &window) override;
    [[nodiscard]] inline bool isFinished() const override;

  private:
    PieceManager &m_piece_manager_ref;
    core::Square m_piece_square;
    Entity::Position m_start_pos;
    Entity::Position m_end_pos;
    float m_elapsed = 0.f;
    float m_duration = constant::ANIM_SNAP_SPEED;
    bool m_finish = false;
  };

}
