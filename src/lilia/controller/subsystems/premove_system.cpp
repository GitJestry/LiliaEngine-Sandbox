#include "lilia/controller/subsystems/premove_system.hpp"

#include <cmath>

#include "lilia/controller/game_manager.hpp"
#include "lilia/model/chess_game.hpp"
#include "lilia/controller/subsystems/legal_move_cache.hpp"
#include "lilia/view/ui/screens/game_view.hpp"

namespace lilia::controller
{

  PremoveSystem::PremoveSystem(view::GameView &view, model::ChessGame &game,
                               view::sound::SoundManager &sfx, LegalMoveCache &legal)
      : m_view(view), m_game(game), m_sfx(sfx), m_legal(legal)
  {
    m_pseudo.reserve(64);
  }

  bool PremoveSystem::hasVirtualPiece(core::Square sq) const
  {
    if (!core::validSquare(sq))
      return false;
    return pieceConsideringPremoves(sq).type != core::PieceType::None;
  }

  model::bb::Piece PremoveSystem::pieceConsideringPremoves(core::Square sq) const
  {
    if (!core::validSquare(sq))
      return {};
    if (!m_queue.empty())
    {
      model::Position pos = positionAfterPremoves();
      if (auto virt = pos.getBoard().getPiece(sq))
        return *virt;
    }
    return m_game.getPiece(sq);
  }

  model::Position PremoveSystem::positionAfterPremoves() const
  {
    model::Position pos = m_game.getPositionRefForBot();
    if (m_queue.empty())
      return pos;

    for (const auto &pm : m_queue)
    {
      auto moverOpt = pos.getBoard().getPiece(pm.from);
      if (!moverOpt)
        break;

      pos.getState().sideToMove = pm.moverColor;

      if (pm.capturedType != core::PieceType::None)
      {
        if (pos.getBoard().getPiece(pm.to))
        {
          pos.getBoard().removePiece(pm.to);
        }
        else if (moverOpt->type == core::PieceType::Pawn &&
                 ((static_cast<int>(pm.from) ^ static_cast<int>(pm.to)) & 7))
        {
          core::Square epSq = (moverOpt->color == core::Color::White)
                                  ? static_cast<core::Square>(pm.to - 8)
                                  : static_cast<core::Square>(pm.to + 8);
          pos.getBoard().removePiece(epSq);
        }
      }

      model::bb::Piece moving = *moverOpt;
      pos.getBoard().removePiece(pm.from);
      if (pm.promotion != core::PieceType::None)
        moving.type = pm.promotion;
      pos.getBoard().setPiece(pm.to, moving);

      if (moving.type == core::PieceType::King &&
          std::abs(static_cast<int>(pm.to) - static_cast<int>(pm.from)) == 2)
      {
        core::Square rookFrom = (pm.to > pm.from) ? static_cast<core::Square>(pm.to + 1)
                                                  : static_cast<core::Square>(pm.to - 2);
        core::Square rookTo = (pm.to > pm.from) ? static_cast<core::Square>(pm.to - 1)
                                                : static_cast<core::Square>(pm.to + 1);
        if (auto rook = pos.getBoard().getPiece(rookFrom))
        {
          pos.getBoard().removePiece(rookFrom);
          pos.getBoard().setPiece(rookTo, *rook);
        }
      }
    }

    return pos;
  }

  bool PremoveSystem::isPseudoLegal(core::Square from, core::Square to) const
  {
    if (!core::validSquare(from) || !core::validSquare(to))
      return false;

    model::Position pos = positionAfterPremoves();
    auto pcOpt = pos.getBoard().getPiece(from);
    if (!pcOpt)
      return false;

    const core::PieceType vType = pcOpt->type;
    const core::Color vCol = pcOpt->color;

    if (vType == core::PieceType::King &&
        std::abs(static_cast<int>(to) - static_cast<int>(from)) == 2)
    {
      core::Square rookSq =
          (to > from) ? static_cast<core::Square>(from + 3) : static_cast<core::Square>(from - 4);
      if (pos.getBoard().getPiece(rookSq) && pos.getBoard().getPiece(rookSq)->color == vCol)
        return true;
    }

    model::Board board;
    board.clear();
    board.setPiece(from, {vType, vCol});

    if (vType == core::PieceType::Pawn)
    {
      const int file = static_cast<int>(from) & 7;
      const int forward = (vCol == core::Color::White) ? 8 : -8;
      const model::bb::Piece dummy{core::PieceType::Pawn, ~vCol};
      if (file > 0)
        board.setPiece(static_cast<core::Square>(static_cast<int>(from) + forward - 1), dummy);
      if (file < 7)
        board.setPiece(static_cast<core::Square>(static_cast<int>(from) + forward + 1), dummy);
    }

    model::GameState st{};
    st.sideToMove = vCol;
    st.castlingRights = 0;
    st.enPassantSquare = core::NO_SQUARE;

    m_pseudo.clear();
    m_movegen.generatePseudoLegalMoves(board, st, m_pseudo);

    for (const auto &m : m_pseudo)
      if (m.from() == from && m.to() == to)
        return true;
    return false;
  }

