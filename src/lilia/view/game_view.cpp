#include "lilia/view/ui/screens/game_view.hpp"

#include <SFML/Window/Mouse.hpp>
#include <algorithm>

#include "lilia/bot/bot_info.hpp"
#include "lilia/view/ui/render/render_constants.hpp"
#include "lilia/view/ui/style/modals/modal.hpp"
#include "lilia/view/ui/render/texture_table.hpp"
#include "lilia/view/ui/style/modals/modal.hpp"
#include "lilia/view/ui/style/style.hpp"
namespace lilia::view
{

  namespace
  {
    sf::Vector2f mouseCoordsForEvent(const sf::Event &e, sf::RenderWindow &win)
    {
      switch (e.type)
      {
      case sf::Event::MouseMoved:
        return win.mapPixelToCoords({e.mouseMove.x, e.mouseMove.y});
      case sf::Event::MouseButtonPressed:
      case sf::Event::MouseButtonReleased:
        return win.mapPixelToCoords({e.mouseButton.x, e.mouseButton.y});
      case sf::Event::MouseWheelScrolled:
        return win.mapPixelToCoords({e.mouseWheelScroll.x, e.mouseWheelScroll.y});
      default:
        return win.mapPixelToCoords(sf::Mouse::getPosition(win));
      }
    }
  }

  GameView::GameView(sf::RenderWindow &window, bool topIsBot, bool bottomIsBot)
      : m_window(window), m_board_view(), m_piece_manager(m_board_view),
        m_highlight_manager(m_board_view),
        m_chess_animator(m_board_view, m_piece_manager), m_promotion_manager(),
        m_cursor_manager(window), m_eval_bar(), m_move_list(), m_top_player(),
        m_bottom_player(), m_top_clock(), m_bottom_clock(), m_modal(),
        m_particles()
  {
    // players
    PlayerInfo topInfo =
        topIsBot ? getBotConfig(BotType::Lilia).info
                 : PlayerInfo{"Challenger", "",
                              std::string{constant::path::ICON_CHALLENGER}};
    PlayerInfo bottomInfo =
        bottomIsBot ? getBotConfig(BotType::Lilia).info
                    : PlayerInfo{"Challenger", "",
                                 std::string{constant::path::ICON_CHALLENGER}};

    bool flipped = bottomIsBot && !topIsBot;
    if (flipped)
    {
      m_top_player.setInfo(bottomInfo);
      m_bottom_player.setInfo(topInfo);
      m_top_player.setPlayerColor(core::Color::White);
      m_bottom_player.setPlayerColor(core::Color::Black);
      m_white_player = &m_top_player;
      m_black_player = &m_bottom_player;
      m_top_clock.setPlayerColor(core::Color::White);
      m_bottom_clock.setPlayerColor(core::Color::Black);
      m_white_clock = &m_top_clock;
      m_black_clock = &m_bottom_clock;
    }
    else
    {
      m_top_player.setInfo(topInfo);
      m_bottom_player.setInfo(bottomInfo);
      m_top_player.setPlayerColor(core::Color::Black);
      m_bottom_player.setPlayerColor(core::Color::White);
      m_black_player = &m_top_player;
      m_white_player = &m_bottom_player;
      m_top_clock.setPlayerColor(core::Color::Black);
      m_bottom_clock.setPlayerColor(core::Color::White);
      m_black_clock = &m_top_clock;
      m_white_clock = &m_bottom_clock;
    }

    // board orientation
    m_board_view.setFlipped(flipped);
    m_eval_bar.setFlipped(flipped);

    // initial layout
    layout(m_window.getSize().x, m_window.getSize().y);

    // theme font for modals (same face as the rest of UI)
    m_modal.loadFont(std::string{constant::path::FONT});
  }

  void GameView::init(const std::string &fen)
  {
    m_board_view.init();
    m_piece_manager.initFromFen(fen);
    m_move_list.clear();
    m_eval_bar.reset();
    m_move_list.setFen(fen);
  }

