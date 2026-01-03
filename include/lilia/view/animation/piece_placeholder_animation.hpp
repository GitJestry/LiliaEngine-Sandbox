#pragma once

#include "lilia/view/ui/render/scene/piece_manager.hpp"
#include "i_animation.hpp"

namespace lilia::view::animation
{

  class PiecePlaceholderAnim : public IAnimation
  {
  public:
    explicit PiecePlaceholderAnim(PieceManager &pieceMgrRef, core::Square pieceSq);
    void update(float dt) override;
    void draw(sf::RenderWindow &window) override;
    [[nodiscard]] inline bool isFinished() const override;

  private:
    PieceManager &m_piece_manager_ref;
    core::Square m_piece_square;
  };

}
