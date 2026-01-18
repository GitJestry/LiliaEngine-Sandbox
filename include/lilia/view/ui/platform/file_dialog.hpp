#pragma once
#include <optional>
#include <string>

namespace lilia::view::ui::platform
{
  // Returns absolute path to chosen file, or nullopt.
  std::optional<std::string> openExecutableFileDialog();
}
