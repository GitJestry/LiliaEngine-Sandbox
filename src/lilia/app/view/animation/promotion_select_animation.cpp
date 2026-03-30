#include "lilia/app/view/animation/promotion_select_animation.hpp"

#include "lilia/app/view/ui/render/scene/promotion.hpp"
#include "lilia/app/view/ui/render/render_constants.hpp"
#include "lilia/app/view/ui/render/resource_table.hpp"

namespace lilia::app::view::animation
{

  PromotionSelectAnim::PromotionSelectAnim(MousePos prPos, ui::PromotionManager &prOptRef,
                                           chess::Color c, bool upwards)
      : m_promo_pos(prPos), m_promo_mgr_ref(prOptRef)
  {
    m_promo_mgr_ref.fillOptions(prPos, c, upwards);

    m_white_boarder.setTexture(ui::ResourceTable::getInstance().getTexture(std::string{ui::constant::tex::PROMOTION}));
    m_white_boarder.setOriginToCenter();
    m_white_boarder.setPosition(m_promo_mgr_ref.getCenterPosition());

    m_white_boarder_shadow.setTexture(
        ui::ResourceTable::getInstance().getTexture(std::string{ui::constant::tex::PROMOTION_SHADOW}));
    m_white_boarder_shadow.setOriginToCenter();
    m_white_boarder_shadow.setPosition(m_promo_mgr_ref.getCenterPosition() +
                                       MousePos{0.f, +4.f});
  }

  void PromotionSelectAnim::update(float dt) {}

  void PromotionSelectAnim::draw(sf::RenderWindow &window)
  {
    m_white_boarder_shadow.draw(window);
    m_white_boarder.draw(window);
    m_promo_mgr_ref.render(window);
  }
  [[nodiscard]] bool PromotionSelectAnim::isFinished() const
  {
    return !m_promo_mgr_ref.hasOptions();
  }

}