  bool PremoveSystem::enqueue(core::Square from, core::Square to)
  {
    const auto st = m_game.getGameState();
    if (!m_game_manager || !m_game_manager->isHuman(~st.sideToMove))
      return false;
    if (m_queue.size() >= kMaxPremoves)
      return false;
    if (!isPseudoLegal(from, to))
      return false;

    model::Position pos = positionAfterPremoves();
    auto moverOpt = pos.getBoard().getPiece(from);
    if (!moverOpt)
      return false;

    core::PieceType capType = core::PieceType::None;
    core::Color capColor = core::Color::White;
    if (auto cap = pos.getBoard().getPiece(to))
    {
      capType = cap->type;
      capColor = cap->color;
    }

    const core::PieceType moverType = moverOpt->type;
    const core::Color moverColor = moverOpt->color;

    const int rank = static_cast<int>(to) / 8;
    if (moverType == core::PieceType::Pawn && (rank == 0 || rank == 7))
    {
      m_view.playPromotionSelectAnim(to, moverColor);
      beginPendingPromotion(from, to, capType, capColor, moverColor);
      return false;
    }

    Premove pm{};
    pm.from = from;
    pm.to = to;
    pm.moverColor = ~st.sideToMove;
    pm.promotion = core::PieceType::None;
    pm.capturedType = capType;
    pm.capturedColor = capColor;

    m_view.clearAttackHighlights();
    m_view.clearHighlightSquare(from);
    m_view.highlightPremoveSquare(from);
    m_view.highlightPremoveSquare(to);
    m_view.highlightSquare(to);

    m_queue.push_back(pm);
    m_sfx.playEffect(view::sound::Effect::Premove);
    updatePreviews();
    return true;
  }

  void PremoveSystem::beginPendingPromotion(core::Square from, core::Square to,
                                            core::PieceType capType, core::Color capColor,
                                            core::Color moverColor)
  {
    m_pending_premove_promotion = true;
    m_pp_from = from;
    m_pp_to = to;
    m_pp_cap_type = capType;
    m_pp_cap_color = capColor;
    m_pp_mover_color = moverColor;
  }

  void PremoveSystem::completePendingPromotion(core::PieceType promoType)
  {
    if (!m_pending_premove_promotion)
      return;

    if (promoType != core::PieceType::None)
    {
      Premove pm{};
      pm.from = m_pp_from;
      pm.to = m_pp_to;
      pm.promotion = promoType;
      pm.capturedType = m_pp_cap_type;
      pm.capturedColor = m_pp_cap_color;
      pm.moverColor = m_pp_mover_color;

      m_view.clearAttackHighlights();
      m_view.clearHighlightSquare(pm.from);
      m_view.highlightPremoveSquare(pm.from);
      m_view.highlightPremoveSquare(pm.to);
      m_view.highlightSquare(pm.to);

      m_queue.push_back(pm);
      m_sfx.playEffect(view::sound::Effect::Premove);
      updatePreviews();
    }

    m_pending_premove_promotion = false;
    m_pp_from = m_pp_to = core::NO_SQUARE;
    m_pp_cap_type = core::PieceType::None;
  }

