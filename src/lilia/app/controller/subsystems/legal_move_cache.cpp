#include "lilia/app/controller/subsystems/legal_move_cache.hpp"

#include "lilia/chess/chess_game.hpp"

namespace lilia::app::controller
{

  const std::vector<chess::Move> &LegalMoveCache::legal() const
  {
    if (!m_cached)
      m_cached = &m_game.generateLegalMoves();
    return *m_cached;
  }

  bool LegalMoveCache::contains(chess::Square from, chess::Square to) const
  {
    for (const auto &m : legal())
    {
      if (m.from() == from && m.to() == to)
        return true;
    }
    return false;
  }

  bool LegalMoveCache::isPromotion(chess::Square from, chess::Square to) const
  {
    for (const auto &m : legal())
    {
      if (m.from() == from && m.to() == to && m.promotion() != chess::PieceType::None)
        return true;
    }
    return false;
  }

} // namespace lilia::controller
