#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <deque>
#include <vector>

#include "../../chess_types.hpp"
#include "../../model/chess_game.hpp"
#include "../../model/move.hpp"
#include "../../model/move_generator.hpp"
#include "../../model/position.hpp"
#include "../../view/audio/sound_manager.hpp"
#include "../../view/game_view.hpp"
#include "../game_controller_types.hpp"

namespace lilia::controller {
class GameManager;
class LegalMoveCache;

class PremoveSystem {
 public:
  PremoveSystem(view::GameView& view, model::ChessGame& game, view::sound::SoundManager& sfx,
                LegalMoveCache& legal);

  void setGameManager(GameManager* gm) { m_game_manager = gm; }

  bool enqueue(core::Square from, core::Square to);
  void clearAll();
  void suspendVisualsIfAtHead(bool atHead);
  void restoreVisualsIfNeeded(bool atHead);

  void onBoardFlipped() { updatePreviews(); }
  void updatePreviews();

  bool hasVirtualPiece(core::Square sq) const;
  model::bb::Piece pieceConsideringPremoves(core::Square sq) const;
  model::Position positionAfterPremoves() const;

  bool isPendingPromotionSelection() const { return m_pending_premove_promotion; }
  void beginPendingPromotion(core::Square from, core::Square to, core::PieceType capType,
                             core::Color capColor, core::Color moverColor);
  void completePendingPromotion(core::PieceType promoType);

  void scheduleFromQueueIfTurnMatches();
  void tickAutoMove();

  bool takeSkipAnimationFlag();
  core::PieceType takeCaptureOverride();

  bool hasQueuedPremoves() const { return !m_queue.empty(); }

 private:
  bool isPseudoLegal(core::Square from, core::Square to) const;
  bool currentLegal(core::Square from, core::Square to) const;
  void rebuildHighlights();

  view::GameView& m_view;
  model::ChessGame& m_game;
  view::sound::SoundManager& m_sfx;
  LegalMoveCache& m_legal;
  GameManager* m_game_manager{nullptr};

  static constexpr std::size_t kMaxPremoves = 200;

  std::deque<Premove> m_queue;
  bool m_visuals_suspended{false};

  bool m_pending_premove_promotion{false};
  core::Square m_pp_from{core::NO_SQUARE};
  core::Square m_pp_to{core::NO_SQUARE};
  core::PieceType m_pp_cap_type{core::PieceType::None};
  core::Color m_pp_cap_color{core::Color::White};
  core::Color m_pp_mover_color{core::Color::White};

  bool m_has_pending_auto_move{false};
  core::Square m_pending_from{core::NO_SQUARE};
  core::Square m_pending_to{core::NO_SQUARE};
  core::PieceType m_pending_capture_type{core::PieceType::None};
  core::PieceType m_pending_promotion{core::PieceType::None};

  bool m_skip_next_move_animation{false};

  mutable model::MoveGenerator m_movegen;
  mutable std::vector<model::Move> m_pseudo;
};

}  // namespace lilia::controller