  void GameView::update(float dt)
  {
    // Keep modal hover/press state responsive.
    const sf::Vector2f mouse = m_window.mapPixelToCoords(sf::Mouse::getPosition(m_window));
    m_modal.update(dt, mouse);

    m_chess_animator.updateAnimations(dt);
    m_particles.update(dt);
  }

  void GameView::updateEval(int eval) { m_eval_bar.update(eval); }

  void GameView::render()
  {
    // background
    const ui::Theme &theme = m_theme.uiTheme();
    ui::drawVerticalGradient(m_window, m_window.getSize(), theme.bgTop, theme.bgBottom);

    // left stack
    m_eval_bar.render(m_window);

    // board + pieces + overlays
    m_board_view.renderBoard(m_window);
    m_top_player.render(m_window);
    m_bottom_player.render(m_window);
    m_highlight_manager.renderSelect(m_window);
    m_highlight_manager.renderPremove(m_window);
    m_chess_animator.renderHighlightLevel(m_window);
    m_highlight_manager.renderHover(m_window);
    m_highlight_manager.renderRightClickSquares(m_window);

    // REAL pieces below animations
    m_piece_manager.renderPieces(m_window, m_chess_animator);
    m_highlight_manager.renderAttack(m_window);
    m_highlight_manager.renderRightClickArrows(m_window);

    // Animations and ghosts: ensure promotion overlay stays on top
    const bool inPromotion = isInPromotionSelection();
    const bool draggingPiece = m_dragging_piece != core::NO_SQUARE;
    if (inPromotion)
    {
      m_piece_manager.renderPremoveGhosts(m_window, m_chess_animator);
      if (draggingPiece)
        m_piece_manager.renderPiece(m_dragging_piece, m_window);
      m_chess_animator.render(m_window);
    }
    else
    {
      m_chess_animator.render(m_window);
      m_piece_manager.renderPremoveGhosts(m_window, m_chess_animator);
      if (draggingPiece)
        m_piece_manager.renderPiece(m_dragging_piece, m_window);
    }
    if (m_show_clocks)
    {
      m_top_clock.render(m_window);
      m_bottom_clock.render(m_window);
    }
    m_move_list.render(m_window);

    if (isAnyModalOpen())
    {
      m_modal.drawOverlay(m_window);
      if (m_modal.isGameOverOpen())
        m_particles.render(m_window);
      m_modal.drawPanel(m_window);
    }
  }

  void GameView::applyPremoveInstant(core::Square from, core::Square to,
                                     core::PieceType promotion)
  {
    m_piece_manager.applyPremoveInstant(from, to, promotion);
  }

  void GameView::addMove(const std::string &move) { m_move_list.addMove(move); }

  void GameView::addResult(const std::string &result)
  {
    m_move_list.addResult(result);
    m_eval_bar.setResult(result);
  }

  void GameView::selectMove(std::size_t moveIndex)
  {
    m_move_list.setCurrentMove(moveIndex);
  }

  void GameView::setBoardFen(const std::string &fen)
  {
    // Clear any lingering ghosts/hidden squares before rebuilding
    m_piece_manager.clearPremovePieces(true);
    m_chess_animator.cancelAll();
    m_piece_manager.removeAll();
    m_piece_manager.initFromFen(fen);
    m_highlight_manager.clearAllHighlights();
    m_move_list.setFen(fen);
  }

  void GameView::updateFen(const std::string &fen) { m_move_list.setFen(fen); }

  void GameView::resetBoard()
  {
    // Hard reset: restore any stashed pieces, cancel anims, then wipe
    m_piece_manager.clearPremovePieces(true);
    m_chess_animator.cancelAll();
    m_piece_manager.removeAll();
    init();
  }

  bool GameView::isInPromotionSelection()
  {
    return m_promotion_manager.hasOptions();
  }

