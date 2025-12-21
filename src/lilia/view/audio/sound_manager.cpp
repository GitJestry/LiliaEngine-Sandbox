#include "lilia/view/audio/sound_manager.hpp"

#include "lilia/view/render_constants.hpp"

namespace lilia::view::sound {

void SoundManager::loadSounds() {
  loadEffect(constant::SFX_CAPTURE_NAME, constant::ASSET_SFX_FILE_PATH);
  loadEffect(constant::SFX_CASTLE_NAME, constant::ASSET_SFX_FILE_PATH);
  loadEffect(constant::SFX_CHECK_NAME, constant::ASSET_SFX_FILE_PATH);
  loadEffect(constant::SFX_ENEMY_MOVE_NAME, constant::ASSET_SFX_FILE_PATH);
  loadEffect(constant::SFX_GAME_BEGINS_NAME, constant::ASSET_SFX_FILE_PATH);
  loadEffect(constant::SFX_GAME_ENDS_NAME, constant::ASSET_SFX_FILE_PATH);
  loadEffect(constant::SFX_PLAYER_MOVE_NAME, constant::ASSET_SFX_FILE_PATH);
  loadEffect(constant::SFX_PROMOTION_NAME, constant::ASSET_SFX_FILE_PATH);
  loadEffect(constant::SFX_WARNING_NAME, constant::ASSET_SFX_FILE_PATH);
  loadEffect(constant::SFX_PREMOVE_NAME, constant::ASSET_SFX_FILE_PATH);
}

void SoundManager::playEffect(Effect effect) {
  switch (effect) {
    case Effect::PlayerMove:
      m_sounds[constant::SFX_PLAYER_MOVE_NAME].play();

      break;
    case Effect::EnemyMove:
      m_sounds[constant::SFX_ENEMY_MOVE_NAME].play();

      break;
    case Effect::Capture:
      m_sounds[constant::SFX_CAPTURE_NAME].play();
      break;
    case Effect::Check:
      m_sounds[constant::SFX_CHECK_NAME].play();
      break;
    case Effect::Warning:
      m_sounds[constant::SFX_WARNING_NAME].play();
      break;
    case Effect::Castle:
      m_sounds[constant::SFX_CASTLE_NAME].play();
      break;
    case Effect::Promotion:
      m_sounds[constant::SFX_PROMOTION_NAME].play();
      break;
    case Effect::GameBegins:
      m_sounds[constant::SFX_GAME_BEGINS_NAME].play();
      break;
    case Effect::GameEnds:
      m_sounds[constant::SFX_GAME_ENDS_NAME].play();
      break;
    case Effect::Premove:
      m_sounds[constant::SFX_PREMOVE_NAME].play();
      break;
  }
}

void SoundManager::playBackgroundMusic(const std::string& filename, bool loop) {
  if (!m_music.openFromFile(filename)) {
    throw std::runtime_error("Failed to open music file: " + filename);
  }
  m_music.setLoop(loop);
  m_music.play();
}

void SoundManager::stopBackgroundMusic() {
  m_music.stop();
}

void SoundManager::setMusicVolume(float volume) {
  m_music.setVolume(volume);
}
void SoundManager::setEffectsVolume(float volume) {
  m_effects_volume = volume;
  for (auto& [_, sound] : m_sounds) {
    sound.setVolume(m_effects_volume);
  }
}

void SoundManager::stopAllEffects() {
  for (auto& [_, sound] : m_sounds) {
    sound.stop();
  }
}

void SoundManager::loadEffect(const std::string& name, const std::string& filepath) {
  sf::SoundBuffer buffer;
  if (!buffer.loadFromFile(filepath + "/" + name + ".wav")) {
    throw std::runtime_error("Failed to load sound effect: " + filepath + "/" + name + ".wav");
  }

  auto [it, inserted] = m_buffers.emplace(name, std::move(buffer));

  sf::Sound sound;
  sound.setBuffer(it->second);
  sound.setVolume(m_effects_volume);
  m_sounds[name] = std::move(sound);
}

}  // namespace lilia::view::sound
