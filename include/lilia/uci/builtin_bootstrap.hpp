#pragma once

namespace lilia::uci
{
  // Finds bundled built-ins near the current runtime target, provisions them
  // into the shared user engine store, and refreshes the registry.
  void bootstrapBuiltinEngines();
}
