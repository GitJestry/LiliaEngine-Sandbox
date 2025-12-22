#include "lilia/controller/subsystems/board_input_system.hpp"

#include <SFML/Window/Mouse.hpp>

#include "lilia/controller/game_manager.hpp"
#include "lilia/controller/subsystems/attack_system.hpp"
#include "lilia/controller/subsystems/legal_move_cache.hpp"
#include "lilia/controller/subsystems/premove_system.hpp"
#include "lilia/model/chess_game.hpp"

namespace lilia::controller {

namespace {
inline bool valid(core::Square sq) {
  return sq != core::NO_SQUARE;
}
}  // namespace

BoardInputSystem::BoardInputSystem(view::GameView& view, model::ChessGame& game,
                                   InputManager& input, SelectionManager& sel,
                                   view::sound::SoundManager& sfx, AttackSystem& attacks,
                                   PremoveSystem& premove, LegalMoveCache& legal)
    : m_view(view),
      m_game(game),
      m_input(input),
      m_sel(sel),
      m_sfx(sfx),
      m_attacks(attacks),
      m_premove(premove),
      m_legal(legal) {}

void BoardInputSystem::bindInputCallbacks() {
  m_input.setOnClick([this](core::MousePos pos) { onClick(pos); });
  m_input.setOnDrag([this](core::MousePos s, core::MousePos c) { onDrag(s, c); });
  m_input.setOnDrop([this](core::MousePos s, core::MousePos e) { onDrop(s, e); });
}

void BoardInputSystem::onMouseMove(core::MousePos pos) {
  if (m_dragging || m_mouse_down) {
    m_view.setHandClosedCursor();
    return;
  }

  const core::Square sq = m_view.mousePosToSquare(pos);
  if (m_view.hasPieceOnSquare(sq) && !m_view.isInPromotionSelection())
    m_view.setHandOpenCursor();
  else
    m_view.setDefaultCursor();
}

void BoardInputSystem::onMousePressed(core::MousePos pos) {
  m_mouse_down = true;

  if (m_view.isInPromotionSelection()) {
    m_view.setHandClosedCursor();
    return;
  }

  const core::Square sq = m_view.mousePosToSquare(pos);
  m_selection_changed_on_press = false;

  if (!m_premove.hasVirtualPiece(sq)) {
    m_view.setDefaultCursor();
    m_view.clearRightClickHighlights();
    return;
  }

  const core::Square currentSelected = m_sel.getSelectedSquare();
  const bool selectionWasDifferent = (currentSelected != sq);

  if (currentSelected != core::NO_SQUARE && currentSelected != sq) {
    m_preview_active = true;
    m_prev_selected_before_preview = currentSelected;

    if (!tryMove(currentSelected, sq)) {
      m_view.clearNonPremoveHighlights();
      m_sel.highlightLastMove();
      m_sel.selectSquare(sq);
      m_sel.hoverSquare(sq);
      if (isHumanPiece(sq)) showAttacks(m_attacks.attacks(sq));
    }
  } else {
    m_preview_active = false;
    m_prev_selected_before_preview = core::NO_SQUARE;

    m_view.clearNonPremoveHighlights();
    m_sel.highlightLastMove();
    m_sel.selectSquare(sq);
    m_sel.hoverSquare(sq);
    if (isHumanPiece(sq)) showAttacks(m_attacks.attacks(sq));
  }

  if (!tryMove(currentSelected, sq)) {
    m_dragging = true;
    m_drag_from = sq;
    m_view.setPieceToMouseScreenPos(sq, pos);
    m_view.playPiecePlaceHolderAnimation(sq);
  }

  m_selection_changed_on_press = selectionWasDifferent && (m_sel.getSelectedSquare() == sq);
}

void BoardInputSystem::onMouseReleased(core::MousePos pos) {
  m_mouse_down = false;
  if (m_dragging) {
    m_dragging = false;
    m_drag_from = core::NO_SQUARE;
    m_view.clearDraggingPiece();
  }
  m_preview_active = false;
  m_prev_selected_before_preview = core::NO_SQUARE;
  onMouseMove(pos);
}

void BoardInputSystem::onRightPressed(core::MousePos pos) {
  m_premove.clearAll();
  m_view.clearAttackHighlights();
  m_sel.highlightLastMove();

  m_right_mouse_down = true;
  m_right_press_time = std::chrono::steady_clock::now();
  m_right_drag_from = m_view.mousePosToSquare(pos);
}

void BoardInputSystem::onRightReleased(core::MousePos pos) {
  if (!m_right_mouse_down) return;
  m_right_mouse_down = false;

  const core::Square endSq = m_view.mousePosToSquare(pos);
  const core::Square startSq = m_right_drag_from;
  m_right_drag_from = core::NO_SQUARE;

  if (!valid(startSq) || !valid(endSq)) return;

  const auto elapsed = std::chrono::steady_clock::now() - m_right_press_time;
  const bool heldLong =
      std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() > 200;

  if (startSq != endSq && heldLong) {
    m_view.highlightRightClickArrow(startSq, endSq);
  } else {
    m_view.highlightRightClickSquare(endSq);
  }
}

void BoardInputSystem::onMouseEntered() {
  m_mouse_down = sf::Mouse::isButtonPressed(sf::Mouse::Left);
  if (!m_dragging) return;

  const core::MousePos mp = m_view.getMousePosition();
  if (!m_mouse_down) {
    m_view.animationSnapAndReturn(m_drag_from, mp);
    m_dragging = false;
    m_drag_from = core::NO_SQUARE;
    m_view.clearDraggingPiece();
    m_input.cancelDrag();
    m_view.setDefaultCursor();
  } else {
    m_view.setPieceToMouseScreenPos(m_drag_from, mp);
  }
}

void BoardInputSystem::onLostFocus() {
  m_mouse_down = false;
  if (m_dragging) {
    const core::MousePos mp = m_view.getMousePosition();
    m_view.animationSnapAndReturn(m_drag_from, mp);
    m_dragging = false;
    m_drag_from = core::NO_SQUARE;
    m_view.clearDraggingPiece();
    m_input.cancelDrag();
  }
  m_view.setDefaultCursor();
}

bool BoardInputSystem::isHumanPiece(core::Square sq) const {
  if (!valid(sq)) return false;
  const auto pc = m_premove.pieceConsideringPremoves(sq);
  if (pc.type == core::PieceType::None) return false;
  return (!m_game_manager) ? true : m_game_manager->isHuman(pc.color);
}

bool BoardInputSystem::tryMove(core::Square a, core::Square b) const {
  if (!isHumanPiece(a)) return false;
  for (auto att : m_attacks.attacks(a))
    if (att == b) return true;
  return false;
}

void BoardInputSystem::showAttacks(const std::vector<core::Square>& att) {
  m_view.clearAttackHighlights();
  for (auto sq : att) {
    if (m_premove.hasVirtualPiece(sq))
      m_view.highlightCaptureSquare(sq);
    else
      m_view.highlightAttackSquare(sq);
  }
}

void BoardInputSystem::onClick(core::MousePos mousePos) {
  if (m_view.isOnFlipIcon(mousePos)) {
    m_view.toggleBoardOrientation();
    m_premove.onBoardFlipped();
    return;
  }

  const core::Square sq = m_view.mousePosToSquare(mousePos);
  if (!valid(sq)) {
    m_selection_changed_on_press = false;
    if (m_sel.getSelectedSquare() != core::NO_SQUARE) m_sel.deselectSquare();
    return;
  }

  if (m_view.hasPieceOnSquare(sq)) {
    m_view.endAnimation(sq);
    m_view.setPieceToSquareScreenPos(sq, sq);
  }

  if (m_selection_changed_on_press && sq == m_sel.getSelectedSquare()) {
    m_selection_changed_on_press = false;
    return;
  }
  m_selection_changed_on_press = false;

  if (m_view.isInPromotionSelection()) {
    const core::PieceType promoType = m_view.getSelectedPromotion(mousePos);
    m_view.removePromotionSelection();

    if (m_premove.isPendingPromotionSelection()) {
      m_premove.completePendingPromotion(promoType);
      m_sel.deselectSquare();
      return;
    }

    if (m_game_manager) m_game_manager->completePendingPromotion(promoType);
    m_sel.deselectSquare();
    return;
  }

  if (m_sel.getSelectedSquare() != core::NO_SQUARE) {
    const auto st = m_game.getGameState();
    const auto selPiece = m_premove.pieceConsideringPremoves(m_sel.getSelectedSquare());

    const bool ownTurnAndPiece =
        (selPiece.type != core::PieceType::None && st.sideToMove == selPiece.color &&
         (!m_game_manager || m_game_manager->isHuman(st.sideToMove)));

    const core::Color humanColor = ~st.sideToMove;
    const bool canPremove =
        (selPiece.type != core::PieceType::None && selPiece.color == humanColor &&
         (!m_game_manager || m_game_manager->isHuman(humanColor)));

    if (ownTurnAndPiece && tryMove(m_sel.getSelectedSquare(), sq)) {
      if (m_game_manager)
        (void)m_game_manager->requestUserMove(m_sel.getSelectedSquare(), sq, true);
      m_sel.deselectSquare();
      return;
    }

    if (!ownTurnAndPiece && canPremove) {
      if (sq == m_sel.getSelectedSquare()) {
        m_sel.deselectSquare();
      } else {
        m_premove.enqueue(m_sel.getSelectedSquare(), sq);
        m_sel.deselectSquare();
      }
      return;
    }

    if (m_premove.hasVirtualPiece(sq)) {
      if (sq == m_sel.getSelectedSquare()) {
        m_sel.deselectSquare();
      } else {
        m_view.clearNonPremoveHighlights();
        m_sel.highlightLastMove();
        m_sel.selectSquare(sq);
        if (isHumanPiece(sq)) showAttacks(m_attacks.attacks(sq));
      }
    } else {
      m_sel.deselectSquare();
    }
    return;
  }

  if (m_premove.hasVirtualPiece(sq)) {
    m_view.clearNonPremoveHighlights();
    m_sel.highlightLastMove();
    m_sel.selectSquare(sq);
    if (isHumanPiece(sq)) showAttacks(m_attacks.attacks(sq));
  }
}

void BoardInputSystem::onDrag(core::MousePos start, core::MousePos current) {
  const core::Square sqStart = m_view.mousePosToSquare(start);
  const core::MousePos clamped = m_view.clampPosToBoard(current);
  const core::Square sqMous = m_view.mousePosToSquare(clamped);

  if (m_view.isInPromotionSelection()) return;
  if (!m_premove.hasVirtualPiece(sqStart)) return;
  if (!m_dragging) return;

  if (m_sel.getSelectedSquare() != sqStart) {
    m_view.clearNonPremoveHighlights();
    m_sel.highlightLastMove();
    m_sel.selectSquare(sqStart);
    if (isHumanPiece(sqStart)) showAttacks(m_attacks.attacks(sqStart));
  }

  if (m_sel.getHoveredSquare() != sqMous) m_sel.dehoverSquare();
  m_sel.hoverSquare(sqMous);

  m_view.setPieceToMouseScreenPos(sqStart, current);
  m_view.playPiecePlaceHolderAnimation(sqStart);
}

void BoardInputSystem::onDrop(core::MousePos start, core::MousePos end) {
  const core::Square from = m_view.mousePosToSquare(start);
  const core::Square to = m_view.mousePosToSquare(m_view.clampPosToBoard(end));

  m_sel.dehoverSquare();

  if (m_view.isInPromotionSelection()) return;

  if (!m_premove.hasVirtualPiece(from)) {
    m_sel.deselectSquare();
    m_preview_active = false;
    m_prev_selected_before_preview = core::NO_SQUARE;
    return;
  }

  m_view.endAnimation(from);

  bool accepted = false;
  bool setPremove = false;

  const auto st = m_game.getGameState();
  const core::Color fromColor = m_premove.pieceConsideringPremoves(from).color;
  const bool humanTurnNow = (m_game_manager && m_game_manager->isHuman(st.sideToMove));
  const bool movingOwnTurnPiece = humanTurnNow && (fromColor == st.sideToMove);

  const core::Color humanNextColor = ~st.sideToMove;
  const bool humanNextIsHuman = (!m_game_manager || m_game_manager->isHuman(humanNextColor));

  if (from != to) {
    if (movingOwnTurnPiece && tryMove(from, to)) {
      if (m_game_manager) accepted = m_game_manager->requestUserMove(from, to, false);
    } else if (fromColor == humanNextColor && humanNextIsHuman) {
      setPremove = m_premove.enqueue(from, to);
    }
  }

  if (!accepted) {
    if (!setPremove) {
      m_view.setPieceToSquareScreenPos(from, from);
      m_view.animationSnapAndReturn(from, end);

      m_view.clearNonPremoveHighlights();
      m_sel.highlightLastMove();
      m_sel.selectSquare(m_preview_active && valid(m_prev_selected_before_preview) &&
                                 m_prev_selected_before_preview != from
                             ? m_prev_selected_before_preview
                             : from);
      if (isHumanPiece(m_sel.getSelectedSquare()))
        showAttacks(m_attacks.attacks(m_sel.getSelectedSquare()));
    } else {
      m_sel.deselectSquare();
    }
  }

  m_preview_active = false;
  m_prev_selected_before_preview = core::NO_SQUARE;
}

void BoardInputSystem::refreshActiveHighlights() {
  core::Square activeSq = core::NO_SQUARE;
  if (m_dragging && valid(m_drag_from))
    activeSq = m_drag_from;
  else if (valid(m_sel.getSelectedSquare()))
    activeSq = m_sel.getSelectedSquare();

  if (valid(activeSq) && isHumanPiece(activeSq)) showAttacks(m_attacks.attacks(activeSq));
}

}  // namespace lilia::controller