  core::PieceType GameView::getSelectedPromotion(core::MousePos mousePos)
  {
    return m_promotion_manager.clickedOnType(
        static_cast<Entity::Position>(mousePos));
  }

  void GameView::removePromotionSelection()
  {
    m_promotion_manager.removeOptions();
  }

  void GameView::scrollMoveList(float delta) { m_move_list.scroll(delta); }

  void GameView::setBotMode(bool anyBot) { m_move_list.setBotMode(anyBot); }

  std::size_t GameView::getMoveIndexAt(core::MousePos mousePos) const
  {
    return m_move_list.getMoveIndexAt(Entity::Position{
        static_cast<float>(mousePos.x), static_cast<float>(mousePos.y)});
  }

  MoveListView::Option GameView::getOptionAt(core::MousePos mousePos) const
  {
    return m_move_list.getOptionAt(Entity::Position{
        static_cast<float>(mousePos.x), static_cast<float>(mousePos.y)});
  }

  void GameView::setGameOver(bool over) { m_move_list.setGameOver(over); }

  /* ---------- Modals ---------- */
  void GameView::showResignPopup()
  {
    auto center = m_board_view.getPosition();
    m_modal.showResign(m_window.getSize(), {center.x, center.y});
  }

  void GameView::hideResignPopup() { m_modal.hideResign(); }

  bool GameView::isResignPopupOpen() const { return m_modal.isResignOpen(); }

  bool GameView::handleModalEvent(const sf::Event &e)
  {
    // Let the modal system own hit-testing and button state.
    const sf::Vector2f mouse = mouseCoordsForEvent(e, m_window);
    return m_modal.handleEvent(e, mouse);
  }

  ui::ModalAction GameView::consumeModalAction() { return m_modal.consumeAction(); }

  bool GameView::isAnyModalOpen() const
  {
    return m_modal.isResignOpen() || m_modal.isGameOverOpen();
  }

  void GameView::showGameOverPopup(const std::string &msg, bool humanWinner)
  {
    auto center = m_board_view.getPosition();
    bool won = humanWinner && (msg.find("won") != std::string::npos ||
                               msg.find("win") != std::string::npos);
    m_modal.showGameOver(m_window.getSize(), msg, won, {center.x, center.y});
    if (won)
    {
      const auto ws = m_window.getSize();
      sf::Vector2f windowCenter{static_cast<float>(ws.x) * 0.5f,
                                static_cast<float>(ws.y) * 0.5f};
      sf::Vector2f windowSize{static_cast<float>(ws.x), static_cast<float>(ws.y)};
      m_particles.emitConfetti(windowCenter, windowSize, 200);
    }
  }

  void GameView::hideGameOverPopup()
  {
    m_modal.hideGameOver();
    m_particles.clear();
  }

  bool GameView::isGameOverPopupOpen() const { return m_modal.isGameOverOpen(); }

  /* ---------- Input helpers ---------- */
  core::Square GameView::mousePosToSquare(core::MousePos mousePos) const
  {
    return m_board_view.mousePosToSquare(mousePos);
  }

  core::MousePos GameView::clampPosToBoard(core::MousePos mousePos,
                                           Entity::Position pieceSize) const
  {
    return m_board_view.clampPosToBoard(mousePos, pieceSize);
  }

  void GameView::setPieceToMouseScreenPos(core::Square pos,
                                          core::MousePos mousePos)
  {
    auto size = getPieceSize(pos);
    m_piece_manager.setPieceToScreenPos(pos, clampPosToBoard(mousePos, size));
    m_dragging_piece = pos;
  }

  void GameView::setPieceToSquareScreenPos(core::Square from, core::Square to)
  {
    m_piece_manager.setPieceToSquareScreenPos(from, to);
  }

  void GameView::clearDraggingPiece() { m_dragging_piece = core::NO_SQUARE; }

