#include "lilia/app/view/animation/chess_animator.hpp"

#include "lilia/app/view/animation/move_animation.hpp"
#include "lilia/app/view/animation/piece_placeholder_animation.hpp"
#include "lilia/app/view/animation/promotion_select_animation.hpp"
#include "lilia/app/view/animation/snap_to_square_animation.hpp"
#include "lilia/app/view/animation/warning_animation.hpp"
#include "lilia/app/view/ui/render/render_constants.hpp"

namespace lilia::app::view::animation
{

  ChessAnimator::ChessAnimator(const ui::BoardView &boardRef, ui::PieceManager &pieceMgrRef)
      : m_board_view_ref(boardRef), m_piece_manager_ref(pieceMgrRef) {}

  void ChessAnimator::warningAnim(chess::Square sq)
  {
    m_anim_manager.add(m_piece_manager_ref.getPieceID(sq),
                       std::make_unique<WarningAnim>(m_board_view_ref.getSquareScreenPos(sq)));
  }

  void ChessAnimator::declareHighlightLevel(chess::Square sq)
  {
    m_anim_manager.declareHighlightLevel(m_piece_manager_ref.getPieceID(sq));
  }

  void ChessAnimator::snapAndReturn(chess::Square pieceSq, MousePos mousePos)
  {
    m_anim_manager.add(
        m_piece_manager_ref.getPieceID(pieceSq),
        std::make_unique<SnapToSquareAnim>(m_piece_manager_ref, pieceSq, mousePos,
                                           m_board_view_ref.getSquareScreenPos(pieceSq)));
  }
  void ChessAnimator::movePiece(chess::Square from, chess::Square to, chess::PieceType promotion,
                                std::function<void()> onComplete)
  {
    m_anim_manager.add(
        m_piece_manager_ref.getPieceID(from),
        std::make_unique<MoveAnim>(m_piece_manager_ref, m_board_view_ref.getSquareScreenPos(from),
                                   m_board_view_ref.getSquareScreenPos(to), from, to, promotion,
                                   std::move(onComplete)));
  }
  void ChessAnimator::dropPiece(chess::Square from, chess::Square to, chess::PieceType promotion)
  {
    m_piece_manager_ref.movePiece(from, to, promotion);
  }

  void ChessAnimator::promotionSelect(chess::Square prPos, ui::PromotionManager &prOptRef, chess::Color c)
  {
    auto pos = m_board_view_ref.getSquareScreenPos(prPos);
    bool upwards = pos.y > m_board_view_ref.getPosition().y;
    m_anim_manager.add(m_piece_manager_ref.getPieceID(chess::NO_SQUARE),
                       std::make_unique<PromotionSelectAnim>(pos, prOptRef, c, upwards));
  }

  void ChessAnimator::piecePlaceHolder(chess::Square sq)
  {
    m_anim_manager.add(m_piece_manager_ref.getPieceID(sq),
                       std::make_unique<PiecePlaceholderAnim>(m_piece_manager_ref, sq));
  }
  void ChessAnimator::end(chess::Square sq)
  {
    m_anim_manager.endAnim(m_piece_manager_ref.getPieceID(sq));
  }

  void ChessAnimator::cancelAll()
  {
    m_anim_manager.cancelAll();
  }

  [[nodiscard]] bool ChessAnimator::isAnimating(ui::Entity::ID_type entityID) const
  {
    return m_anim_manager.isAnimating(entityID);
  }
  void ChessAnimator::updateAnimations(float dt)
  {
    m_anim_manager.update(dt);
  }
  void ChessAnimator::renderHighlightLevel(sf::RenderWindow &window)
  {
    m_anim_manager.highlightLevelDraw(window);
  }
  void ChessAnimator::render(sf::RenderWindow &window)
  {
    m_anim_manager.draw(window);
  }

}
