#pragma once

namespace sf
{
  class RenderWindow;
  class Texture;
}

#include <SFML/Graphics/Color.hpp>
#include <SFML/Graphics/Texture.hpp>

#include <array>

#include "lilia/chess/chess_types.hpp"
#include "../entity.hpp"
#include "lilia/app/view/ui/render/render_constants.hpp"

namespace lilia::app::view::ui
{

  class BoardNode : public Entity
  {
  public:
    BoardNode(const sf::Texture &texture) = delete;
    BoardNode(const sf::Texture &texture, MousePos pos) = delete;
    BoardNode(MousePos pos);
    BoardNode() = default;

    void init(const sf::Texture &texture_white, const sf::Texture &texture_black,
              const sf::Texture &texture_board, sf::Color labelOutline);

    // Rebuild label textures if outline color changed (cheap guard inside).
    void setLabelOutline(sf::Color outline);

    [[nodiscard]] MousePos getPosOfSquare(chess::Square sq) const;

    void draw(sf::RenderWindow &window) override;
    void setPosition(const MousePos &pos) override;

    void setFlipped(bool flipped);
    [[nodiscard]] bool isFlipped() const;

  private:
    MousePos boardOffset() const;
    void positionLabels(MousePos offset);
    void rebuildLabelTextures(sf::Color outlineCol);

    std::array<Entity, constant::BOARD_SIZE * constant::BOARD_SIZE> m_squares;
    std::array<Entity, constant::BOARD_SIZE> m_file_labels;
    std::array<Entity, constant::BOARD_SIZE> m_rank_labels;
    std::array<sf::Texture, constant::BOARD_SIZE> m_file_textures;
    std::array<sf::Texture, constant::BOARD_SIZE> m_rank_textures;

    bool m_flipped{false};

    sf::Color m_labelOutline{};
    bool m_labelOutlineInit{false};
  };

}
