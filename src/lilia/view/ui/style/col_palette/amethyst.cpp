#include "lilia/view/ui/style/col_palette/amethyst.hpp"

namespace lilia::view
{

  // Amethyst Lotus — a creative, high-contrast violet theme
  // lavender light squares + indigo-slate dark squares, deep charcoal UI,
  // amethyst primary accent, warm amber last-move, cool cyan premove, mint VALID.
  const ColorPalette &amethystPalette()
  {
    static const ColorPalette palette = []
    {
      ColorPalette p{};

      // Board
      p.COL_BOARD_LIGHT = sf::Color(245, 240, 252);      // #F5F0FC lavender
      p.COL_BOARD_DARK = sf::Color(70, 79, 130);         // #464F82 indigo-slate
      p.COL_BOARD_OUTLINE = sf::Color(94, 72, 158, 120); // #5E489E with alpha

      // Accents & interactive
      p.COL_ACCENT = sf::Color(162, 117, 255);                 // #A273FF amethyst
      p.COL_ACCENT_HOVER = sf::Color(187, 153, 255);           // #BB99FF
      p.COL_ACCENT_OUTLINE = sf::Color(162, 117, 255, 90);     // amethyst outline
      p.COL_SELECT_HIGHLIGHT = sf::Color(255, 211, 110, 170);  // #FFD36E amber
      p.COL_PREMOVE_HIGHLIGHT = sf::Color(96, 196, 255, 160);  // #60C4FF cyan
      p.COL_WARNING_HIGHLIGHT = sf::Color(255, 122, 136, 190); // #FF7A88
      p.COL_RCLICK_HIGHLIGHT = sf::Color(162, 117, 255, 170);  // amethyst ping
      p.COL_HOVER_OUTLINE = sf::Color(238, 233, 255, 110);     // soft glow
      p.COL_MOVE_HIGHLIGHT = sf::Color(162, 117, 255, 48);     // subtle wash
      p.COL_MARKER = sf::Color(162, 117, 255, 65);             // subtle dot

      // Text
      p.COL_TEXT = sf::Color(242, 240, 252);       // #F2F0FC
      p.COL_MUTED_TEXT = sf::Color(194, 192, 212); // #C2C0D4
      p.COL_LIGHT_TEXT = sf::Color(250, 248, 255); // #FAF8FF
      p.COL_DARK_TEXT = sf::Color(23, 19, 30);     // #17131E

      // Evaluation bars
      p.COL_EVAL_WHITE = sf::Color(255, 255, 255); // crisp
      p.COL_EVAL_BLACK = sf::Color(49, 42, 61);    // eggplant

      // Panels & chrome
      p.COL_PANEL = sf::Color(26, 23, 34, 230);       // #1A1722 deep charcoal-violet
      p.COL_HEADER = sf::Color(40, 34, 54);           // #282236
      p.COL_SIDEBAR_BG = sf::Color(20, 18, 28);       // #14121C
      p.COL_LIST_BG = sf::Color(24, 22, 33);          // #181621
      p.COL_ROW_EVEN = sf::Color(31, 28, 41);         // #1F1C29
      p.COL_ROW_ODD = sf::Color(27, 24, 36);          // #1B1824
      p.COL_HOVER_BG = sf::Color(55, 50, 74);         // #37324A
      p.COL_SLOT_BASE = sf::Color(44, 39, 56);        // #2C2738
      p.COL_BUTTON = sf::Color(54, 46, 74);           // #362E4A
      p.COL_BUTTON_ACTIVE = sf::Color(127, 106, 210); // #7F6AD2
      p.COL_PANEL_TRANS = sf::Color(26, 23, 34, 150);
      p.COL_PANEL_BORDER_ALT = sf::Color(200, 194, 224, 50); // #C8C2E0
      p.COL_PANEL_ALPHA220 = sf::Color(26, 23, 34, 220);

      // Backgrounds / gradients
      p.COL_LIGHT_BG = sf::Color(224, 218, 240); // #E0DAF0
      p.COL_DARK_BG = sf::Color(18, 16, 24);     // #121018
      p.COL_BG_TOP = sf::Color(28, 24, 35);      // #1C1823
      p.COL_BG_BOTTOM = sf::Color(16, 13, 22);   // #100D16

      // Tooltip, discs, borders
      p.COL_TOOLTIP_BG = sf::Color(22, 19, 30, 230); // #16131E
      p.COL_DISC = sf::Color(58, 49, 74, 150);
      p.COL_DISC_HOVER = sf::Color(67, 56, 90, 180);
      p.COL_BORDER = sf::Color(156, 162, 204, 60); // #9CA2CC periwinkle steel
      p.COL_BORDER_LIGHT = sf::Color(156, 162, 204, 50);
      p.COL_BORDER_BEVEL = sf::Color(156, 162, 204, 40);

      // Inputs
      p.COL_INPUT_BG = sf::Color(42, 35, 56);        // #2A2338
      p.COL_INPUT_BORDER = sf::Color(186, 178, 220); // #BAB2DC

      // Time, score, misc
      p.COL_CLOCK_ACCENT = sf::Color(245, 242, 255);     // #F5F2FF
      p.COL_TIME_OFF = sf::Color(112, 80, 168);          // #7050A8
      p.COL_SCORE_TEXT_DARK = sf::Color(20, 18, 28);     // #14121C
      p.COL_SCORE_TEXT_LIGHT = sf::Color(236, 232, 250); // #ECE8FA
      p.COL_LOW_TIME = sf::Color(220, 70, 70);
      p.COL_VALID = sf::Color(122, 205, 164);   // mint OK
      p.COL_INVALID = sf::Color(217, 106, 134); // rose error

      // Brand / logo & overlays
      p.COL_LOGO_BG = sf::Color(162, 117, 255, 70);
      p.COL_GOLD = sf::Color(212, 175, 55);
      p.COL_WHITE_DIM = sf::Color(255, 255, 255, 70);
      p.COL_WHITE_FAINT = sf::Color(255, 255, 255, 30);
      p.COL_TOP_HILIGHT = sf::Color(255, 255, 255, 18);
      p.COL_BOTTOM_SHADOW = sf::Color(0, 0, 0, 40);

      // Shadows
      p.COL_SHADOW_LIGHT = sf::Color(0, 0, 0, 60);
      p.COL_SHADOW_MEDIUM = sf::Color(0, 0, 0, 90);
      p.COL_SHADOW_STRONG = sf::Color(0, 0, 0, 140);
      p.COL_SHADOW_BAR = sf::Color(0, 0, 0, 70);

      // Overlays
      p.COL_OVERLAY_DIM = sf::Color(0, 0, 0, 100);
      p.COL_OVERLAY = sf::Color(0, 0, 0, 120);

      return p;
    }();

    return palette;
  }

} // namespace lilia::view
