#pragma once

#include <SFML/Graphics/RenderWindow.hpp>
#include <SFML/Window/Event.hpp>
#include <SFML/System/Vector2.hpp>
#include <functional>
#include <optional>
#include <vector>

#include "lilia/chess/chess_types.hpp"
#include "lilia/app/view/mousepos.hpp"
#include "lilia/app/view/animation/chess_animator.hpp"
#include "lilia/app/view/ui/views/board_view.hpp"
#include "lilia/app/view/ui/views/clock.hpp"
#include "lilia/app/view/ui/interaction/cursor_manager.hpp"
#include "lilia/app/view/ui/render/entity.hpp"
#include "lilia/app/view/ui/views/eval_bar.hpp"
#include "lilia/app/view/ui/interaction/highlight_manager.hpp"
#include "lilia/app/view/ui/views/modal_view.hpp"
#include "lilia/app/view/ui/views/move_list_view.hpp"
#include "lilia/app/view/ui/render/particle_system.hpp"
#include "lilia/app/view/ui/render/scene/piece_manager.hpp"
#include "lilia/app/view/ui/views/player_info_view.hpp"
#include "lilia/app/view/ui/render/scene/promotion_manager.hpp"
#include "lilia/app/view/ui/style/theme_cache.hpp"
#include "lilia/chess/chess_constants.hpp"

namespace lilia::app::view::ui
{
  enum class ModalAction;

  class GameView
  {
  public:
    GameView(sf::RenderWindow &window, bool topIsBot, bool bottomIsBot);
    ~GameView() = default;

    void init(const std::string &fen = std::string{chess::constant::START_FEN});
    void resetBoard();

    void update(float dt);
    void updateEval(int eval);
    void render();

    void addMove(const std::string &move);
    void addResult(const std::string &result);
    void selectMove(std::size_t moveIndex);
    void setBoardFen(const std::string &fen);
    void updateFen(const std::string &fen);
    void scrollMoveList(float delta);
    void setBotMode(bool anyBot);

    [[nodiscard]] std::size_t getMoveIndexAt(MousePos mousePos) const;
    [[nodiscard]] MoveListView::Option getOptionAt(MousePos mousePos) const;
    void setGameOver(bool over);

    // Modals (ModalView + ModalStack)
    void showResignPopup();
    void hideResignPopup();
    [[nodiscard]] bool isResignPopupOpen() const;

    void showGameOverPopup(const std::string &msg, bool humanWinner);
    void hideGameOverPopup();
    [[nodiscard]] bool isGameOverPopupOpen() const;

    // Feed SFML events to the active modal (buttons, close, etc.).
    // Returns true if the event was consumed.
    bool handleModalEvent(const sf::Event &e);

    // Retrieve and clear the most recent modal action.
    [[nodiscard]] ModalAction consumeModalAction();

    [[nodiscard]] bool isAnyModalOpen() const;

    [[nodiscard]] chess::Square mousePosToSquare(MousePos mousePos) const;
    [[nodiscard]] MousePos clampPosToBoard(MousePos mousePos,
                                           MousePos pieceSize = {0.f, 0.f}) const;
    void setPieceToMouseScreenPos(chess::Square pos, MousePos mousePos);
    void setPieceToSquareScreenPos(chess::Square from, chess::Square to);
    void movePiece(chess::Square from, chess::Square to,
                   chess::PieceType promotion = chess::PieceType::None);

    // Track the piece currently being dragged so it can be rendered above others
    void clearDraggingPiece();

    [[nodiscard]] sf::Vector2u getWindowSize() const;
    [[nodiscard]] MousePos getMousePosition() const;
    [[nodiscard]] MousePos getPieceSize(chess::Square pos) const;

    [[nodiscard]] bool hasPieceOnSquare(chess::Square pos) const;
    [[nodiscard]] bool isSameColorPiece(chess::Square sq1, chess::Square sq2) const;
    [[nodiscard]] chess::PieceType getPieceType(chess::Square pos) const;
    [[nodiscard]] chess::Color getPieceColor(chess::Square pos) const;

    void addPiece(chess::PieceType type, chess::Color color, chess::Square pos);
    void removePiece(chess::Square pos);

