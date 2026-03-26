#include "lilia/uci/builtin_bootstrap.hpp"

#include "lilia/uci/builtin_engine_locator.hpp"
#include "lilia/uci/engine_provisioner.hpp"
#include "lilia/uci/engine_registry.hpp"
#include "lilia/uci/stockfish_downloader.hpp"

namespace lilia::uci
{
  void bootstrapBuiltinEngines()
  {
    auto &registry = EngineRegistry::instance();
    registry.load();

    // 1) Provision app-local bundled engines (Lilia, and optionally a manually bundled Stockfish).
    const auto bundled = BuiltinEngineLocator::findBundledEngines();
    EngineProvisioner provisioner(registry);
    provisioner.ensureBuiltinsInstalled(bundled);

    // 2) Ensure official Stockfish exists in the shared user engine store if not already present.
    //    If a bundled/manual Stockfish was already provisioned above, this becomes a no-op.
    StockfishDownloader stockfish(registry);
    std::string ignoredError;
    (void)stockfish.ensureInstalledIfMissing(&ignoredError);

    // 3) Refresh from disk so the rest of the app sees the final persisted state.
    registry.load();
  }
} // namespace lilia::uci
