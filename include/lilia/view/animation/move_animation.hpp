#pragma once

#include "lilia/chess_types.hpp"
#include "lilia/view/ui/render/scene/piece_manager.hpp"
#include "i_animation.hpp"
#include <functional>

namespace lilia::view::animation
{

  class MoveAnim : public IAnimation
  {
  public:
    explicit MoveAnim(PieceManager &pieceMgrRef, Entity::Position s, Entity::Position e,
                      core::Square from = core::NO_SQUARE, core::Square to = core::NO_SQUARE,
                      core::PieceType promotion = core::PieceType::None,
                      std::function<void()> onComplete = {});
    void update(float dt) override;
    void draw(sf::RenderWindow &window) override;
    [[nodiscard]] inline bool isFinished() const override;

  private:
    PieceManager &m_piece_manager_ref;
    Entity::Position m_start_pos;
    Entity::Position m_end_pos;
    float m_elapsed = 0.f;
    float m_duration = constant::ANIM_MOVE_SPEED;
    bool m_finish = false;
    core::Square m_from;
    core::Square m_to;
    core::PieceType m_promotion;
    std::function<void()> m_on_complete;
  };

}