  void GameView::movePiece(core::Square from, core::Square to,
                           core::PieceType promotion)
  {
    // IMPORTANT: reveal the real piece by consuming the premove ghost first
    m_piece_manager.consumePremoveGhost(from, to);
    m_piece_manager.movePiece(from, to, promotion);
  }

  /* ---------- Cursors ---------- */
  void GameView::setDefaultCursor() { m_cursor_manager.setDefaultCursor(); }
  void GameView::setHandOpenCursor() { m_cursor_manager.setHandOpenCursor(); }
  void GameView::setHandClosedCursor() { m_cursor_manager.setHandClosedCursor(); }

  /* ---------- Board info ---------- */
  sf::Vector2u GameView::getWindowSize() const { return m_window.getSize(); }

  core::MousePos GameView::getMousePosition() const
  {
    sf::Vector2i mp = sf::Mouse::getPosition(m_window);
    return core::MousePos(static_cast<unsigned>(mp.x),
                          static_cast<unsigned>(mp.y));
  }

  Entity::Position GameView::getPieceSize(core::Square pos) const
  {
    return m_piece_manager.getPieceSize(pos);
  }

  void GameView::toggleBoardOrientation()
  {
    m_board_view.toggleFlipped();
    m_eval_bar.setFlipped(m_board_view.isFlipped());
    std::swap(m_top_player, m_bottom_player);
    std::swap(m_white_player, m_black_player);
    std::swap(m_top_clock, m_bottom_clock);
    std::swap(m_white_clock, m_black_clock);
    layout(m_window.getSize().x, m_window.getSize().y);
  }

  bool GameView::isOnFlipIcon(core::MousePos mousePos) const
  {
    return m_board_view.isOnFlipIcon(mousePos);
  }

  void GameView::toggleEvalBarVisibility() { m_eval_bar.toggleVisibility(); }

  bool GameView::isOnEvalToggle(core::MousePos mousePos) const
  {
    return m_eval_bar.isOnToggle(mousePos);
  }

  void GameView::resetEvalBar() { m_eval_bar.reset(); }

  void GameView::setEvalResult(const std::string &result)
  {
    m_eval_bar.setResult(result);
  }

  void GameView::updateClock(core::Color color, float seconds)
  {
    Clock &clk = (color == core::Color::White) ? *m_white_clock : *m_black_clock;
    clk.setTime(seconds);
  }

  void GameView::setClockActive(std::optional<core::Color> active)
  {
    if (m_white_clock)
      m_white_clock->setActive(active && *active == core::Color::White);
    if (m_black_clock)
      m_black_clock->setActive(active && *active == core::Color::Black);
  }

  void GameView::setClocksVisible(bool visible) { m_show_clocks = visible; }

  /* ---------- Pieces / Highlights ---------- */
  bool GameView::hasPieceOnSquare(core::Square pos) const
  {
    return m_piece_manager.hasPieceOnSquare(pos);
  }

  bool GameView::isSameColorPiece(core::Square sq1, core::Square sq2) const
  {
    return m_piece_manager.isSameColor(sq1, sq2);
  }

  core::PieceType GameView::getPieceType(core::Square pos) const
  {
    return m_piece_manager.getPieceType(pos);
  }

  core::Color GameView::getPieceColor(core::Square pos) const
  {
    return m_piece_manager.getPieceColor(pos);
  }

  void GameView::addPiece(core::PieceType type, core::Color color,
                          core::Square pos)
  {
    m_piece_manager.addPiece(type, color, pos);
  }

  void GameView::removePiece(core::Square pos)
  {
    m_piece_manager.removePiece(pos);
  }

  void GameView::addCapturedPiece(core::Color capturer, core::PieceType type)
  {
    PlayerInfoView &view =
        (capturer == core::Color::White) ? *m_white_player : *m_black_player;
    view.addCapturedPiece(type, ~capturer);
  }

  void GameView::removeCapturedPiece(core::Color capturer)
  {
    PlayerInfoView &view =
        (capturer == core::Color::White) ? *m_white_player : *m_black_player;
    view.removeCapturedPiece();
  }

