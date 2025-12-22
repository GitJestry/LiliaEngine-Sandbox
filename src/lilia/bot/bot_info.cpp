#include "lilia/bot/bot_info.hpp"

#include "lilia/view/render_constants.hpp"

namespace lilia {

BotConfig getBotConfig(BotType type) {
  switch (type) {
    case BotType::Lilia:
    default:
      return {"Lilia", "?", view::constant::STR_FILE_PATH_ICON_LILIA_START_SCREEN, 12, 30000};
  }
}

}  // namespace lilia
