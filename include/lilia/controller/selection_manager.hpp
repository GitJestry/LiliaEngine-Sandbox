#pragma once

#include "../chess_types.hpp"
#include "lilia/view/ui/screens/game_view.hpp"

namespace lilia::controller
{

  class SelectionManager
  {
  public:
    explicit SelectionManager(view::GameView &view);

    void reset();

    void highlightLastMove() const;
    void selectSquare(core::Square sq);
    void deselectSquare();
    void clearLastMoveHighlight() const;
    void hoverSquare(core::Square sq);
    void dehoverSquare();

    void setLastMove(core::Square from, core::Square to);
    [[nodiscard]] std::pair<core::Square, core::Square> getLastMove() const;

    [[nodiscard]] core::Square getSelectedSquare() const;
    [[nodiscard]] core::Square getHoveredSquare() const;

  private:
    view::GameView &m_view;
    core::Square m_selected_sq;
    core::Square m_hover_sq;
    std::pair<core::Square, core::Square> m_last_move_squares;
  };

} // namespace lilia::controller
