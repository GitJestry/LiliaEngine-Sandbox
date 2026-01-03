#pragma once

#include <vector>

#include "promotion.hpp"

namespace lilia::view {

class PromotionManager {
 public:
  PromotionManager() = default;

  bool hasOptions();
  void render(sf::RenderWindow& window);
  void fillOptions(Entity::Position prPos, core::Color c, bool upwards);
  void removeOptions();
  Entity::Position getCenterPosition();
  core::PieceType clickedOnType(Entity::Position mousePos);

 private:
  std::vector<Promotion> m_promotions;
};

}  
