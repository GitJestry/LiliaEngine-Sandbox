#pragma once
#include <SFML/Graphics/Color.hpp>
#include <string_view>

#include "../constants.hpp"
#include "color_palette_manager.hpp"

namespace lilia::view::constant {
constexpr unsigned int BOARD_SIZE = 8;
constexpr unsigned int WINDOW_PX_SIZE = 800;
constexpr unsigned int SQUARE_PX_SIZE = WINDOW_PX_SIZE / BOARD_SIZE;
constexpr unsigned int ATTACK_DOT_PX_SIZE =
    static_cast<unsigned int>(static_cast<float>(SQUARE_PX_SIZE) * 0.45f + 0.5f);
constexpr unsigned int CAPTURE_CIRCLE_PX_SIZE =
    static_cast<unsigned int>(static_cast<float>(SQUARE_PX_SIZE) * 1.02f + 0.5f);

constexpr unsigned int EVAL_BAR_HEIGHT = WINDOW_PX_SIZE;
constexpr unsigned int EVAL_BAR_WIDTH =
    static_cast<unsigned int>(static_cast<float>(WINDOW_PX_SIZE) * 0.05f);
constexpr unsigned int EVAL_BAR_FONT_SIZE = 14;

constexpr unsigned int MOVE_LIST_WIDTH =
    static_cast<unsigned int>(static_cast<float>(WINDOW_PX_SIZE) * 0.35f);

constexpr unsigned int SIDE_MARGIN =
    static_cast<unsigned int>(static_cast<float>(SQUARE_PX_SIZE) * 0.5f);

constexpr unsigned int WINDOW_TOTAL_WIDTH =
    EVAL_BAR_WIDTH + SIDE_MARGIN + WINDOW_PX_SIZE + SIDE_MARGIN + MOVE_LIST_WIDTH + SIDE_MARGIN;
constexpr unsigned int WINDOW_TOTAL_HEIGHT = WINDOW_PX_SIZE + SIDE_MARGIN * 2;

constexpr unsigned int HOVER_PX_SIZE = SQUARE_PX_SIZE;

constexpr float ANIM_SNAP_SPEED = .005f;
constexpr float ANIM_MOVE_SPEED = .05f;

// ------------------ Color palette ------------------
#define X(name, defaultValue) inline sf::Color& name = ColorPaletteManager::get().palette().name;
LILIA_COLOR_PALETTE(X)
#undef X
inline constexpr std::string_view STR_COL_PALETTE_DEFAULT{"Default"};
inline constexpr std::string_view STR_COL_PALETTE_Amethyst{"Amethyst"};
inline constexpr std::string_view STR_COL_PALETTE_GREEN_IVORY{"Chess.com"};
inline constexpr std::string_view STR_COL_PALETTE_SOFT_PINK{"Soft Pink"};
inline constexpr std::string_view STR_COL_PALETTE_KINTSUGI{"Kintsugi Jade"};

const std::string STR_TEXTURE_WHITE = "white";
const std::string STR_TEXTURE_BLACK = "black";
const std::string STR_TEXTURE_EVAL_WHITE = "evalwhite";
const std::string STR_TEXTURE_EVAL_BLACK = "evalblack";

const std::string STR_TEXTURE_PROMOTION = "promotion";
const std::string STR_TEXTURE_PROMOTION_SHADOW = "promotionShadow";

const std::string STR_TEXTURE_TRANSPARENT = "transparent";
const std::string STR_TEXTURE_SELECTHLIGHT = "selectHighlight";
const std::string STR_TEXTURE_ATTACKHLIGHT = "attackHighlight";
const std::string STR_TEXTURE_CAPTUREHLIGHT = "captureHighlight";
const std::string STR_TEXTURE_HOVERHLIGHT = "hoverHighlight";
const std::string STR_TEXTURE_PREMOVEHLIGHT = "premoveHighlight";
const std::string STR_TEXTURE_WARNINGHLIGHT = "warningHighlight";
const std::string STR_TEXTURE_RCLICKHLIGHT = "rightClickHighlight";

const std::string STR_FILE_PATH_HAND_OPEN = "assets/icons/cursor_hand_open.png";
const std::string STR_FILE_PATH_HAND_CLOSED = "assets/icons/cursor_hand_closed.png";
const std::string STR_FILE_PATH_FONT = "assets/font/OpenSans-Regular.ttf";
const std::string STR_FILE_PATH_ICON_LILIA = "assets/icons/lilia.png";
const std::string STR_FILE_PATH_ICON_LILIA_START_SCREEN = "assets/icons/lilia_transparent.png";
const std::string STR_FILE_PATH_ICON_CHALLENGER = "assets/icons/challenger.png";
const std::string STR_FILE_PATH_ICON_RESIGN = "assets/icons/resign.png";
const std::string STR_FILE_PATH_ICON_PREV = "assets/icons/prev.png";
const std::string STR_FILE_PATH_ICON_NEXT = "assets/icons/next.png";
const std::string STR_FILE_PATH_ICON_SETTINGS = "assets/icons/settings.png";
const std::string STR_FILE_PATH_ICON_NEW_BOT = "assets/icons/new_bot.png";
const std::string STR_FILE_PATH_ICON_REMATCH = "assets/icons/rematch.png";

const std::string ASSET_PIECES_FILE_PATH = "assets/textures";
constexpr float ASSET_PIECE_SCALE = 1.6f;

const std::string ASSET_SFX_FILE_PATH = "assets/audio/sfx";
const std::string SFX_PLAYER_MOVE_NAME = "player_move";
const std::string SFX_ENEMY_MOVE_NAME = "enemy_move";
const std::string SFX_WARNING_NAME = "warning";
const std::string SFX_CAPTURE_NAME = "capture";
const std::string SFX_CASTLE_NAME = "castle";
const std::string SFX_CHECK_NAME = "check";
const std::string SFX_PROMOTION_NAME = "promotion";
const std::string SFX_GAME_BEGINS_NAME = "game_begins";
const std::string SFX_GAME_ENDS_NAME = "game_ends";
const std::string SFX_PREMOVE_NAME = "pre_move";

}  // namespace lilia::view::constant
