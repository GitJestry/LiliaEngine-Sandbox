#include "lilia/controller/uci_engine_player.hpp"
#include "lilia/engine/uci/uci_utils.hpp"

#include <sstream>

namespace lilia::controller
{
  static model::Move noMove()
  {
    return model::Move(core::NO_SQUARE, core::NO_SQUARE, core::PieceType::None);
  }

  UciEnginePlayer::UciEnginePlayer(lilia::config::BotConfig cfg)
      : m_cfg(std::move(cfg))
  {
    if (m_cfg.engine.executablePath.empty())
      return;

    if (!m_proc.start(m_cfg.engine.executablePath))
      return;

    UciEngineProcess::Id id;
    std::vector<lilia::config::UciOption> opts;
    if (!m_proc.uciHandshake(id, opts))
    {
      m_proc.stop();
      return;
    }

    // Apply configured values (currently defaults from registry; later UI overrides)
    for (const auto &kv : m_cfg.uciValues)
      m_proc.setOption(kv.first, kv.second);

    m_proc.newGame();
    m_ok = true;
  }

  UciEnginePlayer::~UciEnginePlayer()
  {
    m_proc.stop();
  }

  std::future<model::Move> UciEnginePlayer::requestMove(model::ChessGame &game,
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

  model::Move UciEnginePlayer::bestmoveToMove(const std::string &bestLine, model::ChessGame &game) const
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

    const core::Square fromSq = lilia::engine::uci::stringToSquare(uciFrom);
    const core::Square toSq = lilia::engine::uci::stringToSquare(uciTo);

    core::PieceType prom = core::PieceType::None;
    if (promo == 'q')
      prom = core::PieceType::Queen;
    else if (promo == 'r')
      prom = core::PieceType::Rook;
    else if (promo == 'b')
      prom = core::PieceType::Bishop;
    else if (promo == 'n')
      prom = core::PieceType::Knight;

    for (const auto &m : moves)
    {
      if (m.from() == fromSq && m.to() == toSq)
      {
        if (m.promotion() == core::PieceType::None && prom == core::PieceType::None)
          return m;
        if (m.promotion() == prom)
          return m;
      }
    }
    return noMove();
  }
} // namespace lilia::controller
