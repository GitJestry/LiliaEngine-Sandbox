#include "lilia/view/ui/interaction/highlight_manager.hpp"

#include <SFML/Graphics.hpp>
#include <cmath>
#include <numbers>
#include <vector>

#include "lilia/view/ui/style/palette_cache.hpp"
#include "lilia/view/ui/render/render_constants.hpp"
#include "lilia/view/ui/render/resource_table.hpp"

namespace lilia::view
{

  namespace
  {

    static Entity makeHL(std::string_view texName, bool scaleToSquare)
    {
      Entity e(ResourceTable::getInstance().getTexture(std::string{texName}));

      if (scaleToSquare)
      {
        e.setScale(constant::SQUARE_PX_SIZE, constant::SQUARE_PX_SIZE);
      }
      else
      {
        e.setScale(1.f, 1.f);
      }

      e.setOriginToCenter();
      return e;
    }

    static unsigned int arrowKey(core::Square from, core::Square to)
    {
      return static_cast<unsigned int>(from) | (static_cast<unsigned int>(to) << 7);
    }

  } // namespace

  HighlightManager::HighlightManager(const BoardView &boardRef)
      : m_board_view_ref(boardRef) {}

  void HighlightManager::renderEntitiesToBoard(std::unordered_map<core::Square, Entity> &map,
                                               sf::RenderWindow &window)
  {
    for (auto &[sq, ent] : map)
    {
      ent.setPosition(m_board_view_ref.getSquareScreenPos(sq));
      ent.draw(window);
    }
  }

  void HighlightManager::renderAttackToBoard(sf::RenderWindow &window)
  {
    for (auto &[sq, mark] : m_attack)
    {
      mark.entity.setPosition(m_board_view_ref.getSquareScreenPos(sq));
      mark.entity.draw(window);
    }
  }

  void HighlightManager::renderAttack(sf::RenderWindow &window) { renderAttackToBoard(window); }
  void HighlightManager::renderHover(sf::RenderWindow &window) { renderEntitiesToBoard(m_hover, window); }
  void HighlightManager::renderSelect(sf::RenderWindow &window) { renderEntitiesToBoard(m_select, window); }
  void HighlightManager::renderPremove(sf::RenderWindow &window) { renderEntitiesToBoard(m_premove, window); }
  void HighlightManager::renderRightClickSquares(sf::RenderWindow &window)
  {
    renderEntitiesToBoard(m_rclick_squares, window);
  }

  void HighlightManager::renderRightClickArrows(sf::RenderWindow &window)
  {
    const sf::Color col = PaletteCache::get().color(ColorId::COL_RCLICK_HIGHLIGHT);
    const float sqSize = static_cast<float>(constant::SQUARE_PX_SIZE);

    const float thickness = sqSize * 0.2f;
    const float headLength = sqSize * 0.38f;
    const float headWidth = sqSize * 0.48f;
    const float jointOverlap = thickness * 0.5f;
    const float edgeOffset = sqSize * 0.5f * 0.8f;

    auto clipSegmentEnds = [&](sf::Vector2f a, sf::Vector2f b, float clipA,
                               float clipB) -> std::pair<sf::Vector2f, sf::Vector2f>
    {
      sf::Vector2f d = b - a;
      float len = std::sqrt(d.x * d.x + d.y * d.y);
      if (len <= 1e-3f)
        return {a, b};
      sf::Vector2f u = d / len;
      return {a + u * clipA, b - u * clipB};
    };

    auto drawSegment = [&](sf::Vector2f s, sf::Vector2f e, bool arrowHead)
    {
      sf::Vector2f diff = e - s;
      float len = std::sqrt(diff.x * diff.x + diff.y * diff.y);
      if (len <= 0.1f)
        return;

      float angle = std::atan2(diff.y, diff.x) * 180.f / std::numbers::pi_v<float>;
      float bodyLen = arrowHead ? std::max(0.f, len - headLength) : len;

      sf::RectangleShape body({bodyLen, thickness});
      body.setFillColor(col);
      body.setOrigin(0.f, thickness / 2.f);
      body.setPosition(s);
      body.setRotation(angle);
      window.draw(body);

      if (arrowHead)
      {
        sf::ConvexShape head(3);
        head.setPoint(0, {0.f, 0.f});
        head.setPoint(1, {-headLength, headWidth / 2.f});
        head.setPoint(2, {-headLength, -headWidth / 2.f});
        head.setFillColor(col);
        head.setPosition(e);
        head.setRotation(angle);
        window.draw(head);
      }
    };

    for (const auto &[_, pr] : m_rclick_arrows)
    {
      core::Square fromSq = pr.first;
      core::Square toSq = pr.second;
      if (fromSq == toSq)
        continue;

      sf::Vector2f fromPos = m_board_view_ref.getSquareScreenPos(fromSq);
      sf::Vector2f toPos = m_board_view_ref.getSquareScreenPos(toSq);

      int fx = static_cast<int>(fromSq) & 7;
      int fy = static_cast<int>(fromSq) >> 3;
      int tx = static_cast<int>(toSq) & 7;
      int ty = static_cast<int>(toSq) >> 3;
      int adx = std::abs(tx - fx);
      int ady = std::abs(ty - fy);
      bool knight = (adx == 1 && ady == 2) || (adx == 2 && ady == 1);

      if (knight)
      {
        int cornerFile = (ady > adx) ? fx : tx;
        int cornerRank = (ady > adx) ? ty : fy;
        core::Square cornerSq =
            static_cast<core::Square>(cornerFile + cornerRank * constant::BOARD_SIZE);
        sf::Vector2f corner = m_board_view_ref.getSquareScreenPos(cornerSq);

        auto [leg1Start, leg1End] = clipSegmentEnds(fromPos, corner, edgeOffset, -jointOverlap);
        auto [leg2Start, leg2End] = clipSegmentEnds(corner, toPos, -jointOverlap, 0.f);

        drawSegment(leg1Start, leg1End, false);
        drawSegment(leg2Start, leg2End, true);
      }
      else
      {
        auto [start, end] = clipSegmentEnds(fromPos, toPos, edgeOffset, 0.f);
        drawSegment(start, end, true);
      }
    }
  }

