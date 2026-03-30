#pragma once

#include "lilia/chess/chess_types.hpp"
#include "lilia/app/view/ui/views/board_view.hpp"
#include "lilia/app/view/ui/render/scene/piece_manager.hpp"
#include "animation_manager.hpp"
#include <functional>

namespace lilia::app::view::ui
{
  class PromotionManager;
}

namespace lilia::app::view::animation
{

  class ChessAnimator
  {
  public:
    ChessAnimator(const ui::BoardView &boardRef, ui::PieceManager &pieceMgrRef);

    void warningAnim(chess::Square sq);
    void snapAndReturn(chess::Square pieceSq, MousePos mousePos);
    void movePiece(chess::Square from, chess::Square to, chess::PieceType promotion,
                   std::function<void()> onComplete = {});
    void dropPiece(chess::Square from, chess::Square to, chess::PieceType promotion);
    void piecePlaceHolder(chess::Square sq);
    void promotionSelect(chess::Square prPos, ui::PromotionManager &prOptRef, chess::Color c);

    void declareHighlightLevel(chess::Square sq);
    void end(chess::Square sq);
    void cancelAll();

    [[nodiscard]] bool isAnimating(ui::Entity::ID_type entityID) const;
    void updateAnimations(float dt);
    void render(sf::RenderWindow &window);
    void renderHighlightLevel(sf::RenderWindow &window);

  private:
    const ui::BoardView &m_board_view_ref;
    ui::PieceManager &m_piece_manager_ref;
    AnimationManager m_anim_manager;
  };

} // namespace lilia::view::animation
