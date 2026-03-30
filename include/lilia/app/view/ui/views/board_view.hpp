#pragma once

#include <string>

#include "lilia/app/view/mousepos.hpp"
#include "lilia/app/view/ui/render/scene/board_node.hpp"
#include "lilia/app/view/ui/render/entity.hpp"

namespace lilia::app::view::ui
{

  class BoardView
  {
  public:
    BoardView();
    ~BoardView() = default;

    void init();
    void renderBoard(sf::RenderWindow &window);

    [[nodiscard]] MousePos getSquareScreenPos(chess::Square sq) const;

    void toggleFlipped();
    void setFlipped(bool flipped);
    [[nodiscard]] bool isFlipped() const;

    [[nodiscard]] bool isOnFlipIcon(MousePos mousePos) const;

    MousePos clampPosToBoard(MousePos mousePos,
                             MousePos pieceSize = {0.f, 0.f}) const noexcept;
    [[nodiscard]] chess::Square mousePosToSquare(MousePos mousePos) const;

    void setPosition(const MousePos &pos);
    [[nodiscard]] MousePos getPosition() const;

  private:
    BoardNode m_board;
    MousePos m_flip_pos{};
    float m_flip_size{0.f};
    bool m_flipped{false};
  };

} // namespace lilia::view
