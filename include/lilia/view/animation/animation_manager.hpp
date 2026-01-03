#pragma once

#include <memory>
#include <unordered_map>

#include "lilia/view/ui/render/entity.hpp"
#include "i_animation.hpp"

namespace lilia::view::animation
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

    void add(Entity::ID_type entityID, std::unique_ptr<IAnimation> anim);

    void declareHighlightLevel(Entity::ID_type entityID);
    void endAnim(Entity::ID_type entityID);
    [[nodiscard]] bool isAnimating(Entity::ID_type entityID) const;
    void update(float dt);
    void draw(sf::RenderWindow &window);
    void highlightLevelDraw(sf::RenderWindow &window);

    void addOrReplace(Entity::ID_type entityID, std::unique_ptr<IAnimation> anim,
                      AnimLayer layer = AnimLayer::Base);

    void cancelAll(Entity::ID_type entityID);
    void cancelAll();

    bool hasInAnyLayer(Entity::ID_type entityID) const;

  private:
    std::unordered_map<Entity::ID_type, std::unique_ptr<IAnimation>> m_highlight_level_animations;
    std::unordered_map<Entity::ID_type, std::unique_ptr<IAnimation>> m_animations;
  };

} // namespace lilia::view::animation
