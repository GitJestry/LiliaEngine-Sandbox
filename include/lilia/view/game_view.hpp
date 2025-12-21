#pragma once

#include <SFML/Graphics/RenderWindow.hpp>
#include <SFML/System/Vector2.hpp>
#include <functional>
#include <optional>
#include <vector>

#include "../chess_types.hpp"
#include "../constants.hpp"
#include "../controller/mousepos.hpp"
#include "animation/chess_animator.hpp"
#include "board_view.hpp"
#include "clock.hpp"
#include "cursor_manager.hpp"
#include "entity.hpp"
#include "eval_bar.hpp"
#include "highlight_manager.hpp"
#include "modal_view.hpp"
#include "move_list_view.hpp"
#include "particle_system.hpp"
#include "piece_manager.hpp"
#include "player_info_view.hpp"
#include "promotion_manager.hpp"

namespace lilia::view {

class GameView {
 public:
  GameView(sf::RenderWindow& window, bool topIsBot, bool bottomIsBot);
  ~GameView() = default;

  void init(const std::string& fen = core::START_FEN);
  void resetBoard();

  void update(float dt);
  void updateEval(int eval);
  void render();

  void addMove(const std::string& move);
  void addResult(const std::string& result);
  void selectMove(std::size_t moveIndex);
  void setBoardFen(const std::string& fen);
  void updateFen(const std::string& fen);
  void scrollMoveList(float delta);
  void setBotMode(bool anyBot);

  [[nodiscard]] std::size_t getMoveIndexAt(core::MousePos mousePos) const;
  [[nodiscard]] MoveListView::Option getOptionAt(core::MousePos mousePos) const;
  void setGameOver(bool over);

  // Modals (API unchanged; now delegated to ModalView)
  void showResignPopup();
  void hideResignPopup();
  [[nodiscard]] bool isResignPopupOpen() const;
  [[nodiscard]] bool isOnResignYes(core::MousePos mousePos) const;
  [[nodiscard]] bool isOnResignNo(core::MousePos mousePos) const;

  void showGameOverPopup(const std::string& msg, bool humanWinner);
  void hideGameOverPopup();
  [[nodiscard]] bool isGameOverPopupOpen() const;
  [[nodiscard]] bool isOnNewBot(core::MousePos mousePos) const;
  [[nodiscard]] bool isOnRematch(core::MousePos mousePos) const;
  [[nodiscard]] bool isOnModalClose(core::MousePos mousePos) const;

  [[nodiscard]] core::Square mousePosToSquare(core::MousePos mousePos) const;
  [[nodiscard]] core::MousePos clampPosToBoard(core::MousePos mousePos,
                                               Entity::Position pieceSize = {0.f, 0.f}) const;
  void setPieceToMouseScreenPos(core::Square pos, core::MousePos mousePos);
  void setPieceToSquareScreenPos(core::Square from, core::Square to);
  void movePiece(core::Square from, core::Square to,
                 core::PieceType promotion = core::PieceType::None);

  // Track the piece currently being dragged so it can be rendered above others
  void clearDraggingPiece();

  [[nodiscard]] sf::Vector2u getWindowSize() const;
  [[nodiscard]] core::MousePos getMousePosition() const;
  [[nodiscard]] Entity::Position getPieceSize(core::Square pos) const;

  [[nodiscard]] bool hasPieceOnSquare(core::Square pos) const;
  [[nodiscard]] bool isSameColorPiece(core::Square sq1, core::Square sq2) const;
  [[nodiscard]] core::PieceType getPieceType(core::Square pos) const;
  [[nodiscard]] core::Color getPieceColor(core::Square pos) const;

  void addPiece(core::PieceType type, core::Color color, core::Square pos);
  void removePiece(core::Square pos);

  void addCapturedPiece(core::Color capturer, core::PieceType type);
  void removeCapturedPiece(core::Color capturer);
  void clearCapturedPieces();
  void consumePremoveGhost(core::Square from, core::Square to);
  void applyPremoveInstant(core::Square from, core::Square to,
                           core::PieceType promotion = core::PieceType::None);

  void highlightSquare(core::Square pos);
  void highlightAttackSquare(core::Square pos);
  void highlightCaptureSquare(core::Square pos);
  void highlightHoverSquare(core::Square pos);
  void highlightPremoveSquare(core::Square pos);
  void highlightRightClickSquare(core::Square pos);
  void highlightRightClickArrow(core::Square from, core::Square to);
  void stashRightClickHighlights();
  void restoreRightClickHighlights();
  void clearHighlightSquare(core::Square pos);
  void clearHighlightHoverSquare(core::Square pos);
  void clearHighlightPremoveSquare(core::Square pos);
  void clearPremoveHighlights();
  void clearAllHighlights();
  void clearNonPremoveHighlights();
  void clearAttackHighlights();
  void clearRightClickHighlights();

  // Preview helpers for premoves
  void showPremovePiece(core::Square from, core::Square to,
                        core::PieceType promotion = core::PieceType::None);
  void clearPremovePieces(bool restore = true);

  void warningKingSquareAnim(core::Square ksq);
  void animationSnapAndReturn(core::Square sq, core::MousePos mousePos);
  void animationMovePiece(core::Square from, core::Square to,
                          core::Square enPSquare = core::NO_SQUARE,
                          core::PieceType promotion = core::PieceType::None,
                          std::function<void()> onComplete = {});
  void animationDropPiece(core::Square from, core::Square to,
                          core::Square enPSquare = core::NO_SQUARE,
                          core::PieceType promotion = core::PieceType::None);
  void playPromotionSelectAnim(core::Square promSq, core::Color c);
  void playPiecePlaceHolderAnimation(core::Square sq);
  void endAnimation(core::Square sq);

  bool isInPromotionSelection();
  core::PieceType getSelectedPromotion(core::MousePos mousePos);
  void removePromotionSelection();

  void setDefaultCursor();
  void setHandOpenCursor();
  void setHandClosedCursor();

  void toggleBoardOrientation();
  [[nodiscard]] bool isOnFlipIcon(core::MousePos mousePos) const;

  void toggleEvalBarVisibility();
  [[nodiscard]] bool isOnEvalToggle(core::MousePos mousePos) const;

  void resetEvalBar();
  void setEvalResult(const std::string& result);

  void updateClock(core::Color color, float seconds);
  void setClockActive(std::optional<core::Color> active);
  void setClocksVisible(bool visible);

 private:
  void layout(unsigned int width, unsigned int height);

  sf::RenderWindow& m_window;

  BoardView m_board_view;
  PieceManager m_piece_manager;
  HighlightManager m_highlight_manager;
  animation::ChessAnimator m_chess_animator;
  PromotionManager m_promotion_manager;

  // Currently dragged piece (if any)
  core::Square m_dragging_piece{core::NO_SQUARE};

  CursorManager m_cursor_manager;

  // UI components
  EvalBar m_eval_bar;
  MoveListView m_move_list;
  PlayerInfoView m_top_player;
  PlayerInfoView m_bottom_player;
  PlayerInfoView* m_white_player{};
  PlayerInfoView* m_black_player{};
  Clock m_top_clock;
  Clock m_bottom_clock;
  Clock* m_white_clock{};
  Clock* m_black_clock{};
  bool m_show_clocks{true};
  ModalView m_modal;

  // FX
  ParticleSystem m_particles;

  std::vector<core::Square> m_saved_rclick_squares;
  std::vector<std::pair<core::Square, core::Square>> m_saved_rclick_arrows;
};

}  // namespace lilia::view
