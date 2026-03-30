#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <deque>
#include <vector>

#include "lilia/app/view/audio/sound_manager.hpp"
#include "lilia/app/view/ui/screens/game_view.hpp"
#include "lilia/app/controller/game_controller_types.hpp"
#include "lilia/chess/position.hpp"
#include "lilia/chess/move_generator.hpp"

namespace lilia::chess
{
  class ChessGame;
} // namespace lilia::chess

namespace lilia::app::controller
{
  class GameManager;
  class LegalMoveCache;

  class PremoveSystem
  {
  public:
    PremoveSystem(view::ui::GameView &view, chess::ChessGame &game, view::audio::SoundManager &sfx,
                  LegalMoveCache &legal);

    void setGameManager(GameManager *gm) { m_game_manager = gm; }

    bool enqueue(chess::Square from, chess::Square to);
    void clearAll();
    void suspendVisualsIfAtHead(bool atHead);
    void restoreVisualsIfNeeded(bool atHead);

    void onBoardFlipped() { updatePreviews(); }
    void updatePreviews();

    bool hasVirtualPiece(chess::Square sq) const;
    chess::Piece pieceConsideringPremoves(chess::Square sq) const;
    chess::Position positionAfterPremoves() const;

    bool isPendingPromotionSelection() const { return m_pending_premove_promotion; }
    void beginPendingPromotion(chess::Square from, chess::Square to, chess::PieceType capType,
                               chess::Color capColor, chess::Color moverColor);
    void completePendingPromotion(chess::PieceType promoType);

    void scheduleFromQueueIfTurnMatches();
    void tickAutoMove();

    bool takeSkipAnimationFlag();
    chess::PieceType takeCaptureOverride();

    bool hasQueuedPremoves() const { return !m_queue.empty(); }

  private:
    bool isPseudoLegal(chess::Square from, chess::Square to) const;
    bool currentLegal(chess::Square from, chess::Square to) const;
    void rebuildHighlights();

    view::ui::GameView &m_view;
    chess::ChessGame &m_game;
    view::audio::SoundManager &m_sfx;
    LegalMoveCache &m_legal;
    GameManager *m_game_manager{nullptr};

    static constexpr std::size_t kMaxPremoves = 200;

    std::deque<Premove> m_queue;
    bool m_visuals_suspended{false};

    bool m_pending_premove_promotion{false};
    chess::Square m_pp_from{chess::NO_SQUARE};
    chess::Square m_pp_to{chess::NO_SQUARE};
    chess::PieceType m_pp_cap_type{chess::PieceType::None};
    chess::Color m_pp_cap_color{chess::Color::White};
    chess::Color m_pp_mover_color{chess::Color::White};

    bool m_has_pending_auto_move{false};
    chess::Square m_pending_from{chess::NO_SQUARE};
    chess::Square m_pending_to{chess::NO_SQUARE};
    chess::PieceType m_pending_capture_type{chess::PieceType::None};
    chess::PieceType m_pending_promotion{chess::PieceType::None};

    bool m_skip_next_move_animation{false};

    mutable chess::MoveGenerator m_movegen;
    mutable std::vector<chess::Move> m_pseudo;
  };

} // namespace lilia::controller
