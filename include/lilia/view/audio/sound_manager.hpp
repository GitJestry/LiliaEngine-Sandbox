#pragma once

#include <SFML/Audio.hpp>
#include <memory>
#include <string>
#include <unordered_map>

namespace lilia::view::sound {

enum class Effect {
  PlayerMove,
  EnemyMove,
  Capture,
  Check,
  Warning,
  Castle,
  Promotion,
  GameBegins,
  GameEnds,
  Premove
};

class SoundManager {
 public:
  SoundManager() = default;
  ~SoundManager() = default;

  void loadSounds();

  void playEffect(Effect effect);

  void playBackgroundMusic(const std::string& filename, bool loop = true);
  void stopBackgroundMusic();
  void setMusicVolume(float volume);
  void setEffectsVolume(float volume);
  void stopAllEffects();

 private:
  void loadEffect(const std::string& name, const std::string& filepath);

  std::unordered_map<std::string, sf::SoundBuffer> m_buffers;
  std::unordered_map<std::string, sf::Sound> m_sounds;

  sf::Music m_music;
  float m_effects_volume = 100.f;
};

}  // namespace lilia::view::sound
