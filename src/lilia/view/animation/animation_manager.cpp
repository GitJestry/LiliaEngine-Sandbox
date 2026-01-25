#include "lilia/view/animation/animation_manager.hpp"

#include <algorithm>
#include <iostream>

namespace lilia::view::animation
{

  void AnimationManager::add(Entity::ID_type entityID, std::unique_ptr<IAnimation> anim)
  {
    addOrReplace(entityID, std::move(anim), AnimLayer::Base);
  }

  void AnimationManager::addOrReplace(Entity::ID_type entityID, std::unique_ptr<IAnimation> anim,
                                      AnimLayer layer)
  {
    if (auto it = m_animations.find(entityID); it != m_animations.end())
    {
      m_animations.erase(it);
    }
    if (auto it = m_highlight_level_animations.find(entityID);
        it != m_highlight_level_animations.end())
    {
      m_highlight_level_animations.erase(it);
    }

    if (layer == AnimLayer::Highlight)
    {
      m_highlight_level_animations[entityID] = std::move(anim);
    }
    else
    {
      m_animations[entityID] = std::move(anim);
    }
  }

  [[nodiscard]] bool AnimationManager::isAnimating(Entity::ID_type entityID) const
  {
    return m_animations.find(entityID) != m_animations.end();
  }

  [[nodiscard]] bool AnimationManager::hasInAnyLayer(Entity::ID_type entityID) const
  {
    return m_animations.find(entityID) != m_animations.end() ||
           m_highlight_level_animations.find(entityID) != m_highlight_level_animations.end();
  }

  void AnimationManager::declareHighlightLevel(Entity::ID_type entityID)
  {
    if (m_highlight_level_animations.find(entityID) != m_highlight_level_animations.end())
      return;

    if (auto it = m_animations.find(entityID); it != m_animations.end())
    {
      m_highlight_level_animations[entityID] = std::move(it->second);
      m_animations.erase(it);
      return;
    }
  }

  void AnimationManager::endAnim(Entity::ID_type entityID)
  {
    m_animations.erase(entityID);
  }

  void AnimationManager::cancelAll(Entity::ID_type entityID)
  {
    if (auto it = m_animations.find(entityID); it != m_animations.end())
    {
      m_animations.erase(it);
    }
    if (auto it2 = m_highlight_level_animations.find(entityID);
        it2 != m_highlight_level_animations.end())
    {
      m_highlight_level_animations.erase(it2);
    }
  }

  void AnimationManager::cancelAll()
  {
    m_animations.clear();
    m_highlight_level_animations.clear();
  }

  void AnimationManager::update(float dt)
  {
    // Base-Layer
    for (auto it = m_animations.begin(); it != m_animations.end();)
    {
      it->second->update(dt);
      if (it->second->isFinished())
      {
        it = m_animations.erase(it);
      }
      else
      {
        ++it;
      }
    }
    // Highlight-Layer
    for (auto it = m_highlight_level_animations.begin(); it != m_highlight_level_animations.end();)
    {
      it->second->update(dt);
      if (it->second->isFinished())
      {
        it = m_highlight_level_animations.erase(it);
      }
      else
      {
        ++it;
      }
    }
  }

  void AnimationManager::draw(sf::RenderWindow &window)
  {
    for (auto &[id, anim] : m_animations)
      anim->draw(window);
  }

  void AnimationManager::highlightLevelDraw(sf::RenderWindow &window)
  {
    for (auto &[id, anim] : m_highlight_level_animations)
      anim->draw(window);
  }

} // namespace lilia::view::animation
