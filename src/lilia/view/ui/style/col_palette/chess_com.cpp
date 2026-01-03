#include "lilia/view/ui/style/col_palette/chess_com.hpp"

namespace lilia::view
{

  // Chess.com — grün/beige Board, dunkle UI-Chrome
  const ColorPalette &chessComPalette()
  {
    static const ColorPalette palette = []
    {
      ColorPalette p{};

      // --- Board (deine Vorgaben) ---
      p.COL_BOARD_LIGHT = sf::Color(235, 236, 208);      // #EBECD0
      p.COL_BOARD_DARK = sf::Color(115, 149, 82);        // #739552
      p.COL_BOARD_OUTLINE = sf::Color(79, 109, 52, 140); // #4F6D34 with alpha

      // --- Accents & interactive ---
      p.COL_ACCENT = sf::Color(141, 174, 108);                // #8DAE6C (aus Dark-Square abgeleitet)
      p.COL_ACCENT_HOVER = sf::Color(155, 184, 123);          // #9BB87B
      p.COL_ACCENT_OUTLINE = sf::Color(141, 174, 108, 90);    // weicher Grün-Outline
      p.COL_SELECT_HIGHLIGHT = sf::Color(247, 247, 105, 170); // #F7F769 last-move gelb
      p.COL_PREMOVE_HIGHLIGHT = sf::Color(75, 144, 255, 160); // #4B90FF premove blau
      p.COL_WARNING_HIGHLIGHT = sf::Color(226, 87, 76, 200);  // #E2574C Warnung
      p.COL_RCLICK_HIGHLIGHT = sf::Color(141, 174, 108, 170); // grüner Ping
      p.COL_HOVER_OUTLINE = sf::Color(222, 222, 221, 110);    // #DEDEDD leichte Kante
      p.COL_MOVE_HIGHLIGHT = sf::Color(141, 174, 108, 48);    // dezente Grün-Wash
      p.COL_MARKER = sf::Color(141, 174, 108, 65);            // ruhiger Marker

      // --- Text (deine Vorgabe) ---
      p.COL_TEXT = sf::Color(222, 222, 221);       // #DEDEDD
      p.COL_MUTED_TEXT = sf::Color(185, 184, 182); // sanftes Grau
      p.COL_LIGHT_TEXT = sf::Color(245, 245, 244);
      p.COL_DARK_TEXT = sf::Color(18, 17, 15);

      // --- Evaluation bars (deine Vorgaben) ---
      p.COL_EVAL_WHITE = sf::Color(255, 255, 255); // #FFFFFF
      p.COL_EVAL_BLACK = sf::Color(64, 61, 57);    // #403D39

      // --- Panels & Chrome (auf Grundlage deiner Move-List/Background) ---
      p.COL_PANEL = sf::Color(48, 46, 43, 230);              // #302E2B mit Alpha
      p.COL_HEADER = sf::Color(38, 37, 34);                  // #262522
      p.COL_SIDEBAR_BG = sf::Color(33, 32, 30);              // #21201E
      p.COL_LIST_BG = sf::Color(38, 37, 34);                 // #262522
      p.COL_ROW_EVEN = sf::Color(38, 37, 34);                // #262522
      p.COL_ROW_ODD = sf::Color(33, 32, 30);                 // #21201E
      p.COL_HOVER_BG = sf::Color(58, 56, 52);                // #3A3834
      p.COL_SLOT_BASE = sf::Color(42, 41, 38);               // #2A2926
      p.COL_BUTTON = sf::Color(58, 58, 54);                  // #3A3A36
      p.COL_BUTTON_ACTIVE = sf::Color(92, 126, 74);          // #5C7E4A aktiver Grün-Ton
      p.COL_PANEL_TRANS = sf::Color(48, 46, 43, 150);        // #302E2B mit mehr Transparenz
      p.COL_PANEL_BORDER_ALT = sf::Color(222, 222, 221, 50); // #DEDEDD zarte Trennlinie

      // --- Backgrounds / Gradients (dunkel, an #302E2B orientiert) ---
      p.COL_LIGHT_BG = sf::Color(58, 56, 52);  // #3A3834
      p.COL_DARK_BG = sf::Color(26, 25, 23);   // #1A1917
      p.COL_BG_TOP = sf::Color(48, 46, 43);    // #302E2B
      p.COL_BG_BOTTOM = sf::Color(38, 37, 34); // #262522

      // --- Tooltip, Discs, Borders ---
      p.COL_TOOLTIP_BG = sf::Color(30, 29, 27, 230);     // #1E1D1B mit Alpha
      p.COL_DISC = sf::Color(58, 58, 54, 150);           // #3A3A36
      p.COL_DISC_HOVER = sf::Color(68, 68, 64, 180);     // #444440
      p.COL_BORDER = sf::Color(159, 168, 153, 60);       // #9FA899 grünliches Grau
      p.COL_BORDER_LIGHT = sf::Color(159, 168, 153, 50); // #9FA899
      p.COL_BORDER_BEVEL = sf::Color(159, 168, 153, 40); // #9FA899

      // --- Inputs ---
      p.COL_INPUT_BG = sf::Color(36, 35, 33);        // #242321
      p.COL_INPUT_BORDER = sf::Color(164, 164, 159); // #A4A49F

      // --- Time, Score, Misc ---
      p.COL_CLOCK_ACCENT = sf::Color(244, 244, 242);     // #F4F4F2
      p.COL_TIME_OFF = sf::Color(62, 93, 55);            // #3E5D37
      p.COL_SCORE_TEXT_DARK = sf::Color(20, 20, 18);     // #141412
      p.COL_SCORE_TEXT_LIGHT = sf::Color(237, 237, 235); // #EDEDEB
      p.COL_INVALID = sf::Color(200, 70, 70);            // semantisches Fehlerrot

      // --- Brand / Logo & Overlays ---
      p.COL_LOGO_BG = sf::Color(141, 174, 108, 70); // #8DAE6C Glow
      p.COL_TOP_HILIGHT = sf::Color(255, 255, 255, 18);
      p.COL_BOTTOM_SHADOW = sf::Color(0, 0, 0, 40);
      p.COL_PANEL_ALPHA220 = sf::Color(48, 46, 43, 220); // #302E2B

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
