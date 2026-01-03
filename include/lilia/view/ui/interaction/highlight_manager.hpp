#pragma once
#include <unordered_map>
#include <utility>
#include <vector>

#include "lilia/chess_types.hpp"
#include "lilia/view/ui/views/board_view.hpp"
#include "lilia/view/ui/render/entity.hpp"

namespace sf
{
  class RenderWindow;
}

namespace lilia::view
{

  class HighlightManager
  {
  public:
    explicit HighlightManager(const BoardView &boardRef);
    ~HighlightManager() = default;

    void highlightSquare(core::Square pos);
    void highlightAttackSquare(core::Square pos);
    void highlightCaptureSquare(core::Square pos);
    void highlightHoverSquare(core::Square pos);
    void highlightPremoveSquare(core::Square pos);
    void highlightRightClickSquare(core::Square pos);
    void highlightRightClickArrow(core::Square from, core::Square to);

    [[nodiscard]] std::vector<core::Square> getRightClickSquares() const;
    [[nodiscard]] std::vector<std::pair<core::Square, core::Square>> getRightClickArrows() const;

    void clearAllHighlights();
    void clearNonPremoveHighlights();
    void clearAttackHighlights();
    void clearHighlightSquare(core::Square pos);
    void clearHighlightHoverSquare(core::Square pos);
    void clearHighlightPremoveSquare(core::Square pos);
    void clearPremoveHighlights();
    void clearRightClickHighlights();

    void renderAttack(sf::RenderWindow &window);
    void renderHover(sf::RenderWindow &window);
    void renderSelect(sf::RenderWindow &window);
    void renderPremove(sf::RenderWindow &window);
    void renderRightClickSquares(sf::RenderWindow &window);
    void renderRightClickArrows(sf::RenderWindow &window);

  private:
    struct AttackMark
    {
      Entity entity;
      bool capture{false};
    };

    void renderEntitiesToBoard(std::unordered_map<core::Square, Entity> &map, sf::RenderWindow &window);
    void renderAttackToBoard(sf::RenderWindow &window);

    const BoardView &m_board_view_ref;

    std::unordered_map<core::Square, AttackMark> m_attack;
    std::unordered_map<core::Square, Entity> m_select;
    std::unordered_map<core::Square, Entity> m_hover;
    std::unordered_map<core::Square, Entity> m_premove;
    std::unordered_map<core::Square, Entity> m_rclick_squares;
    std::unordered_map<unsigned int, std::pair<core::Square, core::Square>> m_rclick_arrows;
  };

} // namespace lilia::view
