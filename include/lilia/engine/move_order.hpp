#pragma once

#include "config.hpp"
#include "lilia/chess/move.hpp"
#include "lilia/chess/position.hpp"
#include "lilia/chess/compiler.hpp"

namespace lilia::engine
{

  LILIA_ALWAYS_INLINE int mvv_lva(const chess::Position &pos, const chess::Move &m)
  {
    const bool isCap = m.isCapture();
    const chess::PieceType promo = m.promotion();

    if (!isCap && promo == chess::PieceType::None)
      return 0;

    const auto &b = pos.getBoard();

    auto ap = b.getPiece(m.from());
    if (!ap)
      return 0;

    const chess::PieceType attackerType = ap->type;
    const int attackerValue = base_value[static_cast<int>(attackerType)];

    int score = 0;

    if (isCap)
    {
      chess::PieceType victimType = chess::PieceType::Pawn;

      if (m.isEnPassant())
      {
        victimType = chess::PieceType::Pawn;
      }
      else if (auto vp = b.getPiece(m.to()))
      {
        victimType = vp->type;
      }
      else
      {
        return 0;
      }

      const int victimValue = base_value[static_cast<int>(victimType)];

      // MVV-LVA core: large victim first, small attacker preferred
      score += (victimValue << 6) - attackerValue;

      if (m.isEnPassant())
        score += 1;
    }

    if (promo != chess::PieceType::None)
    {
      // Promotion bonus should reflect net material gain,
      // not a fake captured victim.
      const int promoGain =
          base_value[static_cast<int>(promo)] -
          base_value[static_cast<int>(chess::PieceType::Pawn)];

      score += (promoGain << 5);
    }

    return score;
  }

}
