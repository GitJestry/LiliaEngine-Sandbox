#include "lilia/view/ui/style/col_palette/kintsugi_jade.hpp"

namespace lilia::view
{

  // Kintsugi Jade — jade/teal board, warm brass accents, charcoal UI
  const ColorPalette &kintsugiJadePalette()
  {
    static const ColorPalette palette = []
    {
      ColorPalette p{};

      // --- Board ---
      p.COL_BOARD_LIGHT = sf::Color(234, 243, 239);     // #EAF3EF soft jade ivory
      p.COL_BOARD_DARK = sf::Color(47, 106, 95);        // #2F6A5F verdigris teal
      p.COL_BOARD_OUTLINE = sf::Color(31, 62, 57, 140); // #1F3E39 with alpha

      // --- Accents & interactive ---
      p.COL_ACCENT = sf::Color(212, 166, 86);                 // #D4A656 brass
      p.COL_ACCENT_HOVER = sf::Color(224, 183, 105);          // #E0B769 hover brass
      p.COL_ACCENT_OUTLINE = sf::Color(212, 166, 86, 90);     // soft brass outline
      p.COL_SELECT_HIGHLIGHT = sf::Color(255, 210, 113, 170); // #FFD271 last-move amber
      p.COL_PREMOVE_HIGHLIGHT = sf::Color(91, 190, 245, 160); // #5BBEF5 premove blue
      p.COL_WARNING_HIGHLIGHT = sf::Color(224, 90, 90, 200);  // #E05A5A warning
      p.COL_RCLICK_HIGHLIGHT = sf::Color(212, 166, 86, 170);  // brass ping
      p.COL_HOVER_OUTLINE = sf::Color(232, 241, 236, 110);    // #E8F1EC subtle edge
      p.COL_MOVE_HIGHLIGHT = sf::Color(212, 166, 86, 48);     // gentle brass wash
      p.COL_MARKER = sf::Color(212, 166, 86, 65);             // quiet marker

      // --- Text ---
      p.COL_TEXT = sf::Color(245, 247, 246);       // #F5F7F6
      p.COL_MUTED_TEXT = sf::Color(191, 200, 196); // #BFC8C4
      p.COL_LIGHT_TEXT = sf::Color(251, 253, 252); // #FBFDFC
      p.COL_DARK_TEXT = sf::Color(19, 23, 22);     // #131716

      // --- Evaluation bars ---
      p.COL_EVAL_WHITE = sf::Color(255, 255, 255); // pure white
      p.COL_EVAL_BLACK = sf::Color(31, 36, 35);    // #1F2423 deep graphite

      // --- Panels & Chrome ---
      p.COL_PANEL = sf::Color(31, 36, 35, 230);              // #1F2423
      p.COL_HEADER = sf::Color(38, 48, 45);                  // #26302D
      p.COL_SIDEBAR_BG = sf::Color(23, 28, 27);              // #171C1B
      p.COL_LIST_BG = sf::Color(26, 33, 31);                 // #1A211F
      p.COL_ROW_EVEN = sf::Color(29, 36, 34);                // #1D2422
      p.COL_ROW_ODD = sf::Color(25, 32, 30);                 // #19201E
      p.COL_HOVER_BG = sf::Color(47, 58, 54);                // #2F3A36
      p.COL_SLOT_BASE = sf::Color(36, 44, 42);               // #242C2A
      p.COL_BUTTON = sf::Color(42, 52, 49);                  // #2A3431
      p.COL_BUTTON_ACTIVE = sf::Color(180, 137, 58);         // #B4893A pressed brass
      p.COL_PANEL_TRANS = sf::Color(31, 36, 35, 150);        // #1F2423 (more transparent)
      p.COL_PANEL_BORDER_ALT = sf::Color(201, 211, 207, 50); // #C9D3CF subtle divider

      // --- Backgrounds / gradients ---
      p.COL_LIGHT_BG = sf::Color(43, 51, 49);  // #2B3331
      p.COL_DARK_BG = sf::Color(14, 18, 17);   // #0E1211
      p.COL_BG_TOP = sf::Color(22, 27, 26);    // #161B1A
      p.COL_BG_BOTTOM = sf::Color(14, 18, 17); // #0E1211

      // --- Tooltip, discs, borders ---
      p.COL_TOOLTIP_BG = sf::Color(18, 23, 22, 230);     // #121716
      p.COL_DISC = sf::Color(44, 53, 50, 150);           // #2C3532
      p.COL_DISC_HOVER = sf::Color(51, 64, 61, 180);     // #33403D
      p.COL_BORDER = sf::Color(166, 177, 173, 60);       // #A6B1AD
      p.COL_BORDER_LIGHT = sf::Color(166, 177, 173, 50); // #A6B1AD
      p.COL_BORDER_BEVEL = sf::Color(166, 177, 173, 40); // #A6B1AD

      // --- Inputs ---
      p.COL_INPUT_BG = sf::Color(26, 32, 31);        // #1A201F
      p.COL_INPUT_BORDER = sf::Color(143, 163, 158); // #8FA39E

      // --- Time, Score, Misc ---
      p.COL_CLOCK_ACCENT = sf::Color(243, 240, 233);     // #F3F0E9 warm light
      p.COL_TIME_OFF = sf::Color(138, 61, 61);           // #8A3D3D subdued red
      p.COL_SCORE_TEXT_DARK = sf::Color(15, 19, 18);     // #0F1312
      p.COL_SCORE_TEXT_LIGHT = sf::Color(240, 245, 243); // #F0F5F3
      p.COL_INVALID = sf::Color(204, 74, 74);            // #CC4A4A error

      // --- Brand / logo & overlays ---
      p.COL_LOGO_BG = sf::Color(212, 166, 86, 70); // #D4A656 glow
      p.COL_TOP_HILIGHT = sf::Color(255, 255, 255, 18);
      p.COL_BOTTOM_SHADOW = sf::Color(0, 0, 0, 40);
      p.COL_PANEL_ALPHA220 = sf::Color(31, 36, 35, 220); // #1F2423

      // --- Shadows ---
      p.COL_SHADOW_LIGHT = sf::Color(0, 0, 0, 60);
      p.COL_SHADOW_MEDIUM = sf::Color(0, 0, 0, 90);
      p.COL_SHADOW_STRONG = sf::Color(0, 0, 0, 140);
      p.COL_SHADOW_BAR = sf::Color(0, 0, 0, 70);

      // --- Overlays ---
      p.COL_OVERLAY_DIM = sf::Color(0, 0, 0, 100);
      p.COL_OVERLAY = sf::Color(0, 0, 0, 120);

      return p;
    }();

    return palette;
  }

} // namespace lilia::view