  void GameView::clearCapturedPieces()
  {
    m_top_player.clearCapturedPieces();
    m_bottom_player.clearCapturedPieces();
  }

  void GameView::highlightSquare(core::Square pos)
  {
    m_highlight_manager.highlightSquare(pos);
  }
  void GameView::highlightHoverSquare(core::Square pos)
  {
    m_highlight_manager.highlightHoverSquare(pos);
  }
  void GameView::highlightAttackSquare(core::Square pos)
  {
    m_highlight_manager.highlightAttackSquare(pos);
  }
  void GameView::highlightCaptureSquare(core::Square pos)
  {
    m_highlight_manager.highlightCaptureSquare(pos);
  }
  void GameView::highlightPremoveSquare(core::Square pos)
  {
    m_highlight_manager.highlightPremoveSquare(pos);
  }
  void GameView::highlightRightClickSquare(core::Square pos)
  {
    m_highlight_manager.highlightRightClickSquare(pos);
  }
  void GameView::highlightRightClickArrow(core::Square from, core::Square to)
  {
    m_highlight_manager.highlightRightClickArrow(from, to);
  }
  void GameView::stashRightClickHighlights()
  {
    m_saved_rclick_squares = m_highlight_manager.getRightClickSquares();
    m_saved_rclick_arrows = m_highlight_manager.getRightClickArrows();
  }
  void GameView::restoreRightClickHighlights()
  {
    for (auto sq : m_saved_rclick_squares)
      m_highlight_manager.highlightRightClickSquare(sq);
    for (const auto &ar : m_saved_rclick_arrows)
      m_highlight_manager.highlightRightClickArrow(ar.first, ar.second);
  }

  void GameView::clearHighlightSquare(core::Square pos)
  {
    m_highlight_manager.clearHighlightSquare(pos);
  }
  void GameView::clearHighlightHoverSquare(core::Square pos)
  {
    m_highlight_manager.clearHighlightHoverSquare(pos);
  }
  void GameView::clearHighlightPremoveSquare(core::Square pos)
  {
    m_highlight_manager.clearHighlightPremoveSquare(pos);
  }
  void GameView::clearPremoveHighlights()
  {
    m_highlight_manager.clearPremoveHighlights();
  }
  void GameView::clearAllHighlights()
  {
    m_highlight_manager.clearAllHighlights();
  }
  void GameView::clearNonPremoveHighlights()
  {
    m_highlight_manager.clearNonPremoveHighlights();
  }
  void GameView::clearAttackHighlights()
  {
    m_highlight_manager.clearAttackHighlights();
  }
  void GameView::clearRightClickHighlights()
  {
    m_highlight_manager.clearRightClickHighlights();
  }

  void GameView::showPremovePiece(core::Square from, core::Square to,
                                  core::PieceType promotion)
  {
    m_piece_manager.setPremovePiece(from, to, promotion);
  }

  void GameView::clearPremovePieces(bool restore)
  {
    m_piece_manager.clearPremovePieces(restore);
  }

  void GameView::consumePremoveGhost(core::Square from, core::Square to)
  {
    m_piece_manager.consumePremoveGhost(from, to);
  }

  /* ---------- Animations ---------- */
  void GameView::warningKingSquareAnim(core::Square ksq)
  {
    m_chess_animator.warningAnim(ksq);
    m_chess_animator.declareHighlightLevel(ksq);
  }

  void GameView::animationSnapAndReturn(core::Square sq,
                                        core::MousePos mousePos)
  {
    m_chess_animator.snapAndReturn(sq, mousePos);
  }

  void GameView::animationMovePiece(core::Square from, core::Square to,
                                    core::Square enPSquare,
                                    core::PieceType promotion,
                                    std::function<void()> onComplete)
  {
    // IMPORTANT: remove the ghost FIRST so the animation reveals the real piece.
    m_piece_manager.consumePremoveGhost(from, to);
    m_chess_animator.movePiece(from, to, promotion, std::move(onComplete));
    if (enPSquare != core::NO_SQUARE)
      m_piece_manager.removePiece(enPSquare);
  }

