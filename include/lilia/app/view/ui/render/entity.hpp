#pragma once

#include <SFML/Graphics/Sprite.hpp>
#include <atomic>
#include "lilia/app/view/mousepos.hpp"

namespace sf
{
  class RenderWindow;
}

namespace lilia::app::view::ui
{

  class Entity
  {
  public:
    using ID_type = size_t;

    Entity(const sf::Texture &texture);
    Entity(MousePos pos);
    Entity(const sf::Texture &texture, MousePos pos);
    Entity();
    virtual ~Entity() = default;

    virtual void setPosition(const MousePos &pos);

    [[nodiscard]] MousePos getPosition() const;

    [[nodiscard]] MousePos getOriginalSize() const;

    [[nodiscard]] MousePos getCurrentSize() const;

    void setOriginToCenter();
    void setOrigin(MousePos org);

    virtual void draw(sf::RenderWindow &window);

    void setTexture(const sf::Texture &texture);

    [[nodiscard]] const sf::Texture &getTexture() const;

    void setScale(float widthFraction, float heightFraction);

    void setTextureRect(const sf::IntRect &r);

    [[nodiscard]] ID_type getId() const;

  private:
    ID_type m_id;

    [[nodiscard]] static ID_type generateId()
    {
      static std::atomic_size_t counter{1};
      return counter.fetch_add(1, std::memory_order_relaxed);
    }

    sf::Sprite m_sprite;
  };

}
