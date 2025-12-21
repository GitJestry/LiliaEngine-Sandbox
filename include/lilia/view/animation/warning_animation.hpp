#pragma once
#include "../entity.hpp"
#include "i_animation.hpp"

namespace lilia::view::animation {

class WarningAnim : public IAnimation {
 public:
  WarningAnim(Entity::Position ksqPos);

  void update(float dt) override;
  void draw(sf::RenderWindow& window) override;
  [[nodiscard]] inline bool isFinished() const override;

 private:
  Entity m_warning_highlight;
  float m_elapsed = 0.f;                
  const float m_total_duration = 2.0f;
  const float m_blink_period = 0.2f;
  bool m_finish = false;

};

}
