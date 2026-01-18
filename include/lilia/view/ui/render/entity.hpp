#pragma once

#include <SFML/Graphics/Sprite.hpp>
#include <SFML/System/Vector2.hpp>
#include <atomic>

namespace sf
{
  class RenderWindow;
}

namespace lilia::view
{

  class Entity
  {
  public:
    using Position = sf::Vector2f;
    using ID_type = size_t;

    Entity(const sf::Texture &texture);
    Entity(Position pos);
    Entity(const sf::Texture &texture, Position pos);
    Entity();
    virtual ~Entity() = default;

    virtual void setPosition(const Position &pos);

    [[nodiscard]] Position getPosition() const;

    [[nodiscard]] Position getOriginalSize() const;

    [[nodiscard]] Position getCurrentSize() const;

    void setOriginToCenter();
    void setOrigin(Entity::Position org);

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

} // namespace lilia::view
