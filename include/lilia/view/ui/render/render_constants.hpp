#pragma once

#include <array>
#include <cstdint>
#include <string_view>

namespace lilia::view::constant
{

  // ------------------ Board / Window Metrics ------------------
  inline constexpr std::uint32_t BOARD_SIZE = 8;
  inline constexpr std::uint32_t WINDOW_PX_SIZE = 800;

  static_assert(WINDOW_PX_SIZE % BOARD_SIZE == 0, "WINDOW_PX_SIZE must be divisible by BOARD_SIZE.");
  inline constexpr std::uint32_t SQUARE_PX_SIZE = WINDOW_PX_SIZE / BOARD_SIZE;

  // integer rounding helpers (percent of square)
  inline constexpr std::uint32_t percentRound(std::uint32_t v, std::uint32_t pct)
  {
    // round(v * pct / 100)
    return (v * pct + 50u) / 100u;
  }

  inline constexpr std::uint32_t ATTACK_DOT_PX_SIZE = percentRound(SQUARE_PX_SIZE, 45);
  inline constexpr std::uint32_t CAPTURE_CIRCLE_PX_SIZE = percentRound(SQUARE_PX_SIZE, 102);

  inline constexpr std::uint32_t EVAL_BAR_HEIGHT = WINDOW_PX_SIZE;
  inline constexpr std::uint32_t EVAL_BAR_WIDTH = percentRound(WINDOW_PX_SIZE, 5);
  inline constexpr std::uint32_t EVAL_BAR_FONT_SIZE = 14;

  inline constexpr std::uint32_t MOVE_LIST_WIDTH = percentRound(WINDOW_PX_SIZE, 35);
  inline constexpr std::uint32_t SIDE_MARGIN = SQUARE_PX_SIZE / 2;

  inline constexpr std::uint32_t WINDOW_TOTAL_WIDTH =
      EVAL_BAR_WIDTH + SIDE_MARGIN + WINDOW_PX_SIZE + SIDE_MARGIN + MOVE_LIST_WIDTH + SIDE_MARGIN;
  inline constexpr std::uint32_t WINDOW_TOTAL_HEIGHT = WINDOW_PX_SIZE + SIDE_MARGIN * 2;

  inline constexpr std::uint32_t HOVER_PX_SIZE = SQUARE_PX_SIZE;

  // ------------------ Anim ------------------
  inline constexpr float ANIM_SNAP_SPEED = 0.005f;
  inline constexpr float ANIM_MOVE_SPEED = 0.05f;
  inline constexpr float PIECE_SCALE = 1.6f;

  // ------------------ Palette Names (no manager include) ------------------
  namespace palette_name
  {
    inline constexpr std::string_view DEFAULT{"Default"};
    inline constexpr std::string_view AMETHYST{"Amethyst"};
    inline constexpr std::string_view GREEN_IVORY{"Chess.com"};
    inline constexpr std::string_view SOFT_PINK{"Soft Pink"};
    inline constexpr std::string_view KINTSUGI{"Kintsugi Jade"};

    inline constexpr std::array<std::string_view, 5> all{DEFAULT, AMETHYST, GREEN_IVORY, SOFT_PINK,
                                                         KINTSUGI};
  } // namespace palette_name

  // ------------------ Texture Keys ------------------
  namespace tex
  {
    inline constexpr std::string_view WHITE{"white"};
    inline constexpr std::string_view BLACK{"black"};
    inline constexpr std::string_view EVAL_WHITE{"evalwhite"};
    inline constexpr std::string_view EVAL_BLACK{"evalblack"};

    inline constexpr std::string_view PROMOTION{"promotion"};
    inline constexpr std::string_view PROMOTION_SHADOW{"promotionShadow"};

    inline constexpr std::string_view TRANSPARENT{"transparent"};
    inline constexpr std::string_view SELECT_HL{"selectHighlight"};
    inline constexpr std::string_view ATTACK_HL{"attackHighlight"};
    inline constexpr std::string_view CAPTURE_HL{"captureHighlight"};
    inline constexpr std::string_view HOVER_HL{"hoverHighlight"};
    inline constexpr std::string_view PREMOVE_HL{"premoveHighlight"};
    inline constexpr std::string_view WARNING_HL{"warningHighlight"};
    inline constexpr std::string_view RCLICK_HL{"rightClickHighlight"};
  } // namespace tex

  // ------------------ Asset Paths ------------------
  namespace path
  {
    inline constexpr std::string_view HAND_OPEN{"assets/icons/cursor_hand_open.png"};
    inline constexpr std::string_view HAND_CLOSED{"assets/icons/cursor_hand_closed.png"};
    inline constexpr std::string_view FONT{"assets/font/OpenSans-Regular.ttf"};
    inline constexpr std::string_view ICON_LILIA{"assets/icons/lilia.png"};
    inline constexpr std::string_view ICON_LILIA_START{"assets/icons/lilia_transparent.png"};
    inline constexpr std::string_view ICON_CHALLENGER{"assets/icons/challenger.png"};
    inline constexpr std::string_view ICON_RESIGN{"assets/icons/resign.png"};
    inline constexpr std::string_view ICON_PREV{"assets/icons/prev.png"};
    inline constexpr std::string_view ICON_NEXT{"assets/icons/next.png"};
    inline constexpr std::string_view ICON_SETTINGS{"assets/icons/settings.png"};
    inline constexpr std::string_view ICON_NEW_BOT{"assets/icons/new_bot.png"};
    inline constexpr std::string_view ICON_REMATCH{"assets/icons/rematch.png"};

    inline constexpr std::string_view PIECES_DIR{"assets/textures"};
    inline constexpr std::string_view SFX_DIR{"assets/audio/sfx"};
  } // namespace path

  // ------------------ SFX Keys ------------------
  namespace sfx
  {
    inline constexpr std::string_view PLAYER_MOVE{"player_move"};
    inline constexpr std::string_view ENEMY_MOVE{"enemy_move"};
    inline constexpr std::string_view WARNING{"warning"};
    inline constexpr std::string_view CAPTURE{"capture"};
    inline constexpr std::string_view CASTLE{"castle"};
    inline constexpr std::string_view CHECK{"check"};
    inline constexpr std::string_view PROMOTION{"promotion"};
    inline constexpr std::string_view GAME_BEGINS{"game_begins"};
    inline constexpr std::string_view GAME_ENDS{"game_ends"};
    inline constexpr std::string_view PREMOVE{"pre_move"};
  } // namespace sfx

} // namespace lilia::view::constant