  void PremoveSystem::clearAll()
  {
    if (!m_queue.empty() || m_pending_premove_promotion)
    {
      for (const auto &pm : m_queue)
        m_view.clearHighlightSquare(pm.to);
      if (m_pending_premove_promotion && m_pp_to != core::NO_SQUARE)
        m_view.clearHighlightSquare(m_pp_to);

      m_queue.clear();
      m_view.clearPremoveHighlights();
      m_view.clearPremovePieces(true);
      m_pending_premove_promotion = false;
      m_pp_from = m_pp_to = core::NO_SQUARE;
      m_pp_cap_type = core::PieceType::None;
      m_has_pending_auto_move = false;
      m_pending_from = m_pending_to = core::NO_SQUARE;
      m_pending_capture_type = core::PieceType::None;
      m_pending_promotion = core::PieceType::None;
      m_skip_next_move_animation = false;
    }
  }

  void PremoveSystem::suspendVisualsIfAtHead(bool atHead)
  {
    if (!atHead)
      return;
    if (m_queue.empty())
      return;
    if (m_visuals_suspended)
      return;

    m_view.clearPremoveHighlights();
    m_view.clearPremovePieces(true);
    m_visuals_suspended = true;
  }

  void PremoveSystem::restoreVisualsIfNeeded(bool atHead)
  {
    if (!atHead)
      return;
    if (!m_visuals_suspended)
      return;

    m_view.clearPremoveHighlights();
    for (const auto &pm : m_queue)
    {
      m_view.highlightPremoveSquare(pm.from);
      m_view.highlightPremoveSquare(pm.to);
    }
    updatePreviews();
    m_visuals_suspended = false;
  }

  void PremoveSystem::updatePreviews()
  {
    m_view.clearPremovePieces(true);

    model::Position pos = m_game.getPositionRefForBot();

    for (const auto &pm : m_queue)
    {
      auto moverOpt = pos.getBoard().getPiece(pm.from);
      if (!moverOpt)
        continue;

      const core::PieceType movingType = moverOpt->type;
      const core::Color movingCol = moverOpt->color;

      m_view.showPremovePiece(pm.from, pm.to, pm.promotion);

      if (movingType == core::PieceType::King &&
          std::abs(static_cast<int>(pm.to) - static_cast<int>(pm.from)) == 2)
      {
        core::Square rookFrom = (pm.to > pm.from) ? static_cast<core::Square>(pm.to + 1)
                                                  : static_cast<core::Square>(pm.to - 2);
        core::Square rookTo = (pm.to > pm.from) ? static_cast<core::Square>(pm.to - 1)
                                                : static_cast<core::Square>(pm.to + 1);
        m_view.showPremovePiece(rookFrom, rookTo);
      }

      if (pm.capturedType != core::PieceType::None)
      {
        if (pos.getBoard().getPiece(pm.to))
        {
          pos.getBoard().removePiece(pm.to);
        }
        else if (movingType == core::PieceType::Pawn &&
                 ((static_cast<int>(pm.from) ^ static_cast<int>(pm.to)) & 7))
        {
          core::Square epSq = (movingCol == core::Color::White)
                                  ? static_cast<core::Square>(pm.to - 8)
                                  : static_cast<core::Square>(pm.to + 8);
          pos.getBoard().removePiece(epSq);
        }
      }

      model::bb::Piece moving = *moverOpt;
      pos.getBoard().removePiece(pm.from);
      if (pm.promotion != core::PieceType::None)
        moving.type = pm.promotion;
      pos.getBoard().setPiece(pm.to, moving);

      if (moving.type == core::PieceType::King &&
          std::abs(static_cast<int>(pm.to) - static_cast<int>(pm.from)) == 2)
      {
        core::Square rookFrom = (pm.to > pm.from) ? static_cast<core::Square>(pm.to + 1)
                                                  : static_cast<core::Square>(pm.to - 2);
        core::Square rookTo = (pm.to > pm.from) ? static_cast<core::Square>(pm.to - 1)
                                                : static_cast<core::Square>(pm.to + 1);
        if (auto rook = pos.getBoard().getPiece(rookFrom))
        {
          pos.getBoard().removePiece(rookFrom);
          pos.getBoard().setPiece(rookTo, *rook);
        }
      }
    }
  }

  bool PremoveSystem::currentLegal(core::Square from, core::Square to) const
  {
    if (!core::validSquare(from) || !core::validSquare(to))
      return false;
    const auto st = m_game.getGameState();
    const auto pc = m_game.getPiece(from);
    if (pc.type == core::PieceType::None || pc.color != st.sideToMove)
      return false;
    return m_legal.contains(from, to);
  }