    void addCapturedPiece(chess::Color capturer, chess::PieceType type);
    void removeCapturedPiece(chess::Color capturer);
    void clearCapturedPieces();
    void consumePremoveGhost(chess::Square from, chess::Square to);
    void applyPremoveInstant(chess::Square from, chess::Square to,
                             chess::PieceType promotion = chess::PieceType::None);

    void highlightSquare(chess::Square pos);
    void highlightAttackSquare(chess::Square pos);
    void highlightCaptureSquare(chess::Square pos);
    void highlightHoverSquare(chess::Square pos);
    void highlightPremoveSquare(chess::Square pos);
    void highlightRightClickSquare(chess::Square pos);
    void highlightRightClickArrow(chess::Square from, chess::Square to);
    void stashRightClickHighlights();
    void restoreRightClickHighlights();
    void clearHighlightSquare(chess::Square pos);
    void clearHighlightHoverSquare(chess::Square pos);
    void clearHighlightPremoveSquare(chess::Square pos);
    void clearPremoveHighlights();
    void clearAllHighlights();
    void clearNonPremoveHighlights();
    void clearAttackHighlights();
    void clearRightClickHighlights();

    // Preview helpers for premoves
    void showPremovePiece(chess::Square from, chess::Square to,
                          chess::PieceType promotion = chess::PieceType::None);
    void clearPremovePieces(bool restore = true);

    void warningKingSquareAnim(chess::Square ksq);
    void animationSnapAndReturn(chess::Square sq, MousePos mousePos);
    void animationMovePiece(chess::Square from, chess::Square to,
                            chess::Square enPSquare = chess::NO_SQUARE,
                            chess::PieceType promotion = chess::PieceType::None,
                            std::function<void()> onComplete = {});
    void animationDropPiece(chess::Square from, chess::Square to,
                            chess::Square enPSquare = chess::NO_SQUARE,
                            chess::PieceType promotion = chess::PieceType::None);
    void playPromotionSelectAnim(chess::Square promSq, chess::Color c);
    void playPiecePlaceHolderAnimation(chess::Square sq);
    void endAnimation(chess::Square sq);

    bool isInPromotionSelection();
    chess::PieceType getSelectedPromotion(MousePos mousePos);
    void removePromotionSelection();

    void setDefaultCursor();
    void setHandOpenCursor();
    void setHandClosedCursor();

    void toggleBoardOrientation();
    [[nodiscard]] bool isOnFlipIcon(MousePos mousePos) const;

    void toggleEvalBarVisibility();
    [[nodiscard]] bool isOnEvalToggle(MousePos mousePos) const;

    void resetEvalBar();
    void setEvalResult(const std::string &result);

    void updateClock(chess::Color color, float seconds);
    void setClockActive(std::optional<chess::Color> active);
    void setClocksVisible(bool visible);

    // Replay metadata (used by controller when starting replay mode)
    void setReplayHeader(std::optional<domain::ReplayInfo> header);
    void clearReplayHeader();
    void setOutcomeBadges(std::optional<domain::Outcome> white,
                          std::optional<domain::Outcome> black);
    void clearOutcomeBadges();

    // Generic: update player badges (useful for replay and future config refactor)
    void setPlayersInfo(const domain::PlayerInfo &white, const domain::PlayerInfo &black);

  private:
    void layout(unsigned int width, unsigned int height);

    sf::RenderWindow &m_window;

    BoardView m_board_view;
    PieceManager m_piece_manager;
    HighlightManager m_highlight_manager;
    lilia::app::view::animation::ChessAnimator m_chess_animator;
    PromotionManager m_promotion_manager;

    // Currently dragged piece (if any)
    chess::Square m_dragging_piece{chess::NO_SQUARE};

    CursorManager m_cursor_manager;

    // UI components
    EvalBar m_eval_bar;
    MoveListView m_move_list;
    PlayerInfoView m_white_player;
    PlayerInfoView m_black_player;
    Clock m_white_clock;
    Clock m_black_clock;
    bool m_show_clocks{true};
    ModalView m_modal;
    ThemeCache m_theme;
    std::optional<domain::ReplayInfo> m_replay_header;

    // FX
    ParticleSystem m_particles;

    std::vector<chess::Square> m_saved_rclick_squares;
    std::vector<std::pair<chess::Square, chess::Square>> m_saved_rclick_arrows;
  };

} // namespace lilia::view
