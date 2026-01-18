#include "lilia/uci/uci.hpp"

#ifdef LILIA_UI
#include "lilia/app/app.hpp"
#include "lilia/engine/uci/builtin_bootstrap.hpp"
#endif

int main()
{
#ifdef LILIA_UI
  lilia::engine::uci::bootstrapBuiltinEngines();
  lilia::app::App app;
  return app.run();
#elif defined(LILIA_ENGINE)

  lilia::UCI uci;
  return uci.run();
#else
#endif
}
