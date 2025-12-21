#pragma once

#include <atomic>
#include <chrono>
#include <deque>
#include <memory>
#include <string>
#include <utility>
#include <vector>

// Forward declaration to avoid heavy SFML header
namespace sf {
class Event;
}

// Project headers
#include "../chess_types.hpp"
#include "../constants.hpp"
#include "../model/move.hpp"
#include "../model/move_generator.hpp"
#include "../view/audio/sound_manager.hpp"
#include "../view/game_view.hpp"
#include "input_manager.hpp"
#include "selection_manager.hpp"
#include "time_controller.hpp"

namespace lilia::model {
class ChessGame;
struct Move;
class Position;
class MoveGenerator;
namespace bb {
struct Piece;
}
}  // namespace lilia::model

namespace lilia::controller {
class GameManager;

struct MoveView {
  model::Move move;
  core::Color moverColor;
  core::PieceType capturedType;
  view::sound::Effect sound;
  int evalCp{};
};

struct Premove {
  core::Square from;
  core::Square to;
  core::PieceType promotion = core::PieceType::None;
  core::PieceType capturedType;
  core::Color capturedColor;
  core::Color moverColor;
};

struct TimeView {
  float white;
  float black;
  core::Color active;
};

class GameController {
 public:
  explicit GameController(view::GameView& gView, model::ChessGame& game);
  ~GameController();

  void update(float dt);

  void handleEvent(const sf::Event& event);

  void render();

  /**
   * @brief Starts a game via the internal GameManager.
   * @param fen Starting FEN (default: START_FEN).
   * @param whiteIsBot true if the white player is a bot.
   * @param blackIsBot true if the black player is a bot.
   * @param whiteThinkTimeMs Time in milliseconds that the white bot is allowed
   *        to think at most.
   * @param whiteDepth Search depth for the white bot.
   * @param blackThinkTimeMs Time in milliseconds that the black bot is allowed
   *        to think at most.
   * @param blackDepth Search depth for the black bot.
   */
  void startGame(const std::string& fen = core::START_FEN, bool whiteIsBot = false,
                 bool blackIsBot = true, int whiteThinkTimeMs = 1000, int whiteDepth = 5,
                 int blackThinkTimeMs = 1000, int blackDepth = 5, bool useTimer = true,
                 int baseSeconds = 0, int incrementSeconds = 0);

  enum class NextAction { None, NewBot, Rematch };
  [[nodiscard]] NextAction getNextAction() const;

 private:
  bool isHumanPiece(core::Square sq) const;
  bool hasCurrentLegalMove(core::Square from, core::Square to) const;

  void onMouseMove(core::MousePos pos);
  void onMousePressed(core::MousePos pos);
  void onMouseReleased(core::MousePos pos);
  void onRightPressed(core::MousePos pos);
  void onRightReleased(core::MousePos pos);
  void onClick(core::MousePos mousePos);

  void onDrag(core::MousePos start, core::MousePos current);

  void onDrop(core::MousePos start, core::MousePos end);

  void clearPremove();
  bool enqueuePremove(core::Square from, core::Square to);
  void updatePremovePreviews();
  [[nodiscard]] bool isPseudoLegalPremove(core::Square from, core::Square to) const;
  [[nodiscard]] model::Position getPositionAfterPremoves() const;
  [[nodiscard]] model::bb::Piece getPieceConsideringPremoves(core::Square sq) const;
  [[nodiscard]] bool hasVirtualPiece(core::Square sq) const;

  void movePieceAndClear(const model::Move& move, bool isPlayerMove, bool onClick);

  void snapAndReturn(core::Square sq, core::MousePos cur);

  [[nodiscard]] const std::vector<core::Square>& getAttackSquares(core::Square pieceSQ) const;
  void showAttacks(const std::vector<core::Square>& att);
  [[nodiscard]] bool tryMove(core::Square a, core::Square b);
  [[nodiscard]] bool isPromotion(core::Square a, core::Square b);
  [[nodiscard]] bool isSameColor(core::Square a, core::Square b);
  void showGameOver(core::GameResult res, core::Color sideToMove);

  void stepBackward();
  void stepForward();
  void resign();

  void syncCapturedPieces();
  void stashSelectedPiece();
  void restoreSelectedPiece();

  // ---------------- Members ----------------
  view::GameView& m_game_view;                // Responsible for rendering.
  model::ChessGame& m_chess_game;             // Game model containing rules and state.
  InputManager m_input_manager;               // Handles raw input processing.
  view::sound::SoundManager m_sound_manager;  // Handles sfx and music

  bool m_white_is_bot{false};
  bool m_black_is_bot{false};

  core::Square m_promotion_square = core::NO_SQUARE;

  bool m_dragging = false;
  bool m_mouse_down = false;
  bool m_right_mouse_down = false;
  bool m_has_pending_auto_move = false;

  core::Square m_drag_from = core::NO_SQUARE;
  core::Square m_right_drag_from = core::NO_SQUARE;
  std::chrono::steady_clock::time_point m_right_press_time{};
  bool m_preview_active = false;
  core::Square m_prev_selected_before_preview = core::NO_SQUARE;
  bool m_selection_changed_on_press = false;
  core::Square m_stashed_selected_square = core::NO_SQUARE;

  std::deque<Premove> m_premove_queue;
  bool m_premove_suspended = false;  // Premove visuals hidden while browsing history
  // Temporary info while waiting for a premove promotion selection
  bool m_pending_premove_promotion = false;
  core::Square m_ppromo_from = core::NO_SQUARE;
  core::Square m_ppromo_to = core::NO_SQUARE;
  core::PieceType m_ppromo_captured_type = core::PieceType::None;
  core::Color m_ppromo_captured_color = core::Color::White;
  core::Color m_ppromo_mover_color = core::Color::White;
  core::Square m_pending_from = core::NO_SQUARE;
  core::Square m_pending_to = core::NO_SQUARE;
  core::PieceType m_pending_capture_type = core::PieceType::None;
  core::PieceType m_pending_promotion = core::PieceType::None;
  bool m_skip_next_move_animation = false;

  SelectionManager m_selection_manager;

  // ---------------- GameManager ----------------
  std::unique_ptr<GameManager> m_game_manager;
  std::unique_ptr<TimeController> m_time_controller;
  std::atomic<int> m_eval_cp{0};

  std::vector<std::string> m_fen_history;
  std::vector<int> m_eval_history;
  std::size_t m_fen_index{0};
  std::vector<MoveView> m_move_history;
  std::vector<TimeView> m_time_history;
  NextAction m_next_action{NextAction::None};

  mutable model::MoveGenerator m_movegen;
  mutable std::vector<model::Move> m_pseudo_buffer;
  mutable std::vector<core::Square> m_attack_buffer;
  mutable const std::vector<model::Move>* m_cached_moves{nullptr};

  // cached moves
  void invalidateLegalCache();
  void ensureLegalCache() const;
};

}  // namespace lilia::controller
