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

  class ResourceTable
  {
  public:
    static ResourceTable &getInstance();

    [[nodiscard]] const sf::Texture &getTexture(const std::string &name);
    [[nodiscard]] const sf::Texture &getAssetTexture(const std::string &name);
    [[nodiscard]] const sf::Image &getImage(const std::string &name);

    // Optionaler Bootstrap; wird auch automatisch im ctor gemacht (siehe cpp).
    void preLoad();

  private:
    void reloadForPalette();
    void load(std::string_view name, const sf::Color &color, sf::Vector2u size = {1, 1});

    ResourceTable();
    ~ResourceTable();

    ResourceTable(const ResourceTable &) = delete;
    ResourceTable &operator=(const ResourceTable &) = delete;

    std::uint64_t m_paletteListenerId{0};
    std::unordered_map<std::string, sf::Texture> m_textures;
    std::unordered_map<std::string, sf::Image> m_images;
  };

} // namespace lilia::view
