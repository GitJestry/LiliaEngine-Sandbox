#include "lilia/bot/bot_info.hpp"

#include "lilia/view/ui/render/render_constants.hpp"

namespace lilia
{

  BotConfig getBotConfig(BotType type)
  {
    switch (type)
    {
    case BotType::Lilia:
    default:
      return {"Lilia", "?", std::string{view::constant::path::ICON_LILIA}, 14, 30000};
    }
  }

} // namespace lilia
