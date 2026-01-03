#include "lilia/view/ui/views/player_info_view.hpp"

#include <algorithm>
#include <cmath>

#include "lilia/view/ui/style/palette_cache.hpp"
#include "lilia/view/ui/render/render_constants.hpp"
#include "lilia/view/ui/render/texture_table.hpp"
#include "lilia/view/ui/style/style.hpp"

namespace lilia::view
{

  namespace
  {

    // Layout
    constexpr float kIconFrameSize = 32.f;
    constexpr float kTextGap = 12.f;
    constexpr float kEloGap = 6.f;

    constexpr float kCapPad = 4.f;
    constexpr float kCapMinH = 18.f;
    constexpr float kCapMaxH = 28.f;
    constexpr float kPieceAdvance = 0.86f;

  } // namespace

  PlayerInfoView::PlayerInfoView()
  {
    m_frame.setSize({kIconFrameSize, kIconFrameSize});
    m_frame.setOutlineThickness(0.f);

    m_captureBox.setOutlineThickness(0.f);

    if (m_font.loadFromFile(std::string(constant::path::FONT)))
    {
      m_font.setSmooth(false);

      m_name.setFont(m_font);
      m_name.setCharacterSize(16);
      m_name.setStyle(sf::Text::Bold);

      m_elo.setFont(m_font);
      m_elo.setCharacterSize(15);

      m_noCaptures.setFont(m_font);
      m_noCaptures.setCharacterSize(14);
      m_noCaptures.setString("no captures");
    }
  }

  Entity PlayerInfoView::makeCapturedEntity(core::PieceType type, core::Color color)
  {
    constexpr std::uint8_t kNumTypes = 6;
    const std::string filename = std::string{constant::path::PIECES_DIR} + "/piece_" +
                                 std::to_string(static_cast<std::uint8_t>(type) +
                                                kNumTypes * static_cast<std::uint8_t>(color)) +
                                 ".png";

    const sf::Texture &tex = TextureTable::getInstance().get(filename);
    Entity e(tex);
    e.setScale(1.f, 1.f);
    return e;
  }

  void PlayerInfoView::setPlayerColor(core::Color color)
  {
    m_playerColor = color;
  }

  void PlayerInfoView::setInfo(const PlayerInfo &info)
  {
    m_iconPath = info.iconPath;
    m_icon.setTexture(TextureTable::getInstance().get(m_iconPath));

    const auto size = m_icon.getOriginalSize();
    if (size.x > 0.f && size.y > 0.f)
    {
      constexpr float innerPad = 2.f;
      const float targetW = kIconFrameSize - 2.f * innerPad;
      const float targetH = kIconFrameSize - 2.f * innerPad;
      const float sx = targetW / size.x;
      const float sy = targetH / size.y;
      const float s = std::min(sx, sy);
      m_icon.setScale(s, s);
    }
    m_icon.setOriginToCenter();

    m_name.setString(info.name);
    if (info.elo.empty())
    {
      m_elo.setString("");
    }
    else
    {
      m_elo.setString(" (" + info.elo + ")");
    }
  }

  void PlayerInfoView::setPosition(const Entity::Position &pos)
  {
    m_position = pos;

    m_frame.setPosition(ui::snap({pos.x, pos.y}));
    m_icon.setPosition(ui::snap({pos.x + kIconFrameSize * 0.5f, pos.y + kIconFrameSize * 0.5f}));

    auto nb = m_name.getLocalBounds();
    const float nameBaseY = pos.y + (kIconFrameSize - nb.height) * 0.5f - nb.top;
    const float textLeft = pos.x + kIconFrameSize + kTextGap;
    m_name.setPosition(ui::snap({textLeft, nameBaseY}));

    auto nB = m_name.getLocalBounds();
    m_elo.setPosition(ui::snap({textLeft + nB.width + kEloGap, nameBaseY}));

    layoutCaptured();
  }

  void PlayerInfoView::setPositionClamped(const Entity::Position &pos, const sf::Vector2u &viewport)
  {
    const float outerW = kIconFrameSize;
    const float outerH = kIconFrameSize;

    constexpr float pad = 8.f;
    Entity::Position clamped = pos;
    clamped.x = std::clamp(clamped.x, pad, float(viewport.x) - outerW - pad);
    clamped.y = std::clamp(clamped.y, pad, float(viewport.y) - outerH - pad);

    setPosition(clamped);
  }

  void PlayerInfoView::setBoardCenter(float centerX)
  {
    m_boardCenter = centerX;
    layoutCaptured();
  }

