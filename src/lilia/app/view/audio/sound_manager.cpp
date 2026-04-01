#include "lilia/app/view/audio/sound_manager.hpp"

#include "lilia/app/view/ui/render/render_constants.hpp"

namespace lilia::app::view::audio
{

  void SoundManager::loadSounds()
  {
    loadEffect(std::string{ui::constant::sfx::CAPTURE}, std::string{ui::constant::path::SFX_DIR});
    loadEffect(std::string{ui::constant::sfx::CASTLE}, std::string{ui::constant::path::SFX_DIR});
    loadEffect(std::string{ui::constant::sfx::CHECK}, std::string{ui::constant::path::SFX_DIR});
    loadEffect(std::string{ui::constant::sfx::ENEMY_MOVE}, std::string{ui::constant::path::SFX_DIR});
    loadEffect(std::string{ui::constant::sfx::GAME_BEGINS}, std::string{ui::constant::path::SFX_DIR});
    loadEffect(std::string{ui::constant::sfx::GAME_ENDS}, std::string{ui::constant::path::SFX_DIR});
    loadEffect(std::string{ui::constant::sfx::PLAYER_MOVE}, std::string{ui::constant::path::SFX_DIR});
    loadEffect(std::string{ui::constant::sfx::PROMOTION}, std::string{ui::constant::path::SFX_DIR});
    loadEffect(std::string{ui::constant::sfx::WARNING}, std::string{ui::constant::path::SFX_DIR});
    loadEffect(std::string{ui::constant::sfx::PREMOVE}, std::string{ui::constant::path::SFX_DIR});
  }

  void SoundManager::playEffect(Effect effect)
  {
    switch (effect)
    {
    case Effect::PlayerMove:
      m_sounds[std::string{ui::constant::sfx::PLAYER_MOVE}].play();

      break;
    case Effect::EnemyMove:
      m_sounds[std::string{ui::constant::sfx::ENEMY_MOVE}].play();

      break;
    case Effect::Capture:
      m_sounds[std::string{ui::constant::sfx::CAPTURE}].play();
      break;
    case Effect::Check:
      m_sounds[std::string{ui::constant::sfx::CHECK}].play();
      break;
    case Effect::Warning:
      m_sounds[std::string{ui::constant::sfx::WARNING}].play();
      break;
    case Effect::Castle:
      m_sounds[std::string{ui::constant::sfx::CASTLE}].play();
      break;
    case Effect::Promotion:
      m_sounds[std::string{ui::constant::sfx::PROMOTION}].play();
      break;
    case Effect::GameBegins:
      m_sounds[std::string{ui::constant::sfx::GAME_BEGINS}].play();
      break;
    case Effect::GameEnds:
      m_sounds[std::string{ui::constant::sfx::GAME_ENDS}].play();
      break;
    case Effect::Premove:
      m_sounds[std::string{ui::constant::sfx::PREMOVE}].play();
      break;
    }
  }

  void SoundManager::playBackgroundMusic(const std::string &filename, bool loop)
  {
    if (!m_music.openFromFile(filename))
    {
      throw std::runtime_error("Failed to open music file: " + filename);
    }
    m_music.setLoop(loop);
    m_music.play();
  }

  void SoundManager::stopBackgroundMusic()
  {
    m_music.stop();
  }

  void SoundManager::setMusicVolume(float volume)
  {
    m_music.setVolume(volume);
  }
  void SoundManager::setEffectsVolume(float volume)
  {
    m_effects_volume = volume;
    for (auto &[_, sound] : m_sounds)
    {
      sound.setVolume(m_effects_volume);
    }
  }

  void SoundManager::loadEffect(const std::string &name, const std::string &filepath)
  {
    sf::SoundBuffer buffer;
    if (!buffer.loadFromFile(filepath + "/" + name + ".wav"))
    {
      throw std::runtime_error("Failed to load sound effect: " + filepath + "/" + name + ".wav");
    }

    auto [it, inserted] = m_buffers.emplace(name, std::move(buffer));

    sf::Sound sound;
    sound.setBuffer(it->second);
    sound.setVolume(m_effects_volume);
    m_sounds[name] = std::move(sound);
  }

}
