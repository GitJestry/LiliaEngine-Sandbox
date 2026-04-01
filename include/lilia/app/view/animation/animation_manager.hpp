#pragma once

#include <memory>
#include <unordered_map>

#include "lilia/app/view/ui/render/entity.hpp"
#include "i_animation.hpp"

namespace lilia::app::view::animation
{
  enum class AnimLayer
  {
    Base,
    Highlight
  };

  class AnimationManager
  {
  public:
    AnimationManager() = default;

    void add(ui::Entity::ID_type entityID, std::unique_ptr<IAnimation> anim);

    void declareHighlightLevel(ui::Entity::ID_type entityID);
    void endAnim(ui::Entity::ID_type entityID);
    [[nodiscard]] bool isAnimating(ui::Entity::ID_type entityID) const;
    void update(float dt);
    void draw(sf::RenderWindow &window);
    void highlightLevelDraw(sf::RenderWindow &window);

    void addOrReplace(ui::Entity::ID_type entityID, std::unique_ptr<IAnimation> anim,
                      AnimLayer layer = AnimLayer::Base);

    void cancelAll(ui::Entity::ID_type entityID);
    void cancelAll();

    bool hasInAnyLayer(ui::Entity::ID_type entityID) const;

  private:
    std::unordered_map<ui::Entity::ID_type, std::unique_ptr<IAnimation>> m_highlight_level_animations;
    std::unordered_map<ui::Entity::ID_type, std::unique_ptr<IAnimation>> m_animations;
  };

}