  void PlayerInfoView::render(sf::RenderWindow &rt)
  {
    const auto pal = PaletteCache::get().palette();

    const sf::Color shadow = pal[ColorId::COL_SHADOW_LIGHT];
    const sf::Color frameBase = pal[ColorId::COL_HEADER];

    const sf::Color nameCol = m_theme ? m_theme->text : pal[ColorId::COL_TEXT];
    const sf::Color eloCol = m_theme ? m_theme->subtle : pal[ColorId::COL_MUTED_TEXT];

    const sf::Color capBase = (m_playerColor == core::Color::White)
                                  ? pal[ColorId::COL_LIGHT_BG]
                                  : pal[ColorId::COL_DARK_BG];

    const sf::Color capText = (m_playerColor == core::Color::White)
                                  ? pal[ColorId::COL_HEADER]
                                  : pal[ColorId::COL_MUTED_TEXT];

    m_name.setFillColor(nameCol);
    m_elo.setFillColor(eloCol);
    m_noCaptures.setFillColor(capText);

    const sf::FloatRect fb = m_frame.getGlobalBounds();
    ui::drawSoftShadowRect(rt, fb, shadow, 1, 2.f);
    ui::drawBevelButton(rt, fb, frameBase, false, false);

    m_icon.draw(rt);
    rt.draw(m_name);
    rt.draw(m_elo);

    const sf::FloatRect cb = m_captureBox.getGlobalBounds();
    ui::drawSoftShadowRect(rt, cb, shadow, 1, 2.f);
    ui::drawBevelButton(rt, cb, capBase, false, false);

    if (m_capturedPieces.empty())
    {
      rt.draw(m_noCaptures);
    }
    else
    {
      for (auto &piece : m_capturedPieces)
        piece.draw(rt);
    }
  }

  void PlayerInfoView::addCapturedPiece(core::PieceType type, core::Color color)
  {
    m_capturedInfo.emplace_back(type, color);
    m_capturedPieces.push_back(makeCapturedEntity(type, color));
    layoutCaptured();
  }

  void PlayerInfoView::removeCapturedPiece()
  {
    if (!m_capturedPieces.empty())
    {
      m_capturedPieces.pop_back();
      if (!m_capturedInfo.empty())
        m_capturedInfo.pop_back();
      layoutCaptured();
    }
  }

  void PlayerInfoView::clearCapturedPieces()
  {
    m_capturedPieces.clear();
    m_capturedInfo.clear();
    layoutCaptured();
  }

  void PlayerInfoView::layoutCaptured()
  {
    const float capH = std::clamp(kIconFrameSize - 6.f, kCapMinH, kCapMaxH);
    const float baseY = ui::snapf(m_frame.getPosition().y + (kIconFrameSize - capH) * 0.5f);

    if (m_capturedPieces.empty())
    {
      auto tb = m_noCaptures.getLocalBounds();
      const float boxW = tb.width + 2.f * kCapPad;

      const float baseX = ui::snapf(m_boardCenter - boxW * 0.5f);
      m_captureBox.setSize({boxW, capH});
      m_captureBox.setPosition({baseX, baseY});

      const float tx = baseX + kCapPad;
      const float ty = baseY + (capH - tb.height) * 0.5f - tb.top;
      m_noCaptures.setPosition(ui::snap({tx, ty}));
      return;
    }

    const float targetH = capH - 2.f * kCapPad;
    std::vector<sf::Vector2f> sizes;
    sizes.reserve(m_capturedPieces.size());

    float x = kCapPad;

    for (auto &piece : m_capturedPieces)
    {
      const auto orig = piece.getOriginalSize();
      if (orig.x <= 0.f || orig.y <= 0.f)
      {
        sizes.emplace_back(0.f, 0.f);
        continue;
      }

      const float s = (targetH / orig.y) * 1.1f;
      piece.setScale(s, s);

      const auto size = piece.getCurrentSize();
      sizes.push_back(size);
      x += size.x * kPieceAdvance;
    }

    const float contentW = x + kCapPad - 4.f;
    const float baseX = ui::snapf(m_boardCenter - contentW * 0.5f);

    m_captureBox.setSize({contentW, capH});
    m_captureBox.setPosition({baseX, baseY});

    float posX = kCapPad;
    for (std::size_t i = 0; i < m_capturedPieces.size(); ++i)
    {
      auto &piece = m_capturedPieces[i];
      const auto size = sizes[i];
      if (size.x <= 0.f || size.y <= 0.f)
        continue;

      const float px = baseX + posX + 6.f;
      const float py = baseY + (capH - size.y) * 2.20f; // kept on purpose
      piece.setPosition(ui::snap({px, py}));

      posX += size.x * kPieceAdvance;
    }
  }

} // namespace lilia::view
