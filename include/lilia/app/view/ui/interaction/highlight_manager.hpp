#pragma once
#include <unordered_map>
#include <utility>
#include <vector>

#include "lilia/chess/chess_types.hpp"
#include "lilia/app/view/ui/views/board_view.hpp"
#include "lilia/app/view/ui/render/entity.hpp"

namespace sf
{
  class RenderWindow;
}

namespace lilia::app::view::ui
{

  class HighlightManager
  {
  public:
    explicit HighlightManager(const BoardView &boardRef);
    ~HighlightManager() = default;

    void highlightSquare(chess::Square pos);
    void highlightAttackSquare(chess::Square pos);
    void highlightCaptureSquare(chess::Square pos);
    void highlightHoverSquare(chess::Square pos);
    void highlightPremoveSquare(chess::Square pos);
    void highlightRightClickSquare(chess::Square pos);
    void highlightRightClickArrow(chess::Square from, chess::Square to);

    [[nodiscard]] std::vector<chess::Square> getRightClickSquares() const;
    [[nodiscard]] std::vector<std::pair<chess::Square, chess::Square>> getRightClickArrows() const;

    void clearAllHighlights();
    void clearNonPremoveHighlights();
    void clearAttackHighlights();
    void clearHighlightSquare(chess::Square pos);
    void clearHighlightHoverSquare(chess::Square pos);
    void clearHighlightPremoveSquare(chess::Square pos);
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

    void renderEntitiesToBoard(std::unordered_map<chess::Square, Entity> &map, sf::RenderWindow &window);
    void renderAttackToBoard(sf::RenderWindow &window);

    const BoardView &m_board_view_ref;

    std::unordered_map<chess::Square, AttackMark> m_attack;
    std::unordered_map<chess::Square, Entity> m_select;
    std::unordered_map<chess::Square, Entity> m_hover;
    std::unordered_map<chess::Square, Entity> m_premove;
    std::unordered_map<chess::Square, Entity> m_rclick_squares;
    std::unordered_map<unsigned int, std::pair<chess::Square, chess::Square>> m_rclick_arrows;
  };

} // namespace lilia::view
