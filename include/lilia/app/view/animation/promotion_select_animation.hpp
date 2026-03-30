#pragma once

#include "lilia/chess/chess_types.hpp"
#include "lilia/app/view/ui/render/scene/promotion_manager.hpp"
#include "i_animation.hpp"

namespace lilia::app::view::animation
{

  class PromotionSelectAnim : public IAnimation
  {
  public:
    PromotionSelectAnim(MousePos prPos, ui::PromotionManager &prOptRef, chess::Color c,
                        bool upwards);
    void update(float dt) override;
    void draw(sf::RenderWindow &window) override;
    [[nodiscard]] inline bool isFinished() const override;

  private:
    MousePos m_promo_pos;
    ui::PromotionManager &m_promo_mgr_ref;
    ui::Entity m_white_boarder;
    ui::Entity m_white_boarder_shadow;
  };

}
