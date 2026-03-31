#pragma once

#include "config.hpp"
#include "lilia/chess/move.hpp"
#include "lilia/chess/position.hpp"
#include "lilia/chess/compiler.hpp"

namespace lilia::engine
{

  LILIA_ALWAYS_INLINE int mvv_lva_fast(const chess::Position &pos, const chess::Move &m)
  {
    if (!m.isCapture() && m.promotion() == chess::PieceType::None)
      return 0;
    const auto &b = pos.getBoard();

    chess::PieceType victimType = chess::PieceType::Pawn;
    if (m.isEnPassant())
    {
      victimType = chess::PieceType::Pawn;
    }
    else if (auto vp = b.getPiece(m.to()))
    {
      victimType = vp->type;
    }

    chess::PieceType attackerType = chess::PieceType::Pawn;
    if (auto ap = b.getPiece(m.from()))
      attackerType = ap->type;

    const int vVictim = base_value[static_cast<int>(victimType)];
    const int vAttacker = base_value[static_cast<int>(attackerType)];

    int score = (vVictim << 5) - vAttacker; // *32 Spreizung
    if (m.promotion() != chess::PieceType::None)
    {
      static constexpr int promo_order[7] = {0, 40, 40, 60, 120, 0, 0};
      score += promo_order[static_cast<int>(m.promotion())];
    }
    if (m.isEnPassant())
      score += 5;
    return score;
  }

} // namespace lilia::engine
