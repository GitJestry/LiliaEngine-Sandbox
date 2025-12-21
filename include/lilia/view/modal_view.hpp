#pragma once
#include <math.h>

#include <SFML/Graphics.hpp>

#include "color_palette_manager.hpp"

namespace lilia::view {

// Minimal themed modal manager for GameView
class ModalView {
 public:
  ModalView();
  ~ModalView();

  // Must be called once (or let it succeed if already loaded)
  void loadFont(const std::string& fontPath);

  // Resign
  void showResign(const sf::Vector2u& windowSize, sf::Vector2f centerOnBoard);
  void hideResign();
  bool isResignOpen() const;

  // Game over
  void showGameOver(const std::string& msg, bool won, sf::Vector2f centerOnBoard);
  void hideGameOver();
  bool isGameOverOpen() const;

  // Layout on resize (re-center if needed)
  void onResize(const sf::Vector2u& windowSize, sf::Vector2f boardCenter);

  // The façade can render particles between these two calls:
  // drawOverlay() -> particles -> drawPanel()
  void drawOverlay(sf::RenderWindow& win) const;
  void drawPanel(sf::RenderWindow& win) const;

  // Hit tests
  bool hitResignYes(sf::Vector2f p) const;
  bool hitResignNo(sf::Vector2f p) const;
  bool hitNewBot(sf::Vector2f p) const;
  bool hitRematch(sf::Vector2f p) const;
  bool hitClose(sf::Vector2f p) const;

 private:
  // geometry
  sf::Vector2u m_windowSize{};
  sf::Vector2f m_boardCenter{};
  sf::RectangleShape m_btnClose;
  sf::Text m_lblClose;
  sf::FloatRect m_hitClose;

  // state
  bool m_openResign = false;
  bool m_openGameOver = false;

  // visuals
  sf::Font m_font;
  sf::RectangleShape m_panel;    // body
  sf::RectangleShape m_border;   // 1px hairline
  sf::RectangleShape m_overlay;  // screen dim

  // text
  sf::Text m_title;
  sf::Text m_msg;

  // trophy icon
  bool m_showTrophy = false;
  sf::ConvexShape m_trophyCup;
  sf::RectangleShape m_trophyStem;
  sf::RectangleShape m_trophyBase;
  sf::CircleShape m_trophyHandleL;
  sf::CircleShape m_trophyHandleR;

  // buttons (rectangles + labels)
  sf::RectangleShape m_btnLeft, m_btnRight;
  sf::Text m_lblLeft, m_lblRight;

  // cached hit areas
  sf::FloatRect m_hitLeft{}, m_hitRight{};

  // layout helpers
  void layoutCommon(sf::Vector2f center, sf::Vector2f panelSize);
  void layoutGameOverExtras();
  void stylePrimaryButton(sf::RectangleShape& btn, sf::Text& lbl);
  void styleSecondaryButton(sf::RectangleShape& btn, sf::Text& lbl);
  void applyTheme();
  ColorPaletteManager::ListenerID m_listener_id{0};
  static inline float snapf(float v) { return std::round(v); }
};

}  // namespace lilia::view
