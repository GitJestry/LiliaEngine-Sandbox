#include "lilia/app/view/animation/warning_animation.hpp"

#include <cmath>

#include "lilia/app/view/ui/render/render_constants.hpp"
#include "lilia/app/view/ui/render/resource_table.hpp"

namespace lilia::app::view::animation
{

  WarningAnim::WarningAnim(MousePos ksqPos) : m_warning_highlight(ksqPos)
  {
    m_warning_highlight.setTexture(
        ui::ResourceTable::getInstance().getTexture(std::string{ui::constant::tex::WARNING_HL}));
    m_warning_highlight.setOriginToCenter();
    m_warning_highlight.setScale(ui::constant::SQUARE_PX_SIZE, ui::constant::SQUARE_PX_SIZE);
  }

  void WarningAnim::update(float dt)
  {
    if (m_finish)
      return;

    m_elapsed += dt;

    if (m_elapsed >= m_total_duration)
    {
      m_finish = true;
      return;
    }

    float phase = std::fmod(m_elapsed, m_blink_period * 2.f);
    if (phase < m_blink_period)
    {
      m_warning_highlight.setTexture(
          ui::ResourceTable::getInstance().getTexture(std::string{ui::constant::tex::WARNING_HL}));
    }
    else
    {
      m_warning_highlight.setTexture(
          ui::ResourceTable::getInstance().getTexture(std::string{ui::constant::tex::TRANSPARENT}));
    }
  }

  void WarningAnim::draw(sf::RenderWindow &window)
  {
    m_warning_highlight.draw(window);
  }
  [[nodiscard]] bool WarningAnim::isFinished() const
  {
    return m_finish;
  }

} // namespace lilia::view::animation
