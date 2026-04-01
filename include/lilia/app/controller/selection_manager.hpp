#pragma once

#include "lilia/chess/chess_types.hpp"
#include "lilia/app/view/ui/screens/game_view.hpp"

namespace lilia::app::controller
{

  class SelectionManager
  {
  public:
    explicit SelectionManager(view::ui::GameView &view);

    void reset();

    void highlightLastMove() const;
    void selectSquare(chess::Square sq);
    void deselectSquare();
    void clearLastMoveHighlight() const;
    void hoverSquare(chess::Square sq);
    void dehoverSquare();

    void setLastMove(chess::Square from, chess::Square to);
    [[nodiscard]] std::pair<chess::Square, chess::Square> getLastMove() const;

    [[nodiscard]] chess::Square getSelectedSquare() const;
    [[nodiscard]] chess::Square getHoveredSquare() const;

  private:
    view::ui::GameView &m_view;
    chess::Square m_selected_sq;
    chess::Square m_hover_sq;
    std::pair<chess::Square, chess::Square> m_last_move_squares;
  };

}
