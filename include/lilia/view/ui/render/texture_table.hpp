#pragma once

#include <SFML/Graphics/Texture.hpp>
#include <SFML/System/Vector2.hpp>

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>

namespace sf
{
  class Color;
}

namespace lilia::view
{

  class TextureTable
  {
  public:
    static TextureTable &getInstance();

    [[nodiscard]] const sf::Texture &get(const std::string &name);

    // Optionaler Bootstrap; wird auch automatisch im ctor gemacht (siehe cpp).
    void preLoad();

  private:
    void reloadForPalette();
    void load(std::string_view name, const sf::Color &color, sf::Vector2u size = {1, 1});

    TextureTable();
    ~TextureTable();

    TextureTable(const TextureTable &) = delete;
    TextureTable &operator=(const TextureTable &) = delete;

    std::uint64_t m_paletteListenerId{0};
    std::unordered_map<std::string, sf::Texture> m_textures;
  };

} // namespace lilia::view
