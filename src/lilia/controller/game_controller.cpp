#include "lilia/controller/game_controller.hpp"

#include <SFML/System/Sleep.hpp>
#include <SFML/System/Time.hpp>
#include <SFML/Window/Clipboard.hpp>
#include <SFML/Window/Event.hpp>
#include <SFML/Window/Keyboard.hpp>
#include <SFML/Window/Mouse.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <string>

#include "lilia/controller/bot_player.hpp"
#include "lilia/controller/game_manager.hpp"
#include "lilia/model/chess_game.hpp"
#include "lilia/model/move.hpp"
#include "lilia/model/move_generator.hpp"
#include "lilia/model/position.hpp"
#include "lilia/uci/uci_helper.hpp"
#include "lilia/view/render_constants.hpp"

namespace lilia::controller {

namespace {
inline bool isValid(core::Square sq) { return sq != core::NO_SQUARE; }

inline std::string resultToString(core::GameResult res,
                                  core::Color sideToMove) {
  switch (res) {
  case core::GameResult::CHECKMATE:
  case core::GameResult::TIMEOUT:
    return (sideToMove == core::Color::White) ? "0-1" : "1-0";
  case core::GameResult::REPETITION:
  case core::GameResult::MOVERULE:
  case core::GameResult::STALEMATE:
  case core::GameResult::INSUFFICIENT:
    return "1/2-1/2";
  default:
    return "";
  }
}

// chess.com allows multiple queued safe premoves; keep a sane limit
constexpr std::size_t MAX_PREMOVES = 200;
// Preallocate common histories to avoid repeated reallocations
constexpr std::size_t HISTORY_RESERVE = 512;

} // namespace

GameController::GameController(view::GameView &gView, model::ChessGame &game)
    : m_game_view(gView), m_chess_game(game), m_input_manager(),
      m_sound_manager(), m_selection_manager(gView) {
  m_input_manager.setOnClick(
      [this](core::MousePos pos) { this->onClick(pos); });
  m_input_manager.setOnDrag(
      [this](core::MousePos start, core::MousePos current) {
        this->onDrag(start, current);
      });
  m_input_manager.setOnDrop([this](core::MousePos start, core::MousePos end) {
    this->onDrop(start, end);
  });

  m_sound_manager.loadSounds();

  m_game_manager = std::make_unique<GameManager>(game);
  BotPlayer::setEvalCallback([this](int eval) { m_eval_cp.store(eval); });

  m_attack_buffer.reserve(64);
  m_pseudo_buffer.reserve(64);

  m_game_manager->setOnMoveExecuted([this](const model::Move &mv,
                                           bool isPlayerMove, bool onClick) {
    // If the user is viewing history, jump back to head before applying
    if (this->m_fen_index != this->m_fen_history.size() - 1) {
      this->m_fen_index = this->m_fen_history.size() - 1;
      this->m_game_view.setBoardFen(this->m_fen_history[this->m_fen_index]);
      this->m_eval_cp.store(this->m_eval_history[this->m_fen_index]);
      this->m_game_view.updateEval(this->m_eval_history[this->m_fen_index]);
      this->m_game_view.selectMove(this->m_fen_index
                                       ? this->m_fen_index - 1
                                       : static_cast<std::size_t>(-1));
      this->m_game_view.clearAllHighlights();
      if (!this->m_move_history.empty()) {
        const MoveView &info = this->m_move_history.back();
        this->m_selection_manager.setLastMove(info.move.from(), info.move.to());
        this->m_selection_manager.highlightLastMove();
      }
      this->syncCapturedPieces();
      if (this->m_fen_index < this->m_time_history.size()) {
        const TimeView &tv = this->m_time_history[this->m_fen_index];
        this->m_game_view.updateClock(core::Color::White, tv.white);
        this->m_game_view.updateClock(core::Color::Black, tv.black);
        this->m_game_view.setClockActive(tv.active);
      }
      this->m_game_view.restoreRightClickHighlights();
      this->restoreSelectedPiece();
    }

    this->movePieceAndClear(mv, isPlayerMove, onClick);
    this->m_chess_game.checkGameResult();
    this->m_game_view.addMove(move_to_uci(mv));
    this->m_fen_history.push_back(this->m_chess_game.getFen());
    this->m_eval_history.push_back(this->m_eval_cp.load());
    this->m_fen_index = this->m_fen_history.size() - 1;
    this->m_game_view.updateFen(this->m_fen_history.back());
    this->m_game_view.selectMove(this->m_fen_index
                                     ? this->m_fen_index - 1
                                     : static_cast<std::size_t>(-1));
    core::Color stm = this->m_chess_game.getGameState().sideToMove;
    if (this->m_time_controller) {
      core::Color mover = ~stm;
      this->m_time_controller->onMove(mover);
      float w = this->m_time_controller->getTime(core::Color::White);
      float b = this->m_time_controller->getTime(core::Color::Black);
      this->m_game_view.updateClock(core::Color::White, w);
      this->m_game_view.updateClock(core::Color::Black, b);
      this->m_game_view.setClockActive(this->m_time_controller->getActive());
      this->m_time_history.push_back({w, b, stm});
    } else {
      this->m_time_history.push_back({0.f, 0.f, stm});
    }
  });

  m_game_manager->setOnPromotionRequested([this](core::Square sq) {
    this->m_game_view.playPromotionSelectAnim(
        sq, m_chess_game.getGameState().sideToMove);
  });

  m_game_manager->setOnGameEnd([this](core::GameResult res) {
    this->showGameOver(res, m_chess_game.getGameState().sideToMove);
  });
}

GameController::~GameController() {
  shutdown();
}

void GameController::shutdown() {
  if (m_game_manager) m_game_manager->stopGame();
  m_sound_manager.stopBackgroundMusic();
  m_sound_manager.stopAllEffects();
}

void GameController::startGame(const std::string &fen, bool whiteIsBot,
                               bool blackIsBot, int whiteThinkTimeMs,
                               int whiteDepth, int blackThinkTimeMs,
                               int blackDepth, bool useTimer, int baseSeconds,
                               int incrementSeconds) {
  invalidateLegalCache();
  m_sound_manager.playEffect(view::sound::Effect::GameBegins);
  m_game_view.hideResignPopup();
  m_game_view.hideGameOverPopup();
  m_game_view.setGameOver(false);
  m_game_view.init(fen);
  m_game_view.setBotMode(whiteIsBot || blackIsBot);
  m_white_is_bot = whiteIsBot;
  m_black_is_bot = blackIsBot;
  m_game_manager->startGame(fen, whiteIsBot, blackIsBot, whiteThinkTimeMs,
                            whiteDepth, blackThinkTimeMs, blackDepth);

  // Preallocate frequently growing containers to avoid repeated reallocations
  m_fen_history.clear();
  m_eval_history.clear();
  m_move_history.clear();
  m_time_history.clear();
  m_fen_history.reserve(HISTORY_RESERVE);
  m_eval_history.reserve(HISTORY_RESERVE);
  m_move_history.reserve(HISTORY_RESERVE);
  m_time_history.reserve(HISTORY_RESERVE);
  m_premove_queue.clear();
  m_premove_queue.shrink_to_fit();

  if (useTimer) {
    m_time_controller =
        std::make_unique<TimeController>(baseSeconds, incrementSeconds);
    core::Color stm = m_chess_game.getGameState().sideToMove;
    m_time_controller->start(stm);
    m_game_view.setClocksVisible(true);
    m_game_view.updateClock(core::Color::White,
                            static_cast<float>(baseSeconds));
    m_game_view.updateClock(core::Color::Black,
                            static_cast<float>(baseSeconds));
    m_game_view.setClockActive(m_time_controller->getActive());
    m_time_history.push_back({static_cast<float>(baseSeconds),
                              static_cast<float>(baseSeconds), stm});
  } else {
    m_time_controller.reset();
    m_game_view.setClocksVisible(false);
    m_time_history.push_back(
        {0.f, 0.f, m_chess_game.getGameState().sideToMove});
  }
  m_fen_history.push_back(fen);
  m_eval_history.push_back(m_eval_cp.load());
  m_fen_index = 0;
  m_game_view.selectMove(static_cast<std::size_t>(-1));
  m_eval_cp.store(m_eval_history[0]);
  m_game_view.updateEval(m_eval_history[0]);
  m_game_view.clearCapturedPieces();

  // UI-State
  m_mouse_down = false;
  m_dragging = false;
  m_drag_from = core::NO_SQUARE;
  m_preview_active = false;
  m_prev_selected_before_preview = core::NO_SQUARE;
  m_selection_manager.reset();
  m_selection_changed_on_press = false;
  m_stashed_selected_square = core::NO_SQUARE;

  // Premove + Auto-Move
  m_has_pending_auto_move = false;
  m_pending_from = core::NO_SQUARE;
  m_pending_to = core::NO_SQUARE;
  m_pending_capture_type = core::PieceType::None;
  m_pending_promotion = core::PieceType::None;
  m_skip_next_move_animation = false;

  m_game_view.setDefaultCursor();
  m_next_action = NextAction::None;
}

void GameController::handleEvent(const sf::Event &event) {
  // Block all input while a modal popup is open
  if (m_game_view.isResignPopupOpen() || m_game_view.isGameOverPopupOpen()) {
    m_mouse_down = false;
    m_dragging = false;
    m_game_view.setDefaultCursor();

    if (event.type == sf::Event::MouseButtonPressed &&
        event.mouseButton.button == sf::Mouse::Left) {
      core::MousePos mp(event.mouseButton.x, event.mouseButton.y);

      if (m_game_view.isResignPopupOpen()) {
        if (m_game_view.isOnResignYes(mp)) {
          resign();
          m_game_view.hideResignPopup();
        } else if (m_game_view.isOnResignNo(mp) ||
                   m_game_view.isOnModalClose(mp)) {
          m_game_view.hideResignPopup();
        }
      } else if (m_game_view.isGameOverPopupOpen()) {
        if (m_game_view.isOnNewBot(mp)) {
          m_next_action = NextAction::NewBot;
          m_game_view.hideGameOverPopup();
        } else if (m_game_view.isOnRematch(mp)) {
          m_next_action = NextAction::Rematch;
          m_game_view.hideGameOverPopup();
        } else if (m_game_view.isOnModalClose(mp)) {
          m_game_view.hideGameOverPopup();
        }
      }
    }
    return;
  }

  if (event.type == sf::Event::MouseButtonPressed &&
      event.mouseButton.button == sf::Mouse::Left) {
    core::MousePos mp(event.mouseButton.x, event.mouseButton.y);

    if (m_game_view.isOnEvalToggle(mp)) {
      m_game_view.toggleEvalBarVisibility();
      return;
    }
    if (m_game_view.isOnFlipIcon(mp)) {
      m_game_view.toggleBoardOrientation();
      updatePremovePreviews();
      return;
    }

    auto opt = m_game_view.getOptionAt(mp);
    switch (opt) {
    case view::MoveListView::Option::Resign:
      m_game_view.showResignPopup();
      m_mouse_down = false;
      m_dragging = false;
      m_game_view.setDefaultCursor();
      return;
    case view::MoveListView::Option::Prev:
      stepBackward();
      return;
    case view::MoveListView::Option::Next:
      stepForward();
      return;
    case view::MoveListView::Option::Settings:
      return;
    case view::MoveListView::Option::NewBot:
      m_next_action = NextAction::NewBot;
      return;
    case view::MoveListView::Option::Rematch:
      m_next_action = NextAction::Rematch;
      return;
    case view::MoveListView::Option::ShowFen:
      sf::Clipboard::setString(m_fen_history[m_fen_index]);
      return;
    default:
      break;
    }

    std::size_t idx = m_game_view.getMoveIndexAt(
        core::MousePos(event.mouseButton.x, event.mouseButton.y));
    if (idx != static_cast<std::size_t>(-1)) {
      clearPremove();
      const bool leavingFinalState =
          (m_chess_game.getResult() != core::GameResult::ONGOING &&
           m_fen_index == m_fen_history.size() - 1 &&
           idx + 1 != m_fen_history.size() - 1);
      const bool enteringFinalState =
          (m_chess_game.getResult() != core::GameResult::ONGOING &&
           idx + 1 == m_fen_history.size() - 1);

      if (leavingFinalState)
        m_game_view.resetEvalBar();

      if (m_fen_index == m_fen_history.size() - 1 &&
          idx + 1 != m_fen_history.size() - 1) {
        m_game_view.stashRightClickHighlights();
        stashSelectedPiece();
      }

      m_fen_index = idx + 1;
      m_game_view.setBoardFen(m_fen_history[m_fen_index]);
      m_game_view.selectMove(idx);
      const MoveView &info = m_move_history[idx];
      m_selection_manager.setLastMove(info.move.from(), info.move.to());
      m_game_view.clearAllHighlights();
      m_selection_manager.highlightLastMove();
      m_sound_manager.playEffect(info.sound);
      m_eval_cp.store(m_eval_history[m_fen_index]);
      m_game_view.updateEval(m_eval_history[m_fen_index]);
      if (enteringFinalState) {
        m_game_view.setEvalResult(resultToString(
            m_chess_game.getResult(), m_chess_game.getGameState().sideToMove));
      }
      if (m_fen_index < m_time_history.size()) {
        const TimeView &tv = m_time_history[m_fen_index];
        m_game_view.updateClock(core::Color::White, tv.white);
        m_game_view.updateClock(core::Color::Black, tv.black);
        bool latest = (m_fen_index == m_fen_history.size() - 1 &&
                       m_chess_game.getResult() == core::GameResult::ONGOING);
        if (latest)
          m_game_view.setClockActive(tv.active);
        else
          m_game_view.setClockActive(std::nullopt);
      }
      syncCapturedPieces();
      if (m_fen_index == m_fen_history.size() - 1) {
        m_game_view.restoreRightClickHighlights();
        restoreSelectedPiece();
      }
      return;
    }
  }

  if (event.type == sf::Event::MouseWheelScrolled) {
    m_game_view.scrollMoveList(event.mouseWheelScroll.delta);
    if (m_fen_index != m_fen_history.size() - 1)
      return;
  }

  if (event.type == sf::Event::KeyPressed) {
    if (event.key.code == sf::Keyboard::Left) {
      stepBackward();
      return;
    }
    if (event.key.code == sf::Keyboard::Right) {
      stepForward();
      return;
    }
  }
  if (m_fen_index != m_fen_history.size() - 1)
    return;

  if (m_chess_game.getResult() != core::GameResult::ONGOING) {
    if (event.type == sf::Event::MouseButtonPressed &&
        event.mouseButton.button == sf::Mouse::Left) {
      core::MousePos mp(event.mouseButton.x, event.mouseButton.y);
      if (m_game_view.isOnFlipIcon(mp)) {
        m_game_view.toggleBoardOrientation();
        updatePremovePreviews();
      }
    }
    return;
  }

  switch (event.type) {
  case sf::Event::MouseMoved:
    onMouseMove(core::MousePos(event.mouseMove.x, event.mouseMove.y));
    break;
  case sf::Event::MouseButtonPressed:
    if (event.mouseButton.button == sf::Mouse::Left)
      onMousePressed(core::MousePos(event.mouseButton.x, event.mouseButton.y));
    else if (event.mouseButton.button == sf::Mouse::Right)
      onRightPressed(core::MousePos(event.mouseButton.x, event.mouseButton.y));
    break;
  case sf::Event::MouseButtonReleased:
    if (event.mouseButton.button == sf::Mouse::Left)
      onMouseReleased(core::MousePos(event.mouseButton.x, event.mouseButton.y));
    else if (event.mouseButton.button == sf::Mouse::Right)
      onRightReleased(core::MousePos(event.mouseButton.x, event.mouseButton.y));
    break;
  case sf::Event::MouseLeft:
    break;
  case sf::Event::MouseEntered: {
    m_mouse_down = sf::Mouse::isButtonPressed(sf::Mouse::Left);
    if (m_dragging) {
      core::MousePos mp = m_game_view.getMousePosition();
      if (!m_mouse_down) {
        snapAndReturn(m_drag_from, mp);
        m_dragging = false;
        m_drag_from = core::NO_SQUARE;
        m_game_view.clearDraggingPiece();
        m_input_manager.cancelDrag();
        m_game_view.setDefaultCursor();
      } else {
        m_game_view.setPieceToMouseScreenPos(m_drag_from, mp);
      }
    }
    break;
  }
  case sf::Event::LostFocus:
    m_mouse_down = false;
    if (m_dragging) {
      core::MousePos mp = m_game_view.getMousePosition();
      snapAndReturn(m_drag_from, mp);
      m_dragging = false;
      m_drag_from = core::NO_SQUARE;
      m_game_view.clearDraggingPiece();
      m_input_manager.cancelDrag();
    }
    m_game_view.setDefaultCursor();
    break;
  default:
    break;
  }
  m_input_manager.processEvent(event);
}

/* -------------------- Mouse handling -------------------- */
void GameController::onMouseMove(core::MousePos pos) {
  if (m_dragging || m_mouse_down) {
    m_game_view.setHandClosedCursor();
    return;
  }

  const core::Square sq = m_game_view.mousePosToSquare(pos);
  if (m_game_view.hasPieceOnSquare(sq) && !m_game_view.isInPromotionSelection())
    m_game_view.setHandOpenCursor();
  else
    m_game_view.setDefaultCursor();
}

void GameController::onMousePressed(core::MousePos pos) {
  m_mouse_down = true;

  if (m_game_view.isInPromotionSelection()) {
    m_game_view.setHandClosedCursor();
    return;
  }

  const core::Square sq = m_game_view.mousePosToSquare(pos);
  m_selection_changed_on_press = false;

  if (!hasVirtualPiece(sq)) {
    m_game_view.setDefaultCursor();
    m_game_view.clearRightClickHighlights();
    return;
  }

  const core::Square currentSelected = m_selection_manager.getSelectedSquare();
  const bool selectionWasDifferent = (currentSelected != sq);

  if (currentSelected != core::NO_SQUARE && currentSelected != sq) {
    m_preview_active = true;
    m_prev_selected_before_preview = currentSelected;

    if (!tryMove(currentSelected, sq)) {
      m_game_view.clearNonPremoveHighlights();
      m_selection_manager.highlightLastMove();
      m_selection_manager.selectSquare(sq);
      m_selection_manager.hoverSquare(sq);
      if (isHumanPiece(sq))
        showAttacks(getAttackSquares(sq));
    }
  } else {
    m_preview_active = false;
    m_prev_selected_before_preview = core::NO_SQUARE;

    m_game_view.clearNonPremoveHighlights();
    m_selection_manager.highlightLastMove();
    m_selection_manager.selectSquare(sq);
    m_selection_manager.hoverSquare(sq);
    if (isHumanPiece(sq))
      showAttacks(getAttackSquares(sq));
  }

  if (!tryMove(currentSelected, sq)) {
    m_dragging = true;
    m_drag_from = sq;
    m_game_view.setPieceToMouseScreenPos(sq, pos);
    m_game_view.playPiecePlaceHolderAnimation(sq);
  }

  m_selection_changed_on_press =
      selectionWasDifferent && (m_selection_manager.getSelectedSquare() == sq);
}

void GameController::onMouseReleased(core::MousePos pos) {
  m_mouse_down = false;
  if (m_dragging) {
    m_dragging = false;
    m_drag_from = core::NO_SQUARE;
    m_game_view.clearDraggingPiece();
  }
  m_preview_active = false;
  m_prev_selected_before_preview = core::NO_SQUARE;
  onMouseMove(pos);
}

void GameController::onRightPressed(core::MousePos pos) {
  m_right_mouse_down = true;
  m_right_press_time = std::chrono::steady_clock::now();
  m_right_drag_from = m_game_view.mousePosToSquare(pos);
}

void GameController::onRightReleased(core::MousePos pos) {
  if (!m_right_mouse_down)
    return;
  m_right_mouse_down = false;
  const core::Square endSq = m_game_view.mousePosToSquare(pos);
  const core::Square startSq = m_right_drag_from;
  m_right_drag_from = core::NO_SQUARE;
  if (!isValid(startSq) || !isValid(endSq))
    return;

  auto elapsed = std::chrono::steady_clock::now() - m_right_press_time;
  bool heldLong =
      std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() >
      200;
  if (startSq != endSq && heldLong) {
    m_game_view.highlightRightClickArrow(startSq, endSq);
  } else {
    const bool hasPiece = hasVirtualPiece(endSq);
    if (!hasPiece)
      clearPremove();
    m_game_view.highlightRightClickSquare(endSq);
  }
}

/* -------------------- Main loop hooks -------------------- */
void GameController::render() { m_game_view.render(); }

void GameController::update(float dt) {
  // Always tick UI/animations/particles
  m_game_view.update(dt);
  m_game_view.updateEval(m_eval_cp.load());

  if (m_chess_game.getResult() != core::GameResult::ONGOING)
    return;

  // ----- Clocks -----
  if (m_time_controller) {
    m_time_controller->update(dt);
    if (!m_time_history.empty()) {
      m_time_history.back().white =
          m_time_controller->getTime(core::Color::White);
      m_time_history.back().black =
          m_time_controller->getTime(core::Color::Black);
    }
    if (m_fen_index == m_fen_history.size() - 1) {
      m_game_view.updateClock(core::Color::White,
                              m_time_controller->getTime(core::Color::White));
      m_game_view.updateClock(core::Color::Black,
                              m_time_controller->getTime(core::Color::Black));
      m_game_view.setClockActive(m_time_controller->getActive());
    } else if (m_fen_index < m_time_history.size()) {
      const TimeView &tv = m_time_history[m_fen_index];
      m_game_view.updateClock(core::Color::White, tv.white);
      m_game_view.updateClock(core::Color::Black, tv.black);
      m_game_view.setClockActive(std::nullopt);
    }
    if (auto flag = m_time_controller->getFlagged()) {
      m_chess_game.setResult(core::GameResult::TIMEOUT);
      if (m_game_manager)
        m_game_manager->stopGame();
      showGameOver(core::GameResult::TIMEOUT, *flag);
      return;
    }
  }

  // ----- Engine / bots -----
  if (m_game_manager)
    m_game_manager->update(dt);

  // ----- Auto-play the queued head premove when our turn starts -----
  if (m_has_pending_auto_move) {
    const auto st = m_chess_game.getGameState();
    const bool humansTurn =
        (m_game_manager && m_game_manager->isHuman(st.sideToMove));

    if (humansTurn && hasCurrentLegalMove(m_pending_from, m_pending_to)) {
      // Refresh capture info from the live board if available
      if (auto cap = m_chess_game.getPiece(m_pending_to);
          cap.type != core::PieceType::None) {
        m_pending_capture_type = cap.type;
      }

      // 1) Instant visuals (avoid flicker)
      m_game_view.applyPremoveInstant(m_pending_from, m_pending_to,
                                      m_pending_promotion);

      // 1a) If it's castling, move the rook instantly as well
      auto pc = m_chess_game.getPiece(m_pending_from);
      if (pc.type == core::PieceType::King &&
          std::abs(static_cast<int>(m_pending_to) -
                   static_cast<int>(m_pending_from)) == 2) {
        core::Square rookFrom =
            (m_pending_to > m_pending_from)
                ? static_cast<core::Square>(m_pending_to + 1)
                : static_cast<core::Square>(m_pending_to - 2);
        core::Square rookTo = (m_pending_to > m_pending_from)
                                  ? static_cast<core::Square>(m_pending_to - 1)
                                  : static_cast<core::Square>(m_pending_to + 1);
        m_game_view.applyPremoveInstant(rookFrom, rookTo,
                                        core::PieceType::None);
      }

      // 2) Hand it to the game manager (promotion handled internally)
      bool accepted = m_game_manager
                          ? m_game_manager->requestUserMove(
                                m_pending_from, m_pending_to,
                                /*onClick*/ true, m_pending_promotion)
                          : false;

      if (!accepted) {
        // Roll back visuals to the last known state; cancel the chain
        m_game_view.setBoardFen(m_fen_history.back());
        m_game_view.restoreRightClickHighlights();
        clearPremove();
      } else {
        // IMPORTANT: Do NOT pop the queue here — it was already popped when
        // scheduling in movePieceAndClear(). Just rebuild highlights/ghosts for
        // what's left.
        m_game_view.clearPremoveHighlights();
        for (const auto &remaining : m_premove_queue) {
          m_game_view.highlightPremoveSquare(remaining.from);
          m_game_view.highlightPremoveSquare(remaining.to);
        }
        updatePremovePreviews();
      }

      // One-shot flags are consumed now
      m_has_pending_auto_move = false;
      m_pending_from = m_pending_to = core::NO_SQUARE;
      m_pending_promotion = core::PieceType::None;
      m_pending_capture_type = core::PieceType::None;

    } else if (humansTurn) {
      // Drop only the head that became illegal and try to schedule the next
      m_has_pending_auto_move = false;
      m_pending_from = m_pending_to = core::NO_SQUARE;
      m_pending_promotion = core::PieceType::None;
      m_pending_capture_type = core::PieceType::None;

      const auto st2 = m_chess_game.getGameState();
      while (!m_premove_queue.empty() &&
             m_premove_queue.front().moverColor == st2.sideToMove) {
        Premove pm2 = m_premove_queue.front();
        m_premove_queue.pop_front();
        if (hasCurrentLegalMove(pm2.from, pm2.to)) {
          m_has_pending_auto_move = true;
          m_pending_from = pm2.from;
          m_pending_to = pm2.to;
          m_pending_capture_type = pm2.capturedType;
          m_pending_promotion = pm2.promotion;
          m_skip_next_move_animation = true;
          break;
        }
      }

      m_game_view.clearPremoveHighlights();
      for (const auto &remaining : m_premove_queue) {
        m_game_view.highlightPremoveSquare(remaining.from);
        m_game_view.highlightPremoveSquare(remaining.to);
      }
      updatePremovePreviews();

    } else {
      // Not our turn *yet* — keep the pending state and try again next frame.
      // (Do NOT clear flags or the queue here.)
    }
  }

  // If a piece is being held or selected while turns switch, ensure the
  // attack highlights reflect the current player to move instead of lingering
  // premove previews.
  core::Square activeSq = core::NO_SQUARE;
  if (m_dragging && isValid(m_drag_from)) {
    activeSq = m_drag_from;
  } else if (isValid(m_selection_manager.getSelectedSquare())) {
    activeSq = m_selection_manager.getSelectedSquare();
  }
  if (isValid(activeSq) && isHumanPiece(activeSq)) {
    showAttacks(getAttackSquares(activeSq));
  }
}

/* -------------------- Highlights -------------------- */
/* -------------------- Premove queue -------------------- */
bool GameController::enqueuePremove(core::Square from, core::Square to) {
  // Only allow premove for the human side NOT to move
  const auto st = m_chess_game.getGameState();
  if (!m_game_manager || !m_game_manager->isHuman(~st.sideToMove))
    return false;

  if (m_premove_queue.size() >= MAX_PREMOVES)
    return false;
  if (!isPseudoLegalPremove(from, to))
    return false;

  // Use virtual position AFTER current queue to determine mover/captures
  model::Position pos = getPositionAfterPremoves();

  auto moverOpt = pos.getBoard().getPiece(from);
  if (!moverOpt)
    return false;

  const core::PieceType moverType = moverOpt->type;
  const core::Color moverColor = moverOpt->color;

  // Capture info from virtual board
  core::PieceType capType = core::PieceType::None;
  core::Color capColor = core::Color::White;
  if (auto cap = pos.getBoard().getPiece(to)) {
    capType = cap->type;
    capColor = cap->color;
  }

  // Promotion? -> defer until user selects piece type
  int rank = static_cast<int>(to) / 8;
  if (moverType == core::PieceType::Pawn && (rank == 0 || rank == 7)) {
    m_game_view.playPromotionSelectAnim(to, moverColor);
    m_pending_premove_promotion = true;
    m_ppromo_from = from;
    m_ppromo_to = to;
    m_ppromo_captured_type = capType;
    m_ppromo_captured_color = capColor;
    m_ppromo_mover_color = moverColor;
    return false;
  }

  Premove pm{};
  pm.from = from;
  pm.to = to;
  pm.moverColor = ~st.sideToMove;
  pm.promotion = core::PieceType::None;
  pm.capturedType = capType;
  pm.capturedColor = capColor;

  // Visuals
  m_game_view.clearAttackHighlights();
  m_game_view.clearHighlightSquare(from);
  m_game_view.highlightPremoveSquare(from);
  m_game_view.highlightPremoveSquare(to);
  m_game_view.highlightSquare(to);

  m_premove_queue.push_back(pm);
  m_sound_manager.playEffect(view::sound::Effect::Premove);

  // Rebuild preview so ghosts end up on the latest squares per piece
  updatePremovePreviews();

  return true;
}

void GameController::clearPremove() {
  if (!m_premove_queue.empty() || m_pending_premove_promotion) {
    for (const auto &pm : m_premove_queue) {
      m_game_view.clearHighlightSquare(pm.to);
    }
    if (m_pending_premove_promotion && m_ppromo_to != core::NO_SQUARE)
      m_game_view.clearHighlightSquare(m_ppromo_to);
    m_premove_queue.clear();
    m_game_view.clearPremoveHighlights();
    m_game_view.clearPremovePieces(true); // restore any stashed captures
    m_selection_manager.highlightLastMove();
    m_pending_premove_promotion = false;
    m_ppromo_from = m_ppromo_to = core::NO_SQUARE;
  }
}

void GameController::updatePremovePreviews() {
  // Rebuild ghosts from the *model* head position; never trust transient view
  // state.
  m_game_view.clearPremovePieces(true);

  // Start from the live game position and apply queued premoves virtually,
  // step by step, so later premoves "see" the earlier ones.
  model::Position pos = m_chess_game.getPositionRefForBot();

  for (const auto &pm : m_premove_queue) {
    auto moverOpt = pos.getBoard().getPiece(pm.from);
    if (!moverOpt) {
      // If the mover is unexpectedly missing in the virtual chain, skip drawing
      // but still keep the chain consistent by not crashing.
      continue;
    }

    const core::PieceType movingType = moverOpt->type;
    const core::Color movingCol = moverOpt->color;

    // Draw the ghost for this premove (promotion handled by view).
    m_game_view.showPremovePiece(pm.from, pm.to, pm.promotion);

    // Castling: also draw the rook's ghost based on King move semantics.
    if (movingType == core::PieceType::King &&
        std::abs(static_cast<int>(pm.to) - static_cast<int>(pm.from)) == 2) {
      core::Square rookFrom = (pm.to > pm.from)
                                  ? static_cast<core::Square>(pm.to + 1)
                                  : static_cast<core::Square>(pm.to - 2);
      core::Square rookTo = (pm.to > pm.from)
                                ? static_cast<core::Square>(pm.to - 1)
                                : static_cast<core::Square>(pm.to + 1);
      m_game_view.showPremovePiece(rookFrom, rookTo);
    }

    // --- Advance the virtual board for the next premove in the chain ---

    // Remove captured piece (incl. potential en-passant victim).
    if (pm.capturedType != core::PieceType::None) {
      if (pos.getBoard().getPiece(pm.to)) {
        pos.getBoard().removePiece(pm.to);
      } else if (movingType == core::PieceType::Pawn &&
                 ((static_cast<int>(pm.from) ^ static_cast<int>(pm.to)) & 7)) {
        // Diagonal pawn move onto empty square → en-passant capture
        core::Square epSq = (movingCol == core::Color::White)
                                ? static_cast<core::Square>(pm.to - 8)
                                : static_cast<core::Square>(pm.to + 8);
        pos.getBoard().removePiece(epSq);
      }
    }

    // Move the piece and handle promotion.
    model::bb::Piece moving = *moverOpt;
    pos.getBoard().removePiece(pm.from);
    if (pm.promotion != core::PieceType::None)
      moving.type = pm.promotion;
    pos.getBoard().setPiece(pm.to, moving);

    // Handle castling rook in the virtual board as well.
    if (moving.type == core::PieceType::King &&
        std::abs(static_cast<int>(pm.to) - static_cast<int>(pm.from)) == 2) {
      core::Square rookFrom = (pm.to > pm.from)
                                  ? static_cast<core::Square>(pm.to + 1)
                                  : static_cast<core::Square>(pm.to - 2);
      core::Square rookTo = (pm.to > pm.from)
                                ? static_cast<core::Square>(pm.to - 1)
                                : static_cast<core::Square>(pm.to + 1);
      if (auto rook = pos.getBoard().getPiece(rookFrom)) {
        pos.getBoard().removePiece(rookFrom);
        pos.getBoard().setPiece(rookTo, *rook);
      }
    }
  }
}

void GameController::movePieceAndClear(const model::Move &move,
                                       bool isPlayerMove, bool onClick) {
  invalidateLegalCache();
  const core::Square from = move.from();
  const core::Square to = move.to();

  // 1) Resolve drag conflicts
  if (m_dragging && m_drag_from == from) {
    m_dragging = false;
    m_mouse_down = false;
    m_selection_manager.dehoverSquare();
    m_game_view.setPieceToSquareScreenPos(from, from);
    m_game_view.endAnimation(from);
  }

  // 2) Selection cleanup
  if (m_selection_manager.getSelectedSquare() == from ||
      m_selection_manager.getSelectedSquare() == to)
    m_selection_manager.deselectSquare();
  m_preview_active = false;
  m_prev_selected_before_preview = core::NO_SQUARE;
  m_game_view.clearRightClickHighlights();

  // 3) En-passant victim square for visuals
  core::Square epVictimSq = core::NO_SQUARE;
  const core::Color moverColorBefore = ~m_chess_game.getGameState().sideToMove;
  if (move.isEnPassant()) {
    epVictimSq = (moverColorBefore == core::Color::White)
                     ? static_cast<core::Square>(to - 8)
                     : static_cast<core::Square>(to + 8);
  }

  // 3b) Resolve captured piece type (prefer pending/cached info from premove
  // path)
  core::PieceType capturedType = core::PieceType::None;
  if (move.isCapture()) {
    if (m_pending_capture_type != core::PieceType::None) {
      capturedType = m_pending_capture_type;
    } else {
      core::Square capSq = move.isEnPassant() ? epVictimSq : to;
      capturedType = m_game_view.getPieceType(capSq);
    }
  }

  // Keep the skip flag stable for the entire move (king + rook)
  const bool skipAnim = m_skip_next_move_animation;

  // If we already applied the instant premove, remove the EP victim now
  if (skipAnim && move.isEnPassant() && epVictimSq != core::NO_SQUARE) {
    m_game_view.removePiece(epVictimSq);
  }

  // 4) Main mover (animate unless we’re in the instant/premove path)
  if (!skipAnim) {
    if (onClick)
      m_game_view.animationMovePiece(from, to, epVictimSq, move.promotion());
    else
      m_game_view.animationDropPiece(from, to, epVictimSq, move.promotion());
  }

  // 5) Castling rook
  if (move.castle() != model::CastleSide::None) {
    const core::Square rookFrom = m_chess_game.getRookSquareFromCastleside(
        move.castle(), moverColorBefore);
    const core::Square rookTo = (move.castle() == model::CastleSide::KingSide)
                                    ? static_cast<core::Square>(to - 1)
                                    : static_cast<core::Square>(to + 1);

    if (!skipAnim) {
      // Normal path: animate the rook
      m_game_view.animationMovePiece(rookFrom, rookTo);
    } else {
      // Instant premove path: the rook was already moved via
      // applyPremoveInstant in update() → do nothing here to avoid
      // double-moving or flicker.
    }
  }

  // One-shot flags can be cleared now
  m_skip_next_move_animation = false;
  m_pending_capture_type = core::PieceType::None;

  // 6) Visuals / Sounds
  m_selection_manager.clearLastMoveHighlight();
  m_selection_manager.setLastMove(from, to);
  m_selection_manager.highlightLastMove();
  if (isValid(m_selection_manager.getSelectedSquare()))
    m_game_view.highlightSquare(m_selection_manager.getSelectedSquare());

  const core::Color sideToMoveNow = m_chess_game.getGameState().sideToMove;

  view::sound::Effect effect;
  if (m_chess_game.isKingInCheck(sideToMoveNow))
    effect = view::sound::Effect::Check;
  else if (move.promotion() != core::PieceType::None)
    effect = view::sound::Effect::Promotion;
  else if (move.isCapture())
    effect = view::sound::Effect::Capture;
  else if (move.castle() != model::CastleSide::None)
    effect = view::sound::Effect::Castle;
  else
    effect = isPlayerMove ? view::sound::Effect::PlayerMove
                          : view::sound::Effect::EnemyMove;

  m_sound_manager.playEffect(effect);
  if (move.isCapture())
    m_game_view.addCapturedPiece(moverColorBefore, capturedType);
  m_move_history.push_back(
      {move, moverColorBefore, capturedType, effect, m_eval_cp.load()});

  // 7) Safe premove processing (queue head)
  if (!m_premove_queue.empty()) {
    const core::Color sideToMoveNow = m_chess_game.getGameState().sideToMove;

    bool scheduled = false;
    while (!m_premove_queue.empty()) {
      Premove pm = m_premove_queue.front();
      if (pm.moverColor != sideToMoveNow || !m_game_manager ||
          !m_game_manager->isHuman(sideToMoveNow)) {
        break; // not our turn or not human
      }

      m_premove_queue.pop_front(); // drop the head we're examining

      if (hasCurrentLegalMove(pm.from, pm.to)) {
        // schedule this one
        m_has_pending_auto_move = true;
        m_pending_from = pm.from;
        m_pending_to = pm.to;
        m_pending_capture_type = pm.capturedType;
        m_pending_promotion = pm.promotion;
        m_skip_next_move_animation = true;
        scheduled = true;
        break;
      }
      // head was illegal → keep looping to try the next queued premove
    }

    // Rebuild highlights/ghosts for what's left
    m_game_view.clearPremoveHighlights();
    for (const auto &remaining : m_premove_queue) {
      m_game_view.highlightPremoveSquare(remaining.from);
      m_game_view.highlightPremoveSquare(remaining.to);
    }
    updatePremovePreviews();
  }
}

/* ------------------------------------------------------------------------ */

void GameController::snapAndReturn(core::Square sq, core::MousePos cur) {
  m_selection_manager.selectSquare(sq);
  m_game_view.animationSnapAndReturn(sq, cur);
}

[[nodiscard]] bool GameController::tryMove(core::Square a, core::Square b) {
  if (!isHumanPiece(a))
    return false;
  for (auto att : getAttackSquares(a)) {
    if (att == b)
      return true;
  }
  return false;
}

[[nodiscard]] bool GameController::isPromotion(core::Square a, core::Square b) {
  ensureLegalCache();
  for (const auto &m : *m_cached_moves) {
    if (m.from() == a && m.to() == b && m.promotion() != core::PieceType::None)
      return true;
  }
  return false;
}

[[nodiscard]] bool GameController::isSameColor(core::Square a, core::Square b) {
  return m_game_view.isSameColorPiece(a, b);
}

const std::vector<core::Square> &
GameController::getAttackSquares(core::Square pieceSQ) const {
  m_attack_buffer.clear();
  if (!isValid(pieceSQ))
    return m_attack_buffer;

  core::PieceType vType = m_game_view.getPieceType(pieceSQ);
  core::Color vCol = m_game_view.getPieceColor(pieceSQ);
  const bool hasVirtual = (vType != core::PieceType::None);
  const bool premoveContext =
      hasVirtual && (vCol != m_chess_game.getGameState().sideToMove);

  if (premoveContext) {
    model::Board board;
    board.clear();
    board.setPiece(pieceSQ, {vType, vCol});

    model::GameState st{};
    st.sideToMove = vCol;
    st.castlingRights = 0;
    st.enPassantSquare = core::NO_SQUARE;

    if (vType == core::PieceType::Pawn) {
      const int file = static_cast<int>(pieceSQ) & 7;
      const int forward = (vCol == core::Color::White) ? 8 : -8;
      const model::bb::Piece dummy{core::PieceType::Pawn, ~vCol};
      if (file > 0)
        board.setPiece(
            static_cast<core::Square>(static_cast<int>(pieceSQ) + forward - 1),
            dummy);
      if (file < 7)
        board.setPiece(
            static_cast<core::Square>(static_cast<int>(pieceSQ) + forward + 1),
            dummy);
    }

    m_pseudo_buffer.clear();
    m_movegen.generatePseudoLegalMoves(board, st, m_pseudo_buffer);
    for (const auto &m : m_pseudo_buffer)
      if (m.from() == pieceSQ)
        m_attack_buffer.push_back(m.to());
    return m_attack_buffer;
  }

  ensureLegalCache();
  for (const auto &m : *m_cached_moves)
    if (m.from() == pieceSQ)
      m_attack_buffer.push_back(m.to());
  return m_attack_buffer;
}

void GameController::showAttacks(const std::vector<core::Square> &att) {
  m_game_view.clearAttackHighlights();
  for (auto sq : att) {
    if (hasVirtualPiece(sq))
      m_game_view.highlightCaptureSquare(sq);
    else
      m_game_view.highlightAttackSquare(sq);
  }
}

void GameController::onClick(core::MousePos mousePos) {
  if (m_game_view.isOnFlipIcon(mousePos)) {
    m_game_view.toggleBoardOrientation();
    updatePremovePreviews();
    return;
  }
  const core::Square sq = m_game_view.mousePosToSquare(mousePos);
  if (!isValid(sq)) {
    m_selection_changed_on_press = false;
    if (m_selection_manager.getSelectedSquare() != core::NO_SQUARE)
      m_selection_manager.deselectSquare();
    return;
  }
  // piece might have been moved on mouse press without dragging - reset it
  if (m_game_view.hasPieceOnSquare(sq)) {
    m_game_view.endAnimation(sq);
    m_game_view.setPieceToSquareScreenPos(sq, sq);
  }

  if (m_selection_changed_on_press &&
      sq == m_selection_manager.getSelectedSquare()) {
    m_selection_changed_on_press = false;
    return;
  }
  m_selection_changed_on_press = false;

  // Promotion dialog?
  if (m_game_view.isInPromotionSelection()) {
    const core::PieceType promoType =
        m_game_view.getSelectedPromotion(mousePos);
    m_game_view.removePromotionSelection();
    if (m_pending_premove_promotion) {
      if (promoType != core::PieceType::None) {
        Premove pm{};
        pm.from = m_ppromo_from;
        pm.to = m_ppromo_to;
        pm.promotion = promoType;
        pm.capturedType = m_ppromo_captured_type;
        pm.capturedColor = m_ppromo_captured_color;
        pm.moverColor = m_ppromo_mover_color;

        m_game_view.clearAttackHighlights();
        m_game_view.clearHighlightSquare(pm.from);
        m_game_view.highlightPremoveSquare(pm.from);
        m_game_view.highlightPremoveSquare(pm.to);
        m_game_view.highlightSquare(pm.to);

        m_premove_queue.push_back(pm);
        m_sound_manager.playEffect(view::sound::Effect::Premove);
        updatePremovePreviews();
      }
      m_pending_premove_promotion = false;
      m_ppromo_from = m_ppromo_to = core::NO_SQUARE;
      m_selection_manager.deselectSquare();
      return;
    }
    if (m_game_manager)
      m_game_manager->completePendingPromotion(promoType);
    m_selection_manager.deselectSquare();
    return;
  }

  // If something is selected, try that move first
  if (m_selection_manager.getSelectedSquare() != core::NO_SQUARE) {
    const auto st = m_chess_game.getGameState();
    auto selPiece =
        getPieceConsideringPremoves(m_selection_manager.getSelectedSquare());
    const bool ownTurnAndPiece =
        (selPiece.type != core::PieceType::None &&
         st.sideToMove == selPiece.color &&
         (!m_game_manager || m_game_manager->isHuman(st.sideToMove)));
    const core::Color humanColor = ~st.sideToMove;
    const bool canPremove =
        (selPiece.type != core::PieceType::None &&
         selPiece.color == humanColor &&
         (!m_game_manager || m_game_manager->isHuman(humanColor)));

    if (ownTurnAndPiece &&
        tryMove(m_selection_manager.getSelectedSquare(), sq)) {
      if (m_game_manager) {
        (void)m_game_manager->requestUserMove(
            m_selection_manager.getSelectedSquare(), sq,
            /*onClick*/ true);
      }
      m_selection_manager.deselectSquare();
      return; // don't reselect
    }
    if (!ownTurnAndPiece && canPremove) {
      if (sq == m_selection_manager.getSelectedSquare()) {
        m_selection_manager.deselectSquare();
      } else {
        enqueuePremove(m_selection_manager.getSelectedSquare(), sq);
        m_selection_manager.deselectSquare();
      }
      return; // don't reselect
    }

    // Not a legal click move -> maybe change selection
    if (hasVirtualPiece(sq)) {
      if (sq == m_selection_manager.getSelectedSquare()) {
        m_selection_manager.deselectSquare();
      } else {
        m_game_view.clearNonPremoveHighlights();
        m_selection_manager.highlightLastMove();
        m_selection_manager.selectSquare(sq);
        if (isHumanPiece(sq))
          showAttacks(getAttackSquares(sq));
      }
    } else {
      m_selection_manager.deselectSquare();
    }
    return;
  }

  // Nothing selected yet: select if there is a (virtual) piece
  if (hasVirtualPiece(sq)) {
    m_game_view.clearNonPremoveHighlights();
    m_selection_manager.highlightLastMove();
    m_selection_manager.selectSquare(sq);
    if (isHumanPiece(sq))
      showAttacks(getAttackSquares(sq));
  }
}

void GameController::onDrag(core::MousePos start, core::MousePos current) {
  const core::Square sqStart = m_game_view.mousePosToSquare(start);
  const core::MousePos clamped = m_game_view.clampPosToBoard(current);
  const core::Square sqMous = m_game_view.mousePosToSquare(clamped);

  if (m_game_view.isInPromotionSelection())
    return;
  if (!hasVirtualPiece(sqStart))
    return;
  if (!m_dragging)
    return;

  // Ensure start is selected
  if (m_selection_manager.getSelectedSquare() != sqStart) {
    m_game_view.clearNonPremoveHighlights();
    m_selection_manager.highlightLastMove();
    m_selection_manager.selectSquare(sqStart);
    if (isHumanPiece(sqStart))
      showAttacks(getAttackSquares(sqStart));
  }

  if (m_selection_manager.getHoveredSquare() != sqMous)
    m_selection_manager.dehoverSquare();
  m_selection_manager.hoverSquare(sqMous);

  m_game_view.setPieceToMouseScreenPos(sqStart, current);
  m_game_view.playPiecePlaceHolderAnimation(sqStart);
}

void GameController::onDrop(core::MousePos start, core::MousePos end) {
  const core::Square from = m_game_view.mousePosToSquare(start);
  const core::Square to =
      m_game_view.mousePosToSquare(m_game_view.clampPosToBoard(end));

  m_selection_manager.dehoverSquare();

  if (m_game_view.isInPromotionSelection())
    return;

  if (!hasVirtualPiece(from)) {
    m_selection_manager.deselectSquare();
    m_preview_active = false;
    m_prev_selected_before_preview = core::NO_SQUARE;
    return;
  }

  // End drag placeholder before doing anything
  m_game_view.endAnimation(from);

  // If the drop position is outside the board and the cursor isn't over the
  // piece, snap the piece back to its origin square immediately.
  if (m_game_view.mousePosToSquare(end) == core::NO_SQUARE) {
    auto size = m_game_view.getPieceSize(from);
    auto clamped = m_game_view.clampPosToBoard(end, size);
    const float halfW = size.x / 2.f;
    const float halfH = size.y / 2.f;
    const float left = static_cast<float>(clamped.x) - halfW;
    const float right = static_cast<float>(clamped.x) + halfW;
    const float top = static_cast<float>(clamped.y) - halfH;
    const float bottom = static_cast<float>(clamped.y) + halfH;
    const bool overPiece = static_cast<float>(end.x) >= left &&
                           static_cast<float>(end.x) <= right &&
                           static_cast<float>(end.y) >= top &&
                           static_cast<float>(end.y) <= bottom;
    if (!overPiece) {
      m_game_view.setPieceToSquareScreenPos(from, from);
      m_game_view.animationSnapAndReturn(from, end);

      if (m_preview_active && isValid(m_prev_selected_before_preview) &&
          m_prev_selected_before_preview != from) {
        m_game_view.clearNonPremoveHighlights();
        m_selection_manager.highlightLastMove();
        m_selection_manager.selectSquare(m_prev_selected_before_preview);
        if (isHumanPiece(m_prev_selected_before_preview))
          showAttacks(getAttackSquares(m_prev_selected_before_preview));
      } else {
        m_game_view.clearNonPremoveHighlights();
        m_selection_manager.highlightLastMove();
        m_selection_manager.selectSquare(from);
        if (isHumanPiece(from))
          showAttacks(getAttackSquares(from));
      }

      m_preview_active = false;
      m_prev_selected_before_preview = core::NO_SQUARE;
      return;
    }
  }

  bool accepted = false;
  bool setPremove = false;

  const auto st = m_chess_game.getGameState();
  const core::Color fromColor = getPieceConsideringPremoves(from).color;
  const bool humanTurnNow =
      (m_game_manager && m_game_manager->isHuman(st.sideToMove));
  const bool movingOwnTurnPiece = humanTurnNow && (fromColor == st.sideToMove);
  const core::Color humanNextColor = ~st.sideToMove;
  const bool humanNextIsHuman =
      (!m_game_manager || m_game_manager->isHuman(humanNextColor));

  if (from != to) {
    if (movingOwnTurnPiece && tryMove(from, to)) {
      if (m_game_manager) {
        accepted = m_game_manager->requestUserMove(from, to, /*onClick*/ false);
      }
    } else if (fromColor == humanNextColor && humanNextIsHuman) {
      // Drag-to-premove when it's not your turn
      setPremove = enqueuePremove(from, to);
    }
  }

  if (!accepted) {
    if (!setPremove) {
      m_game_view.setPieceToSquareScreenPos(from, from);

      // Warning snap if you're in check and tried an illegal drop
      if (m_chess_game.isKingInCheck(m_chess_game.getGameState().sideToMove) &&
          m_game_manager &&
          m_game_manager->isHuman(m_chess_game.getGameState().sideToMove) &&
          from != to && m_game_view.hasPieceOnSquare(from) &&
          m_chess_game.getPiece(from).color ==
              m_chess_game.getGameState().sideToMove) {
        m_game_view.warningKingSquareAnim(
            m_chess_game.getKingSquare(m_chess_game.getGameState().sideToMove));
        m_sound_manager.playEffect(view::sound::Effect::Warning);
      }

      m_game_view.animationSnapAndReturn(from, end);

      if (m_preview_active && isValid(m_prev_selected_before_preview) &&
          m_prev_selected_before_preview != from) {
        m_game_view.clearNonPremoveHighlights();
        m_selection_manager.highlightLastMove();
        m_selection_manager.selectSquare(m_prev_selected_before_preview);
        if (isHumanPiece(m_prev_selected_before_preview))
          showAttacks(getAttackSquares(m_prev_selected_before_preview));
      } else {
        m_game_view.clearNonPremoveHighlights();
        m_selection_manager.highlightLastMove();
        m_selection_manager.selectSquare(from);
        if (isHumanPiece(from))
          showAttacks(getAttackSquares(from));
      }
    } else {
      // For premove, don't snap back or reselect
      m_selection_manager.deselectSquare();
    }
  }

  // Always clear preview state
  m_preview_active = false;
  m_prev_selected_before_preview = core::NO_SQUARE;
}

/* -------------------- Helpers -------------------- */
bool GameController::isHumanPiece(core::Square sq) const {
  if (!isValid(sq))
    return false;
  auto pc = getPieceConsideringPremoves(sq);
  if (pc.type == core::PieceType::None)
    return false;
  return (!m_game_manager) ? true : m_game_manager->isHuman(pc.color);
}

bool GameController::hasCurrentLegalMove(core::Square from,
                                         core::Square to) const {
  if (!isValid(from) || !isValid(to))
    return false;
  const auto st = m_chess_game.getGameState();
  auto pc = m_chess_game.getPiece(from);
  if (pc.type == core::PieceType::None || pc.color != st.sideToMove)
    return false;

  ensureLegalCache();
  for (const auto &m : *m_cached_moves) {
    if (m.from() == from && m.to() == to)
      return true;
  }
  return false;
}

void GameController::invalidateLegalCache() { m_cached_moves = nullptr; }

void GameController::ensureLegalCache() const {
  if (!m_cached_moves)
    m_cached_moves = &m_chess_game.generateLegalMoves();
}

model::Position GameController::getPositionAfterPremoves() const {
  model::Position pos = m_chess_game.getPositionRefForBot();
  if (m_premove_queue.empty())
    return pos;
  for (const auto &pm : m_premove_queue) {
    auto moverOpt = pos.getBoard().getPiece(pm.from);
    if (!moverOpt)
      break;

    // Keep side to move stable so previews chain for the same color
    pos.getState().sideToMove = pm.moverColor;

    // Remove captured piece (including potential en-passant victim)
    if (pm.capturedType != core::PieceType::None) {
      if (pos.getBoard().getPiece(pm.to)) {
        pos.getBoard().removePiece(pm.to);
      } else if (moverOpt->type == core::PieceType::Pawn &&
                 ((static_cast<int>(pm.from) ^ static_cast<int>(pm.to)) & 7)) {
        // Diagonal pawn move onto empty square -> en-passant capture
        core::Square epSq = (moverOpt->color == core::Color::White)
                                ? static_cast<core::Square>(pm.to - 8)
                                : static_cast<core::Square>(pm.to + 8);
        pos.getBoard().removePiece(epSq);
      }
    }

    // Move the piece, ignoring normal legality
    model::bb::Piece moving = *moverOpt;
    pos.getBoard().removePiece(pm.from);
    if (pm.promotion != core::PieceType::None)
      moving.type = pm.promotion;
    pos.getBoard().setPiece(pm.to, moving);

    // Handle castling: move rook as well
    if (moving.type == core::PieceType::King &&
        std::abs(static_cast<int>(pm.to) - static_cast<int>(pm.from)) == 2) {
      core::Square rookFrom = (pm.to > pm.from)
                                  ? static_cast<core::Square>(pm.to + 1)
                                  : static_cast<core::Square>(pm.to - 2);
      core::Square rookTo = (pm.to > pm.from)
                                ? static_cast<core::Square>(pm.to - 1)
                                : static_cast<core::Square>(pm.to + 1);
      if (auto rook = pos.getBoard().getPiece(rookFrom)) {
        pos.getBoard().removePiece(rookFrom);
        pos.getBoard().setPiece(rookTo, *rook);
      }
    }
  }
  return pos;
}

model::bb::Piece
GameController::getPieceConsideringPremoves(core::Square sq) const {
  if (!isValid(sq))
    return {};
  // Prefer the virtual board after queued premoves (fixes "captured piece
  // steals selection")
  if (!m_premove_queue.empty()) {
    model::Position pos = getPositionAfterPremoves();
    if (auto virt = pos.getBoard().getPiece(sq))
      return *virt;
  }
  return m_chess_game.getPiece(sq);
}

bool GameController::hasVirtualPiece(core::Square sq) const {
  if (!isValid(sq))
    return false;
  return getPieceConsideringPremoves(sq).type != core::PieceType::None;
}

bool GameController::isPseudoLegalPremove(core::Square from,
                                          core::Square to) const {
  if (!isValid(from) || !isValid(to))
    return false;

  // Work from the virtual position AFTER already queued premoves
  model::Position pos = getPositionAfterPremoves();
  auto pcOpt = pos.getBoard().getPiece(from);
  if (!pcOpt)
    return false;
  const core::PieceType vType = pcOpt->type;
  const core::Color vCol = pcOpt->color;

  // Allow castling premove: king moves two squares toward own rook (standard)
  if (vType == core::PieceType::King &&
      std::abs(static_cast<int>(to) - static_cast<int>(from)) == 2) {
    core::Square rookSq = (to > from) ? static_cast<core::Square>(from + 3)
                                      : static_cast<core::Square>(from - 4);
    if (pos.getBoard().getPiece(rookSq) &&
        pos.getBoard().getPiece(rookSq)->color == vCol) {
      return true;
    }
  }

  // Safe premove generation: isolate the mover on an empty board, ignore
  // checks/castling/EP.
  model::Board board;
  board.clear();
  board.setPiece(from, {vType, vCol});

  if (vType == core::PieceType::Pawn) {
    // Add dummy diagonals so capture premoves are offerable even if empty
    const int file = static_cast<int>(from) & 7;
    const int forward = (vCol == core::Color::White) ? 8 : -8;
    const model::bb::Piece dummy{core::PieceType::Pawn, ~vCol};
    if (file > 0)
      board.setPiece(
          static_cast<core::Square>(static_cast<int>(from) + forward - 1),
          dummy);
    if (file < 7)
      board.setPiece(
          static_cast<core::Square>(static_cast<int>(from) + forward + 1),
          dummy);
  }

  model::GameState st{};
  st.sideToMove = vCol;
  st.castlingRights = 0;
  st.enPassantSquare = core::NO_SQUARE;

  m_pseudo_buffer.clear();
  m_movegen.generatePseudoLegalMoves(board, st, m_pseudo_buffer);

  for (const auto &m : m_pseudo_buffer)
    if (m.from() == from && m.to() == to)
      return true;

  return false;
}

void GameController::showGameOver(core::GameResult res,
                                  core::Color sideToMove) {
  // Reset any dragging state so no piece remains floating when the game ends.
  m_mouse_down = false;
  m_input_manager.cancelDrag();

  if (m_dragging) {
    if (m_drag_from != core::NO_SQUARE) {
      // Stop the placeholder animation and snap the piece back to its origin
      // square.
      m_game_view.endAnimation(m_drag_from);
      m_game_view.setPieceToSquareScreenPos(m_drag_from, m_drag_from);
    }
    m_game_view.clearDraggingPiece();
    m_dragging = false;
    m_drag_from = core::NO_SQUARE;
  }
  m_game_view.setDefaultCursor();

  // Ensure no premove state or visuals linger after the game ends
  m_premove_queue.clear();
  m_game_view.clearPremoveHighlights();
  m_game_view.clearPremovePieces(true);

  if (m_time_controller) {
    m_time_controller->stop();
    m_game_view.setClockActive(std::nullopt);
    if (!m_time_history.empty()) {
      const TimeView &tv = m_time_history.back();
      m_game_view.updateClock(core::Color::White, tv.white);
      m_game_view.updateClock(core::Color::Black, tv.black);
    }
  }

  m_sound_manager.playEffect(view::sound::Effect::GameEnds);
  std::string resultStr;
  core::Color winner = (sideToMove == core::Color::White) ? core::Color::Black
                                                          : core::Color::White;
  bool humanWinner = (winner == core::Color::White && !m_white_is_bot) ||
                     (winner == core::Color::Black && !m_black_is_bot);
  switch (res) {
  case core::GameResult::CHECKMATE:
    resultStr = (sideToMove == core::Color::White) ? "0-1" : "1-0";
    m_game_view.showGameOverPopup(
        sideToMove == core::Color::White ? "Black won" : "White won",
        humanWinner);
    break;
  case core::GameResult::TIMEOUT:
    resultStr = (sideToMove == core::Color::White) ? "0-1" : "1-0";
    m_game_view.showGameOverPopup(sideToMove == core::Color::White
                                      ? "Black wins on time"
                                      : "White wins on time",
                                  humanWinner);
    break;
  case core::GameResult::REPETITION:
    resultStr = "1/2-1/2";
    m_game_view.showGameOverPopup("Draw by repetition", false);
    break;
  case core::GameResult::MOVERULE:
    resultStr = "1/2-1/2";
    m_game_view.showGameOverPopup("Draw by 50 move rule", false);
    break;
  case core::GameResult::STALEMATE:
    resultStr = "1/2-1/2";
    m_game_view.showGameOverPopup("Stalemate", false);
    break;
  case core::GameResult::INSUFFICIENT:
    resultStr = "1/2-1/2";
    m_game_view.showGameOverPopup("Insufficient material", false);
    break;
  default:
    resultStr = "error";
    m_game_view.showGameOverPopup("result is not correct", false);
    break;
  }
  m_game_view.addResult(resultStr);
  m_game_view.setGameOver(true);
}

void GameController::syncCapturedPieces() {
  m_game_view.clearCapturedPieces();
  for (std::size_t i = 0; i < m_fen_index; ++i) {
    const MoveView &mv = m_move_history[i];
    if (mv.move.isCapture()) {
      m_game_view.addCapturedPiece(mv.moverColor, mv.capturedType);
    }
  }
}

void GameController::stashSelectedPiece() {
  m_stashed_selected_square = m_selection_manager.getSelectedSquare();
  if (m_stashed_selected_square != core::NO_SQUARE) {
    m_selection_manager.deselectSquare();
  }
}

void GameController::restoreSelectedPiece() {
  if (m_stashed_selected_square != core::NO_SQUARE &&
      isHumanPiece(m_stashed_selected_square)) {
    m_selection_manager.selectSquare(m_stashed_selected_square);
    showAttacks(getAttackSquares(m_stashed_selected_square));
  }
  m_stashed_selected_square = core::NO_SQUARE;
}

void GameController::stepBackward() {
  // Hide premove visuals when traversing history, but preserve the queue
  if (!m_premove_queue.empty() && m_fen_index == m_fen_history.size() - 1 &&
      !m_premove_suspended) {
    m_game_view.clearPremoveHighlights();
    m_game_view.clearPremovePieces(true);
    m_premove_suspended = true;
  }
  if (m_fen_index == m_fen_history.size() - 1) {
    stashSelectedPiece();
  }
  if (m_fen_index > 0) {
    const bool leavingFinalState =
        (m_chess_game.getResult() != core::GameResult::ONGOING &&
         m_fen_index == m_fen_history.size() - 1);

    if (m_fen_index == m_fen_history.size() - 1)
      m_game_view.stashRightClickHighlights();
    m_game_view.setBoardFen(m_fen_history[m_fen_index]);
    const MoveView &info = m_move_history[m_fen_index - 1];
    core::Square epVictim = core::NO_SQUARE;
    if (info.move.isEnPassant()) {
      epVictim = (info.moverColor == core::Color::White)
                     ? static_cast<core::Square>(info.move.to() - 8)
                     : static_cast<core::Square>(info.move.to() + 8);
    }
    m_game_view.animationMovePiece(
        info.move.to(), info.move.from(), core::NO_SQUARE,
        core::PieceType::None, [this, info, epVictim]() {
          if (info.move.isCapture()) {
            core::Square capSq =
                info.move.isEnPassant() ? epVictim : info.move.to();
            m_game_view.addPiece(info.capturedType, ~info.moverColor, capSq);
          }
          if (info.move.promotion() != core::PieceType::None) {
            m_game_view.removePiece(info.move.from());
            m_game_view.addPiece(core::PieceType::Pawn, info.moverColor,
                                 info.move.from());
          }
        });
    if (info.move.castle() != model::CastleSide::None) {
      const core::Square rookFrom = m_chess_game.getRookSquareFromCastleside(
          info.move.castle(), info.moverColor);
      const core::Square rookTo =
          (info.move.castle() == model::CastleSide::KingSide)
              ? static_cast<core::Square>(info.move.to() - 1)
              : static_cast<core::Square>(info.move.to() + 1);
      m_game_view.animationMovePiece(rookTo, rookFrom);
    }
    --m_fen_index;
    m_game_view.selectMove(m_fen_index ? m_fen_index - 1
                                       : static_cast<std::size_t>(-1));
    m_selection_manager.setLastMove(info.move.from(), info.move.to());
    m_game_view.clearAllHighlights();
    m_selection_manager.highlightLastMove();
    m_sound_manager.playEffect(info.sound);
    m_eval_cp.store(m_eval_history[m_fen_index]);
    if (leavingFinalState)
      m_game_view.resetEvalBar();
    m_game_view.updateEval(m_eval_history[m_fen_index]);
    m_game_view.updateFen(m_fen_history[m_fen_index]);
    if (m_fen_index < m_time_history.size()) {
      const TimeView &tv = m_time_history[m_fen_index];
      m_game_view.updateClock(core::Color::White, tv.white);
      m_game_view.updateClock(core::Color::Black, tv.black);
      bool latest = (m_fen_index == m_fen_history.size() - 1 &&
                     m_chess_game.getResult() == core::GameResult::ONGOING);
      if (latest)
        m_game_view.setClockActive(tv.active);
      else
        m_game_view.setClockActive(std::nullopt);
    }
    syncCapturedPieces();
    if (m_fen_index == m_fen_history.size() - 1) {
      m_game_view.restoreRightClickHighlights();
      restoreSelectedPiece();
    }
  }
}

void GameController::stepForward() {
  if (m_fen_index < m_move_history.size()) {
    const bool enteringFinalState =
        (m_chess_game.getResult() != core::GameResult::ONGOING &&
         m_fen_index + 1 == m_fen_history.size() - 1);

    m_game_view.setBoardFen(m_fen_history[m_fen_index]);
    const MoveView &info = m_move_history[m_fen_index];
    core::Square epVictim = core::NO_SQUARE;
    if (info.move.isEnPassant()) {
      epVictim = (info.moverColor == core::Color::White)
                     ? static_cast<core::Square>(info.move.to() - 8)
                     : static_cast<core::Square>(info.move.to() + 8);
      m_game_view.removePiece(epVictim);
    } else if (info.move.isCapture()) {
      m_game_view.removePiece(info.move.to());
    }
    if (info.move.castle() != model::CastleSide::None) {
      const core::Square rookFrom = m_chess_game.getRookSquareFromCastleside(
          info.move.castle(), info.moverColor);
      const core::Square rookTo =
          (info.move.castle() == model::CastleSide::KingSide)
              ? static_cast<core::Square>(info.move.to() - 1)
              : static_cast<core::Square>(info.move.to() + 1);
      m_game_view.animationMovePiece(rookFrom, rookTo);
    }

    // Defer premove visual restoration until this animation completes to avoid
    // racing.
    auto onMainMoveDone = [this]() {
      if (m_premove_suspended && m_fen_index == m_fen_history.size() - 1) {
        m_game_view.clearPremoveHighlights();
        for (const auto &pm : m_premove_queue) {
          m_game_view.highlightPremoveSquare(pm.from);
          m_game_view.highlightPremoveSquare(pm.to);
        }
        updatePremovePreviews(); // now model-driven, safe vs. animations
        m_premove_suspended = false;
      }
    };
    m_game_view.animationMovePiece(info.move.from(), info.move.to(), epVictim,
                                   info.move.promotion(), onMainMoveDone);

    ++m_fen_index;
    m_game_view.selectMove(m_fen_index ? m_fen_index - 1
                                       : static_cast<std::size_t>(-1));
    m_selection_manager.setLastMove(info.move.from(), info.move.to());
    m_game_view.clearAllHighlights();
    m_selection_manager.highlightLastMove();
    m_sound_manager.playEffect(info.sound);
    m_eval_cp.store(m_eval_history[m_fen_index]);
    m_game_view.updateEval(m_eval_history[m_fen_index]);
    if (enteringFinalState) {
      m_game_view.setEvalResult(resultToString(
          m_chess_game.getResult(), m_chess_game.getGameState().sideToMove));
    }
    m_game_view.updateFen(m_fen_history[m_fen_index]);
    if (m_fen_index < m_time_history.size()) {
      const TimeView &tv = m_time_history[m_fen_index];
      m_game_view.updateClock(core::Color::White, tv.white);
      m_game_view.updateClock(core::Color::Black, tv.black);
      bool latest = (m_fen_index == m_fen_history.size() - 1 &&
                     m_chess_game.getResult() == core::GameResult::ONGOING);
      if (latest)
        m_game_view.setClockActive(tv.active);
      else
        m_game_view.setClockActive(std::nullopt);
    }
    syncCapturedPieces();
    if (m_fen_index == m_fen_history.size() - 1)
      m_game_view.restoreRightClickHighlights();
  }

  // (Restoration of premove visuals when returning to head now happens
  //  in the animation completion callback above.)
}

void GameController::resign() {
  m_game_manager->stopGame();
  m_chess_game.setResult(core::GameResult::CHECKMATE);
  m_game_view.clearAllHighlights();
  m_selection_manager.highlightLastMove();
  core::Color loser = m_chess_game.getGameState().sideToMove;
  if (m_game_manager && !m_game_manager->isHuman(loser)) {
    loser = ~loser;
  }
  showGameOver(core::GameResult::CHECKMATE, loser);
}

GameController::NextAction GameController::getNextAction() const {
  return m_next_action;
}

} // namespace lilia::controller