  void HighlightManager::highlightSquare(core::Square pos)
  {
    m_select[pos] = makeHL(constant::tex::SELECT_HL, /*scaleToSquare=*/true);
  }

  void HighlightManager::highlightAttackSquare(core::Square pos)
  {
    AttackMark m;
    m.entity = makeHL(constant::tex::ATTACK_HL, /*scaleToSquare=*/false);
    m.capture = false;
    m_attack[pos] = std::move(m);
  }

  void HighlightManager::highlightCaptureSquare(core::Square pos)
  {
    AttackMark m;
    m.entity = makeHL(constant::tex::CAPTURE_HL, /*scaleToSquare=*/false);
    m.capture = true;
    m_attack[pos] = std::move(m);
  }

  void HighlightManager::highlightHoverSquare(core::Square pos)
  {
    m_hover[pos] = makeHL(constant::tex::HOVER_HL, /*scaleToSquare=*/false);
  }

  void HighlightManager::highlightPremoveSquare(core::Square pos)
  {
    m_premove[pos] = makeHL(constant::tex::PREMOVE_HL, /*scaleToSquare=*/true);
  }

  void HighlightManager::highlightRightClickSquare(core::Square pos)
  {
    if (auto it = m_rclick_squares.find(pos); it != m_rclick_squares.end())
    {
      m_rclick_squares.erase(it);
      return;
    }
    m_rclick_squares[pos] = makeHL(constant::tex::RCLICK_HL, /*scaleToSquare=*/true);
  }

  void HighlightManager::highlightRightClickArrow(core::Square from, core::Square to)
  {
    unsigned int key = arrowKey(from, to);
    if (auto it = m_rclick_arrows.find(key); it != m_rclick_arrows.end())
    {
      m_rclick_arrows.erase(it);
      return;
    }
    m_rclick_arrows[key] = {from, to};
  }

  std::vector<core::Square> HighlightManager::getRightClickSquares() const
  {
    std::vector<core::Square> out;
    out.reserve(m_rclick_squares.size());
    for (const auto &[sq, _] : m_rclick_squares)
      out.push_back(sq);
    return out;
  }

  std::vector<std::pair<core::Square, core::Square>> HighlightManager::getRightClickArrows() const
  {
    std::vector<std::pair<core::Square, core::Square>> out;
    out.reserve(m_rclick_arrows.size());
    for (const auto &[_, pr] : m_rclick_arrows)
      out.push_back(pr);
    return out;
  }

  void HighlightManager::clearAllHighlights()
  {
    m_select.clear();
    m_attack.clear();
    m_hover.clear();
    m_premove.clear();
    m_rclick_squares.clear();
    m_rclick_arrows.clear();
  }

  void HighlightManager::clearNonPremoveHighlights()
  {
    m_select.clear();
    m_attack.clear();
    m_hover.clear();
    m_rclick_squares.clear();
    m_rclick_arrows.clear();
  }

  void HighlightManager::clearAttackHighlights() { m_attack.clear(); }

  void HighlightManager::clearHighlightSquare(core::Square pos) { m_select.erase(pos); }
  void HighlightManager::clearHighlightHoverSquare(core::Square pos) { m_hover.erase(pos); }
  void HighlightManager::clearHighlightPremoveSquare(core::Square pos) { m_premove.erase(pos); }

  void HighlightManager::clearPremoveHighlights() { m_premove.clear(); }

  void HighlightManager::clearRightClickHighlights()
  {
    m_rclick_squares.clear();
    m_rclick_arrows.clear();
  }

} // namespace lilia::view
