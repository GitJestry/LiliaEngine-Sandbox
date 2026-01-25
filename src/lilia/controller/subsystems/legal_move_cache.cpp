#include "lilia/controller/subsystems/legal_move_cache.hpp"

#include "lilia/model/chess_game.hpp"

namespace lilia::controller
{

  const std::vector<model::Move> &LegalMoveCache::legal() const
  {
    if (!m_cached)
      m_cached = &m_game.generateLegalMoves();
    return *m_cached;
  }

  bool LegalMoveCache::contains(core::Square from, core::Square to) const
  {
    for (const auto &m : legal())
    {
      if (m.from() == from && m.to() == to)
        return true;
    }
    return false;
  }

  bool LegalMoveCache::isPromotion(core::Square from, core::Square to) const
  {
    for (const auto &m : legal())
    {
      if (m.from() == from && m.to() == to && m.promotion() != core::PieceType::None)
        return true;
    }
    return false;
  }

} // namespace lilia::controller
