#pragma once

#include "lilia/chess_types.hpp"
#include "lilia/view/ui/views/board_view.hpp"
#include "lilia/view/ui/render/scene/piece_manager.hpp"
#include "animation_manager.hpp"
#include <functional>

namespace lilia::view
{
  class PromotionManager;
}

namespace lilia::view::animation
{

  class ChessAnimator
  {
  public:
    ChessAnimator(const BoardView &boardRef, PieceManager &pieceMgrRef);

    void warningAnim(core::Square sq);
    void snapAndReturn(core::Square pieceSq, core::MousePos mousePos);
    void movePiece(core::Square from, core::Square to, core::PieceType promotion,
                   std::function<void()> onComplete = {});
    void dropPiece(core::Square from, core::Square to, core::PieceType promotion);
    void piecePlaceHolder(core::Square sq);
    void promotionSelect(core::Square prPos, PromotionManager &prOptRef, core::Color c);

    void declareHighlightLevel(core::Square sq);
    void end(core::Square sq);
    void cancelAll();

    [[nodiscard]] bool isAnimating(Entity::ID_type entityID) const;
    void updateAnimations(float dt);
    void render(sf::RenderWindow &window);
    void renderHighlightLevel(sf::RenderWindow &window);

  private:
    const BoardView &m_board_view_ref;
    PieceManager &m_piece_manager_ref;
    AnimationManager m_anim_manager;
  };

} // namespace lilia::view::animation
