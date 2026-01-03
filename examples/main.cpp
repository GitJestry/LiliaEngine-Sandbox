#include "lilia/uci/uci.hpp"

#ifdef LILIA_UI
#include "lilia/app/app.hpp"
#endif

int main() {
#ifdef LILIA_UI

  lilia::app::App app;
  return app.run();
#elif defined(LILIA_ENGINE)

  lilia::UCI uci;
  return uci.run();
#else
#endif
}
