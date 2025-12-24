#pragma once
#include <SFML/Graphics/Color.hpp>
#include <optional>

namespace lilia::view {

// Rose Noir — a burgundy/charcoal dark theme with warm ivory lights
// Idea: warm ivory board lights + burgundy board darks,
// charcoal UI, burgundy primary accent, rosy select, wine premove.
// NOTE: GOLD / WHITE_* / VALID / LOW_TIME remain semantic defaults unless you choose to theme-lock
// them.

// Macro listing all color palette entries with their default values
#define LILIA_COLOR_PALETTE(X)                                                           \
  X(COL_EVAL_WHITE, sf::Color(255, 252, 250))            /* #FFFCEA */                   \
  X(COL_EVAL_BLACK, sf::Color(30, 28, 32))               /* near-charcoal */             \
  X(COL_BOARD_LIGHT, sf::Color(248, 240, 242))           /* #F8F0F2 warm ivory */        \
  X(COL_BOARD_DARK, sf::Color(88, 52, 64))               /* #583440 burgundy-charcoal */ \
  X(COL_SELECT_HIGHLIGHT, sf::Color(200, 120, 135, 170)) /* rosy select */               \
  X(COL_PREMOVE_HIGHLIGHT, sf::Color(165, 88, 110, 160)) /* wine premove */              \
  X(COL_WARNING_HIGHLIGHT, sf::Color(200, 80, 95, 190))  /* warm warning */              \
  X(COL_RCLICK_HIGHLIGHT, sf::Color(145, 47, 64, 170))   /* burgundy ping */             \
  X(COL_HOVER_OUTLINE, sf::Color(255, 235, 240, 110))    /* warm ivory outline */        \
  X(COL_MARKER, sf::Color(145, 47, 64, 65))              /* quiet marker */              \
  X(COL_PANEL, sf::Color(24, 25, 30, 230))               /* deep charcoal */             \
  X(COL_HEADER, sf::Color(64, 67, 78))                   /* #40434E */                   \
  X(COL_SIDEBAR_BG, sf::Color(18, 19, 24))                                               \
  X(COL_LIST_BG, sf::Color(20, 22, 28))                                                  \
  X(COL_ROW_EVEN, sf::Color(24, 26, 32))                                                 \
  X(COL_ROW_ODD, sf::Color(22, 24, 30))                                                  \
  X(COL_HOVER_BG, sf::Color(38, 40, 48))                                                 \
  X(COL_TEXT, sf::Color(255, 252, 250))       /* #FFFCEA */                              \
  X(COL_MUTED_TEXT, sf::Color(200, 192, 200)) /* warm gray */                            \
  X(COL_ACCENT, sf::Color(145, 47, 64))       /* #912F40 */                              \
  X(COL_ACCENT_HOVER, sf::Color(170, 66, 83)) /* lighter burgundy */                     \
  X(COL_ACCENT_OUTLINE, sf::Color(145, 47, 64, 90))                                      \
  X(COL_SLOT_BASE, sf::Color(38, 36, 44))                                                \
  X(COL_DARK_TEXT, sf::Color(16, 14, 18))                                                \
  X(COL_LIGHT_TEXT, sf::Color(255, 250, 248))                                            \
  X(COL_LIGHT_BG, sf::Color(228, 220, 228))                                              \
  X(COL_DARK_BG, sf::Color(14, 12, 16))                                                  \
  X(COL_CLOCK_ACCENT, sf::Color(252, 245, 245))                                          \
  X(COL_TOOLTIP_BG, sf::Color(12, 10, 14, 230))                                          \
  X(COL_DISC, sf::Color(34, 36, 42, 150))                                                \
  X(COL_DISC_HOVER, sf::Color(40, 42, 50, 180))                                          \
  X(COL_BORDER, sf::Color(170, 150, 160, 60))                                            \
  X(COL_BORDER_LIGHT, sf::Color(170, 150, 160, 50))                                      \
  X(COL_BORDER_BEVEL, sf::Color(170, 150, 160, 40))                                      \
  X(COL_BOARD_OUTLINE, sf::Color(64, 67, 78, 120))                                       \
  X(COL_SHADOW_LIGHT, sf::Color(0, 0, 0, 60))                                            \
  X(COL_SHADOW_MEDIUM, sf::Color(0, 0, 0, 90))                                           \
  X(COL_SHADOW_STRONG, sf::Color(0, 0, 0, 140))                                          \
  X(COL_SHADOW_BAR, sf::Color(0, 0, 0, 70))                                              \
  X(COL_MOVE_HIGHLIGHT, sf::Color(145, 47, 64, 48))                                      \
  X(COL_OVERLAY_DIM, sf::Color(0, 0, 0, 100))                                            \
  X(COL_OVERLAY, sf::Color(0, 0, 0, 120))                                                \
  X(COL_GOLD, sf::Color(212, 175, 55))             /* semantic default */                \
  X(COL_WHITE_DIM, sf::Color(255, 255, 255, 70))   /* semantic default */                \
  X(COL_WHITE_FAINT, sf::Color(255, 255, 255, 30)) /* semantic default */                \
  X(COL_SCORE_TEXT_DARK, sf::Color(14, 12, 16))                                          \
  X(COL_SCORE_TEXT_LIGHT, sf::Color(250, 246, 244))                                      \
  X(COL_LOW_TIME, sf::Color(220, 70, 70)) /* semantic default */                         \
  X(COL_BG_TOP, sf::Color(12, 10, 12))                                                   \
  X(COL_BG_BOTTOM, sf::Color(8, 7, 5))                                                   \
  X(COL_PANEL_TRANS, sf::Color(24, 25, 30, 150))                                         \
  X(COL_PANEL_BORDER_ALT, sf::Color(200, 192, 200, 50))                                  \
  X(COL_BUTTON, sf::Color(40, 42, 50))                                                   \
  X(COL_BUTTON_ACTIVE, sf::Color(70, 72, 84))                                            \
  X(COL_TIME_OFF, sf::Color(112, 38, 50)) /* #702632 */                                  \
  X(COL_INPUT_BORDER, sf::Color(170, 150, 160))                                          \
  X(COL_INPUT_BG, sf::Color(28, 30, 38))                                                 \
  X(COL_VALID, sf::Color(122, 205, 164)) /* semantic default */                          \
  X(COL_INVALID, sf::Color(145, 47, 64))                                                 \
  X(COL_LOGO_BG, sf::Color(145, 47, 64, 70))                                             \
  X(COL_TOP_HILIGHT, sf::Color(255, 255, 255, 18))                                       \
  X(COL_BOTTOM_SHADOW, sf::Color(0, 0, 0, 40))                                           \
  X(COL_PANEL_ALPHA220, sf::Color(24, 25, 30, 220))

struct ColorPalette {
#define X(name, defaultValue) std::optional<sf::Color> name;
  LILIA_COLOR_PALETTE(X)
#undef X
};

struct PaletteColors {
#define X(name, defaultValue) sf::Color name;
  LILIA_COLOR_PALETTE(X)
#undef X
};

}  // namespace lilia::view
