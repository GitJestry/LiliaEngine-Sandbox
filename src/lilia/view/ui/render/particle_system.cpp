#include "lilia/view/ui/render/particle_system.hpp"

#include <SFML/Graphics/RenderWindow.hpp>

#include <algorithm>
#include <cmath>
#include <random>

namespace lilia::view
{

  ParticleSystem::ParticleSystem()
  {
    m_paletteListener = PaletteCache::get().addListener([this]
                                                        { onPaletteChanged(); });
  }

  ParticleSystem::~ParticleSystem()
  {
    PaletteCache::get().removeListener(m_paletteListener);
  }

  void ParticleSystem::emitConfetti(const sf::Vector2f &center, const sf::Vector2f &windowSize,
                                    std::size_t count)
  {
    static thread_local std::mt19937 rng{std::random_device{}()};

    std::uniform_real_distribution<float> xDist(center.x - windowSize.x / 2.f,
                                                center.x + windowSize.x / 2.f);
    std::uniform_real_distribution<float> vxDist(-50.f, 50.f);
    std::uniform_real_distribution<float> vyDist(-900.f, -600.f);
    std::uniform_real_distribution<float> radiusDist(1.5f, 6.0f);
    std::uniform_real_distribution<float> phaseDist(0.f, 6.2831853f);

    const float startY = center.y + windowSize.y / 2.f;

    if (m_particles.capacity() < m_particles.size() + count)
    {
      m_particles.reserve(m_particles.size() + count);
    }

    // Updated: use PaletteCache::palette() (consistent with ThemeCache)
    const auto p = PaletteCache::get().palette();
    const sf::Color base = p[ColorId::COL_TEXT];

    for (std::size_t i = 0; i < count; ++i)
    {
      const float x = xDist(rng);
      const float radius = radiusDist(rng);

      sf::CircleShape shape(radius);
      shape.setFillColor(base);
      shape.setOrigin(radius, radius);
      shape.setPosition({x, startY});

      const sf::Vector2f velocity{vxDist(rng), vyDist(rng)};
      constexpr float lifetime = 6.f;

      m_particles.push_back(
          Particle{std::move(shape), velocity, lifetime, startY, lifetime, false, phaseDist(rng)});
    }
  }

  void ParticleSystem::update(float dt)
  {
    static constexpr float upwardGravity = 400.f;
    static constexpr float downwardGravity = 80.f;

    static thread_local std::mt19937 rng{std::random_device{}()};
    std::uniform_real_distribution<float> jitterDist(-15.f, 15.f);

    for (auto it = m_particles.begin(); it != m_particles.end();)
    {
      it->lifetime -= dt;
      if (it->lifetime <= 0.f)
      {
        it = m_particles.erase(it);
        continue;
      }

      if (!it->falling && it->velocity.y >= 0.f)
        it->falling = true;

      const float gravity = it->falling ? downwardGravity : upwardGravity;
      it->velocity.y += gravity * dt;
      it->velocity.x += jitterDist(rng) * dt;

      const float t = it->totalLifetime - it->lifetime;
      const float wiggle = std::sin(t * 8.f + it->phase) * 20.f;

      it->shape.move({(it->velocity.x + wiggle) * dt, it->velocity.y * dt});

      if (it->lifetime < 3.f)
      {
        sf::Color c = it->shape.getFillColor();
        c.a = static_cast<sf::Uint8>(255 * std::clamp(it->lifetime / 3.f, 0.f, 1.f));
        it->shape.setFillColor(c);
      }

      if (it->shape.getPosition().y >= it->floorY)
      {
        it = m_particles.erase(it);
      }
      else
      {
        ++it;
      }
    }
  }

  void ParticleSystem::render(sf::RenderWindow &window) const
  {
    for (const auto &p : m_particles)
    {
      window.draw(p.shape);
    }
  }

  void ParticleSystem::onPaletteChanged()
  {
    // Updated: use PaletteCache::palette() (consistent with ThemeCache)
    const auto p = PaletteCache::get().palette();
    const sf::Color base = p[ColorId::COL_TEXT];

    // Keep particle alpha (fade state), update only RGB.
    for (auto &particle : m_particles)
    {
      sf::Color c = base;
      c.a = particle.shape.getFillColor().a;
      particle.shape.setFillColor(c);
    }
  }

} // namespace lilia::view
