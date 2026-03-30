#pragma once

#include <vector>

#include "promotion.hpp"

namespace lilia::app::view::ui
{

  class PromotionManager
  {
  public:
    PromotionManager() = default;

    bool hasOptions();
    void render(sf::RenderWindow &window);
    void fillOptions(MousePos prPos, chess::Color c, bool upwards);
    void removeOptions();
    MousePos getCenterPosition();
    chess::PieceType clickedOnType(MousePos mousePos);

  private:
    std::vector<Promotion> m_promotions;
  };

}