  void PremoveSystem::scheduleFromQueueIfTurnMatches()
  {
    if (m_queue.empty())
      return;

    const auto st = m_game.getGameState();
    const core::Color stm = st.sideToMove;
    if (!m_game_manager || !m_game_manager->isHuman(stm))
      return;

    while (!m_queue.empty() && m_queue.front().moverColor == stm)
    {
      Premove pm = m_queue.front();
      m_queue.pop_front();

      if (currentLegal(pm.from, pm.to))
      {
        m_has_pending_auto_move = true;
        m_pending_from = pm.from;
        m_pending_to = pm.to;
        m_pending_capture_type = pm.capturedType;
        m_pending_promotion = pm.promotion;
        m_skip_next_move_animation = true;
        break;
      }
    }

    rebuildHighlights();
    updatePreviews();
  }

  void PremoveSystem::rebuildHighlights()
  {
    m_view.clearPremoveHighlights();
    for (const auto &pm : m_queue)
    {
      m_view.highlightPremoveSquare(pm.from);
      m_view.highlightPremoveSquare(pm.to);
    }
  }

  void PremoveSystem::tickAutoMove()
  {
    if (!m_has_pending_auto_move)
      return;

    const auto st = m_game.getGameState();
    const bool humansTurn = (m_game_manager && m_game_manager->isHuman(st.sideToMove));

    if (humansTurn && currentLegal(m_pending_from, m_pending_to))
    {
      if (auto cap = m_game.getPiece(m_pending_to); cap.type != core::PieceType::None)
        m_pending_capture_type = cap.type;

      m_view.applyPremoveInstant(m_pending_from, m_pending_to, m_pending_promotion);

      auto pc = m_game.getPiece(m_pending_from);
      if (pc.type == core::PieceType::King &&
          std::abs(static_cast<int>(m_pending_to) - static_cast<int>(m_pending_from)) == 2)
      {
        core::Square rookFrom = (m_pending_to > m_pending_from)
                                    ? static_cast<core::Square>(m_pending_to + 1)
                                    : static_cast<core::Square>(m_pending_to - 2);
        core::Square rookTo = (m_pending_to > m_pending_from)
                                  ? static_cast<core::Square>(m_pending_to - 1)
                                  : static_cast<core::Square>(m_pending_to + 1);
        m_view.applyPremoveInstant(rookFrom, rookTo, core::PieceType::None);
      }

      const bool accepted = m_game_manager
                                ? m_game_manager->requestUserMove(m_pending_from, m_pending_to, true,
                                                                  m_pending_promotion)
                                : false;

      if (!accepted)
      {
        m_view.setBoardFen(m_game.getFen());
        m_view.restoreRightClickHighlights();
        clearAll();
      }
      else
      {
        rebuildHighlights();
        updatePreviews();
      }

      m_has_pending_auto_move = false;
      m_pending_from = m_pending_to = core::NO_SQUARE;
      m_pending_promotion = core::PieceType::None;
      m_pending_capture_type = core::PieceType::None;
      return;
    }

    if (humansTurn)
    {
      m_has_pending_auto_move = false;
      m_pending_from = m_pending_to = core::NO_SQUARE;
      m_pending_promotion = core::PieceType::None;
      m_pending_capture_type = core::PieceType::None;

      const auto st2 = m_game.getGameState();
      while (!m_queue.empty() && m_queue.front().moverColor == st2.sideToMove)
      {
        Premove pm2 = m_queue.front();
        m_queue.pop_front();
        if (currentLegal(pm2.from, pm2.to))
        {
          m_has_pending_auto_move = true;
          m_pending_from = pm2.from;
          m_pending_to = pm2.to;
          m_pending_capture_type = pm2.capturedType;
          m_pending_promotion = pm2.promotion;
          m_skip_next_move_animation = true;
          break;
        }
      }

      rebuildHighlights();
      updatePreviews();
    }
  }

  bool PremoveSystem::takeSkipAnimationFlag()
  {
    const bool v = m_skip_next_move_animation;
    m_skip_next_move_animation = false;
    return v;
  }

  core::PieceType PremoveSystem::takeCaptureOverride()
  {
    const core::PieceType v = m_pending_capture_type;
    m_pending_capture_type = core::PieceType::None;
    return v;
  }

} // namespace lilia::controller
