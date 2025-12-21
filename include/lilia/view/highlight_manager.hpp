#pragma once
#include <unordered_map>
#include <utility>
#include <vector>

#include "../chess_types.hpp"
#include "board_view.hpp"
#include "color_palette_manager.hpp"
#include "entity.hpp"

namespace lilia::view {

class HighlightManager {
 public:
  HighlightManager(const BoardView& boardRef);
  ~HighlightManager();

  void highlightSquare(core::Square pos);
  void highlightAttackSquare(core::Square pos);
  void highlightCaptureSquare(core::Square pos);
  void highlightHoverSquare(core::Square pos);
  void highlightPremoveSquare(core::Square pos);
  void highlightRightClickSquare(core::Square pos);
  void highlightRightClickArrow(core::Square from, core::Square to);
  [[nodiscard]] std::vector<core::Square> getRightClickSquares() const;
  [[nodiscard]] std::vector<std::pair<core::Square, core::Square>> getRightClickArrows() const;
  void clearAllHighlights();
  void clearNonPremoveHighlights();
  void clearAttackHighlights();
  void clearHighlightSquare(core::Square pos);
  void clearHighlightHoverSquare(core::Square pos);
  void clearHighlightPremoveSquare(core::Square pos);
  void clearPremoveHighlights();
  void clearRightClickHighlights();

  void renderAttack(sf::RenderWindow& window);
  void renderHover(sf::RenderWindow& window);
  void renderSelect(sf::RenderWindow& window);
  void renderPremove(sf::RenderWindow& window);
  void renderRightClickSquares(sf::RenderWindow& window);
  void renderRightClickArrows(sf::RenderWindow& window);

 private:
  void renderEntitiesToBoard(std::unordered_map<core::Square, Entity>& map,
                             sf::RenderWindow& window);
  void onPaletteChanged();

  const BoardView& m_board_view_ref;

  std::unordered_map<core::Square, Entity> m_hl_attack_squares;
  std::unordered_map<core::Square, bool> m_attack_is_capture;
  std::unordered_map<core::Square, Entity> m_hl_select_squares;
  std::unordered_map<core::Square, Entity> m_hl_hover_squares;
  std::unordered_map<core::Square, Entity> m_hl_premove_squares;
  std::unordered_map<core::Square, Entity> m_hl_rclick_squares;
  std::unordered_map<unsigned int, std::pair<core::Square, core::Square>> m_hl_rclick_arrows;
  ColorPaletteManager::ListenerID m_paletteListener{0};
};

}  // namespace lilia::view
