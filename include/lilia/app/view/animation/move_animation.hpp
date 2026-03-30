#pragma once

#include "lilia/chess/chess_types.hpp"
#include "lilia/app/view/ui/render/scene/piece_manager.hpp"
#include "i_animation.hpp"
#include <functional>

namespace lilia::app::view::animation
{

  class MoveAnim : public IAnimation
  {
  public:
    explicit MoveAnim(ui::PieceManager &pieceMgrRef, MousePos s, MousePos e,
                      chess::Square from = chess::NO_SQUARE, chess::Square to = chess::NO_SQUARE,
                      chess::PieceType promotion = chess::PieceType::None,
                      std::function<void()> onComplete = {});
    void update(float dt) override;
    void draw(sf::RenderWindow &window) override;
    [[nodiscard]] inline bool isFinished() const override;

  private:
    ui::PieceManager &m_piece_manager_ref;
    MousePos m_start_pos;
    MousePos m_end_pos;
    float m_elapsed = 0.f;
    float m_duration = ui::constant::ANIM_MOVE_SPEED;
    bool m_finish = false;
    chess::Square m_from;
    chess::Square m_to;
    chess::PieceType m_promotion;
    std::function<void()> m_on_complete;
  };

}
