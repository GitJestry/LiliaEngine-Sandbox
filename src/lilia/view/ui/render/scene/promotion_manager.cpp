#include "lilia/view/ui/render/scene/promotion_manager.hpp"

#include "lilia/view/ui/render/render_constants.hpp"

namespace lilia::view
{

  bool PromotionManager::hasOptions()
  {
    return !m_promotions.empty();
  }
  Entity::Position PromotionManager::getCenterPosition()
  {
    if (m_promotions.empty())
      return {0.f, 0.f};
    auto first = m_promotions.front().getPosition();
    auto last = m_promotions.back().getPosition();
    return {(first.x + last.x) * 0.5f, (first.y + last.y) * 0.5f};
  }
  void PromotionManager::fillOptions(Entity::Position prPos, core::Color c, bool upwards)
  {
    removeOptions();
    constexpr core::PieceType promotionTypes[] = {core::PieceType::Knight, core::PieceType::Bishop,
                                                  core::PieceType::Rook, core::PieceType::Queen};

    for (std::size_t i = 0; i < std::size(promotionTypes); i++)
    {
      float offset = static_cast<float>(i) * constant::SQUARE_PX_SIZE;
      Entity::Position pos =
          upwards ? Entity::Position{prPos.x, prPos.y - offset}
                  : Entity::Position{prPos.x, prPos.y + offset};
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

  core::PieceType PromotionManager::clickedOnType(Entity::Position mousePos)
  {
    for (auto &opt : m_promotions)
    {
      if (inArea(opt.getPosition().x, mousePos.x, constant::SQUARE_PX_SIZE * 0.5) &&
          inArea(opt.getPosition().y, mousePos.y, constant::SQUARE_PX_SIZE * 0.5))
        return opt.getType();
    }
    return core::PieceType::None;
  }

}
