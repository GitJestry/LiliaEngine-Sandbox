#include "lilia/app/view/ui/render/scene/promotion_manager.hpp"

#include "lilia/app/view/ui/render/render_constants.hpp"

namespace lilia::app::view::ui
{

  bool PromotionManager::hasOptions()
  {
    return !m_promotions.empty();
  }
  MousePos PromotionManager::getCenterPosition()
  {
    if (m_promotions.empty())
      return {0.f, 0.f};
    auto first = m_promotions.front().getPosition();
    auto last = m_promotions.back().getPosition();
    return {(first.x + last.x) * 0.5f, (first.y + last.y) * 0.5f};
  }
  void PromotionManager::fillOptions(MousePos prPos, chess::Color c, bool upwards)
  {
    removeOptions();
    constexpr chess::PieceType promotionTypes[] = {chess::PieceType::Knight, chess::PieceType::Bishop,
                                                   chess::PieceType::Rook, chess::PieceType::Queen};

    for (std::size_t i = 0; i < std::size(promotionTypes); i++)
    {
      float offset = static_cast<float>(i) * constant::SQUARE_PX_SIZE;
      MousePos pos =
          upwards ? MousePos{prPos.x, prPos.y - offset}
                  : MousePos{prPos.x, prPos.y + offset};
      m_promotions.push_back(Promotion(pos, promotionTypes[i], c));
    }
  }
  void PromotionManager::removeOptions()
  {
    m_promotions.clear();
  }

  void PromotionManager::render(sf::RenderWindow &window)
  {
    for (auto &opt : m_promotions)
      opt.draw(window);
  }

  bool inArea(float us, float other, int area)
  {
    return (us >= other - area && us <= other + area);
  }

  chess::PieceType PromotionManager::clickedOnType(MousePos mousePos)
  {
    for (auto &opt : m_promotions)
    {
      if (inArea(opt.getPosition().x, mousePos.x, constant::SQUARE_PX_SIZE * 0.5) &&
          inArea(opt.getPosition().y, mousePos.y, constant::SQUARE_PX_SIZE * 0.5))
        return opt.getType();
    }
    return chess::PieceType::None;
  }

}
