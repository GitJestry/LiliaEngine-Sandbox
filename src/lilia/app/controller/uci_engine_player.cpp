#include "lilia/app/controller/uci_engine_player.hpp"
#include "lilia/protocol/uci/uci_helper.hpp"
#include "lilia/app/engines/engine_registry.hpp"

#include <filesystem>
#include <sstream>

namespace lilia::app::controller
{
  static chess::Move noMove()
  {
    return chess::Move(chess::NO_SQUARE, chess::NO_SQUARE, chess::PieceType::None);
  }

  UciEnginePlayer::UciEnginePlayer(domain::analysis::config::BotConfig cfg)
      : m_cfg(std::move(cfg))
  {
    std::string exePath;
    std::string workingDir;

    // Preferred path: resolve by stable engine id through the registry.
    if (!m_cfg.engine.engineId.empty())
    {
      auto entry = engines::EngineRegistry::instance().get(m_cfg.engine.engineId);
      if (!entry)
        return;

      // Refresh the BotConfig's EngineRef with the registry-resolved platform install.
      m_cfg.engine = entry->ref;
      exePath = entry->ref.executablePath;
      workingDir = entry->workingDirectory.string();
    }
    else
    {
      // Legacy fallback if only a raw path is present.
      exePath = m_cfg.engine.executablePath;
      if (!exePath.empty())
      {
        std::filesystem::path p(exePath);
        workingDir = p.has_parent_path()
                         ? p.parent_path().string()
                         : std::filesystem::current_path().string();
      }
    }

    if (exePath.empty())
      return;

    if (!m_proc.start(exePath, workingDir))
      return;

    engines::UciEngineProcess::Id id;
    std::vector<domain::analysis::config::UciOption> opts;
    if (!m_proc.uciHandshake(id, opts))
    {
      m_proc.stop();
      return;
    }

    for (const auto &kv : m_cfg.uciValues)
      m_proc.setOption(kv.first, kv.second);

    m_proc.newGame();
    m_ok = true;
  }

  UciEnginePlayer::~UciEnginePlayer()
  {
    m_proc.stop();
  }

  std::future<chess::Move> UciEnginePlayer::requestMove(chess::ChessGame &game,
                                                        std::atomic_bool &cancel)
  {
    return std::async(std::launch::async, [this, &game, &cancel]()
                      {
      if (!m_ok)
        return noMove();

      // You need a FEN getter. Use your existing API or add it.
      const std::string fen = game.getFen(); // ADAPT if your method name differs

      m_proc.position(fen, {});

      // Search control policy:
      // - If you later wire clock times: use goTime(...)
      // - For now: movetime or depth fallback
      if (m_cfg.limits.movetimeMs)
        m_proc.goFixedMovetime(*m_cfg.limits.movetimeMs);
      else if (m_cfg.limits.depth)
        m_proc.goFixedDepth(*m_cfg.limits.depth);
      else
        m_proc.goFixedMovetime(500);

      // cancellation: send stop (engine will still output bestmove; we consume it)
      if (cancel.load())
        m_proc.stopSearch();

      std::string best = m_proc.waitBestmove();
      if (best.empty())
        return noMove();

      if (cancel.load())
        return noMove();

      return bestmoveToMove(best, game); });
  }

  chess::Move UciEnginePlayer::bestmoveToMove(const std::string &bestLine, chess::ChessGame &game) const
  {
    // bestmove e2e4 | bestmove e7e8q
    std::istringstream iss(bestLine);
    std::string tok, uci;
    iss >> tok; // "bestmove"
    iss >> uci;
    if (uci.size() < 4)
      return noMove();

    // Match against legal moves so you don’t need to guess square indexing.
    const auto &moves = game.generateLegalMoves();

    auto uciFrom = uci.substr(0, 2);
    auto uciTo = uci.substr(2, 2);
    char promo = (uci.size() >= 5) ? uci[4] : '\0';

    const chess::Square fromSq = protocol::uci::stringToSquare(uciFrom);
    const chess::Square toSq = protocol::uci::stringToSquare(uciTo);

    chess::PieceType prom = chess::PieceType::None;
    if (promo == 'q')
      prom = chess::PieceType::Queen;
    else if (promo == 'r')
      prom = chess::PieceType::Rook;
    else if (promo == 'b')
      prom = chess::PieceType::Bishop;
    else if (promo == 'n')
      prom = chess::PieceType::Knight;

    for (const auto &m : moves)
    {
      if (m.from() == fromSq && m.to() == toSq)
      {
        if (m.promotion() == chess::PieceType::None && prom == chess::PieceType::None)
          return m;
        if (m.promotion() == prom)
          return m;
      }
    }
    return noMove();
  }
}