  void GameView::animationDropPiece(core::Square from, core::Square to,
                                    core::Square enPSquare,
                                    core::PieceType promotion)
  {
    // IMPORTANT: remove the ghost FIRST so the drop reveals the real piece.
    m_piece_manager.consumePremoveGhost(from, to);
    m_chess_animator.dropPiece(from, to, promotion);
    if (enPSquare != core::NO_SQUARE)
      m_piece_manager.removePiece(enPSquare);
  }

  void GameView::playPromotionSelectAnim(core::Square promSq, core::Color c)
  {
    m_chess_animator.promotionSelect(promSq, m_promotion_manager, c);
  }

  void GameView::playPiecePlaceHolderAnimation(core::Square sq)
  {
    m_chess_animator.piecePlaceHolder(sq);
  }

  void GameView::endAnimation(core::Square sq) { m_chess_animator.end(sq); }

  /* ---------- Layout ---------- */
  void GameView::layout(unsigned int width, unsigned int height)
  {
    float vMargin = std::max(0.f, (static_cast<float>(height) -
                                   static_cast<float>(constant::WINDOW_PX_SIZE)) /
                                      2.f);
    float hMargin =
        std::max(0.f, (static_cast<float>(width) -
                       static_cast<float>(constant::WINDOW_TOTAL_WIDTH)) /
                          2.f);

    float boardCenterX =
        hMargin +
        static_cast<float>(constant::EVAL_BAR_WIDTH + constant::SIDE_MARGIN) +
        static_cast<float>(constant::WINDOW_PX_SIZE) / 2.f;
    float boardCenterY =
        vMargin + static_cast<float>(constant::WINDOW_PX_SIZE) / 2.f;

    m_board_view.setPosition({boardCenterX, boardCenterY});

    float evalCenterX = hMargin + static_cast<float>(constant::EVAL_BAR_WIDTH +
                                                     constant::SIDE_MARGIN) /
                                      2.f;
    m_eval_bar.setPosition({evalCenterX, boardCenterY});

    float moveListX =
        hMargin +
        static_cast<float>(constant::EVAL_BAR_WIDTH + constant::SIDE_MARGIN +
                           constant::WINDOW_PX_SIZE + constant::SIDE_MARGIN);
    m_move_list.setPosition({moveListX, vMargin});
    m_move_list.setSize(constant::MOVE_LIST_WIDTH, constant::WINDOW_PX_SIZE);

    float boardLeft =
        boardCenterX - static_cast<float>(constant::WINDOW_PX_SIZE) / 2.f;
    float boardTop =
        boardCenterY - static_cast<float>(constant::WINDOW_PX_SIZE) / 2.f;

    // player badges
    m_top_player.setPositionClamped({boardLeft + 5.f, boardTop - 45.f},
                                    m_window.getSize());
    m_bottom_player.setPositionClamped(
        {boardLeft + 5.f,
         boardTop + static_cast<float>(constant::WINDOW_PX_SIZE) + 15.f},
        m_window.getSize());
    m_top_player.setBoardCenter(boardCenterX);
    m_bottom_player.setBoardCenter(boardCenterX);

    float clockX = boardLeft + static_cast<float>(constant::WINDOW_PX_SIZE) -
                   Clock::WIDTH * 0.85f;
    m_top_clock.setPosition({clockX, boardTop - Clock::HEIGHT});
    m_bottom_clock.setPosition(
        {clockX, boardTop + static_cast<float>(constant::WINDOW_PX_SIZE) + 5.f});

    // keep modal centered on window/board changes
    m_modal.onResize(m_window.getSize(), m_board_view.getPosition());
  }

} // namespace lilia::view
