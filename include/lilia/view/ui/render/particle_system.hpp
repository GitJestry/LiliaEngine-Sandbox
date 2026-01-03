#pragma once

#include <SFML/Graphics/CircleShape.hpp>
#include <SFML/System/Vector2.hpp>

#include <cstddef>
#include <cstdint>
#include <vector>

#include "lilia/view/ui/style/palette_cache.hpp"

namespace sf
{
  class RenderWindow;
}

namespace lilia::view
{

  class ParticleSystem
  {
  public:
    ParticleSystem();
    ~ParticleSystem();

    ParticleSystem(const ParticleSystem &) = delete;
    ParticleSystem &operator=(const ParticleSystem &) = delete;

    struct Particle
    {
      sf::CircleShape shape;
      sf::Vector2f velocity;
      float lifetime{};
      float floorY{};        // y position where particle should disappear
      float totalLifetime{}; // initial lifetime (for effects like fade)
      bool falling{false};
      float phase{}; // per-particle wiggle phase
    };

    void emitConfetti(const sf::Vector2f &center, const sf::Vector2f &windowSize, std::size_t count);
    void update(float dt);
    void render(sf::RenderWindow &window) const;

    void clear() { m_particles.clear(); }
    [[nodiscard]] bool empty() const noexcept { return m_particles.empty(); }

  private:
    void onPaletteChanged();

    std::vector<Particle> m_particles;
    PaletteCache::ListenerID m_paletteListener{0};
  };

} // namespace lilia::view
