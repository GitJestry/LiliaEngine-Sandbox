#pragma once

#include "lilia/app/view/ui/render/scene/piece_manager.hpp"
#include "i_animation.hpp"

namespace lilia::app::view::animation
{

  class PiecePlaceholderAnim : public IAnimation
  {
  public:
    explicit PiecePlaceholderAnim(ui::PieceManager &pieceMgrRef, chess::Square pieceSq);
    void update(float dt) override;
    void draw(sf::RenderWindow &window) override;
    [[nodiscard]] inline bool isFinished() const override;

  private:
    ui::PieceManager &m_piece_manager_ref;
    chess::Square m_piece_square;
  };

}
