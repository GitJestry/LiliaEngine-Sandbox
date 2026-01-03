#include "lilia/view/ui/style/col_palette/soft_pink.hpp"

namespace lilia::view
{

  // Soft Pink — blush board, rosy accents, mauve-tinted dark UI
  const ColorPalette &softPinkPalette()
  {
    static const ColorPalette palette = []
    {
      ColorPalette p{};

      // --- Board ---
      p.COL_BOARD_LIGHT = sf::Color(244, 233, 239);       // #F4E9EF (blush ivory)
      p.COL_BOARD_DARK = sf::Color(192, 138, 160);        // #C08AA0 (dusty rose)
      p.COL_BOARD_OUTLINE = sf::Color(142, 94, 115, 140); // #8E5E73 with alpha

      // --- Accents & interactive ---
      p.COL_ACCENT = sf::Color(212, 127, 161);                 // #D47FA1 primary rosy accent
      p.COL_ACCENT_HOVER = sf::Color(223, 148, 178);           // #DF94B2 lighter hover
      p.COL_ACCENT_OUTLINE = sf::Color(212, 127, 161, 90);     // soft rosy outline
      p.COL_SELECT_HIGHLIGHT = sf::Color(248, 234, 154, 170);  // #F8EA9A soft last-move
      p.COL_PREMOVE_HIGHLIGHT = sf::Color(138, 167, 255, 160); // #8AA7FF gentle premove blue
      p.COL_WARNING_HIGHLIGHT = sf::Color(226, 87, 76, 200);   // #E2574C warning
      p.COL_RCLICK_HIGHLIGHT = sf::Color(212, 127, 161, 170);  // rosy ping
      p.COL_HOVER_OUTLINE = sf::Color(246, 238, 242, 110);     // #F6EEF2 subtle outline
      p.COL_MOVE_HIGHLIGHT = sf::Color(212, 127, 161, 48);     // rosy wash
      p.COL_MARKER = sf::Color(212, 127, 161, 65);             // quiet marker

      // --- Text ---
      p.COL_TEXT = sf::Color(241, 239, 241);       // #F1EFF1
      p.COL_MUTED_TEXT = sf::Color(207, 200, 207); // #CFC8CF
      p.COL_LIGHT_TEXT = sf::Color(250, 247, 250); // #FAF7FA
      p.COL_DARK_TEXT = sf::Color(23, 19, 22);     // #171316

      // --- Evaluation bars ---
      p.COL_EVAL_WHITE = sf::Color(255, 255, 255); // #FFFFFF
      p.COL_EVAL_BLACK = sf::Color(62, 56, 60);    // #3E383C (pink-tinted near-black)

      // --- Panels & Chrome (mauve-tinted dark, chess.com-ähnliche Struktur) ---
      p.COL_PANEL = sf::Color(46, 42, 44, 230);              // #2E2A2C
      p.COL_HEADER = sf::Color(42, 38, 41);                  // #2A2629
      p.COL_SIDEBAR_BG = sf::Color(34, 31, 35);              // #221F23
      p.COL_LIST_BG = sf::Color(42, 38, 41);                 // #2A2629
      p.COL_ROW_EVEN = sf::Color(42, 38, 41);                // #2A2629
      p.COL_ROW_ODD = sf::Color(34, 31, 35);                 // #221F23
      p.COL_HOVER_BG = sf::Color(59, 53, 58);                // #3B353A
      p.COL_SLOT_BASE = sf::Color(52, 47, 51);               // #342F33
      p.COL_BUTTON = sf::Color(56, 51, 56);                  // #383338
      p.COL_BUTTON_ACTIVE = sf::Color(176, 108, 137);        // #B06C89 active rosy
      p.COL_PANEL_TRANS = sf::Color(46, 42, 44, 150);        // #2E2A2C (more transparent)
      p.COL_PANEL_BORDER_ALT = sf::Color(232, 221, 227, 50); // #E8DDE3 subtle divider

      // --- Backgrounds / gradients ---
      p.COL_LIGHT_BG = sf::Color(61, 56, 61);  // #3D383D
      p.COL_DARK_BG = sf::Color(26, 23, 26);   // #1A171A
      p.COL_BG_TOP = sf::Color(46, 42, 44);    // #2E2A2C
      p.COL_BG_BOTTOM = sf::Color(38, 35, 39); // #262327

      // --- Tooltip, discs, borders ---
      p.COL_TOOLTIP_BG = sf::Color(33, 30, 33, 230);     // #211E21
      p.COL_DISC = sf::Color(58, 52, 57, 150);           // #3A3439
      p.COL_DISC_HOVER = sf::Color(67, 61, 66, 180);     // #433D42
      p.COL_BORDER = sf::Color(200, 181, 190, 60);       // #C8B5BE pinkish gray
      p.COL_BORDER_LIGHT = sf::Color(200, 181, 190, 50); // #C8B5BE
      p.COL_BORDER_BEVEL = sf::Color(200, 181, 190, 40); // #C8B5BE

      // --- Inputs ---
      p.COL_INPUT_BG = sf::Color(39, 36, 40);        // #272428
      p.COL_INPUT_BORDER = sf::Color(191, 166, 178); // #BFA6B2

      // --- Time, Score, Misc ---
      p.COL_CLOCK_ACCENT = sf::Color(245, 241, 244);     // #F5F1F4
      p.COL_TIME_OFF = sf::Color(107, 59, 78);           // #6B3B4E subdued deep rose
      p.COL_SCORE_TEXT_DARK = sf::Color(20, 17, 20);     // #141114
      p.COL_SCORE_TEXT_LIGHT = sf::Color(239, 231, 236); // #EFE7EC
      p.COL_INVALID = sf::Color(208, 77, 126);           // #D04D7E semantic error

      // --- Brand / logo & overlays ---
      p.COL_LOGO_BG = sf::Color(212, 127, 161, 70); // #D47FA1 glow
      p.COL_TOP_HILIGHT = sf::Color(255, 255, 255, 18);
      p.COL_BOTTOM_SHADOW = sf::Color(0, 0, 0, 40);
      p.COL_PANEL_ALPHA220 = sf::Color(46, 42, 44, 220); // #2E2A2C

      // --- Shadows ---
      p.COL_SHADOW_LIGHT = sf::Color(0, 0, 0, 60);
      p.COL_SHADOW_MEDIUM = sf::Color(0, 0, 0, 90);
      p.COL_SHADOW_STRONG = sf::Color(0, 0, 0, 140);
      p.COL_SHADOW_BAR = sf::Color(0, 0, 0, 70);

      // --- Overlays ---
      p.COL_OVERLAY_DIM = sf::Color(0, 0, 0, 100);
      p.COL_OVERLAY = sf::Color(0, 0, 0, 120);

      // GOLD/WHITE_* und VALID bleiben auf Defaults, außer du willst sie fixen.
      return p;
    }();

    return palette;
  }

} // namespace lilia::view
