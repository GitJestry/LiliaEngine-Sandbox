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

    constexpr float kNameEloLineGap = 2.f;
    constexpr float kTextToCaptureGap = 10.f;

    constexpr float kCapPad = 4.f;
    constexpr float kCapMinH = 18.f;
    constexpr float kCapMaxH = 28.f;
    constexpr float kPieceAdvance = 0.86f;

    constexpr float kOutcomeGap = 8.f;
    constexpr float kOutcomePadX = 7.f;
    constexpr float kOutcomePadY = 2.f;
    constexpr float kOutcomeMinH = 16.f;

    // Ellipsize right (keep prefix)
    std::string ellipsizeRight(const sf::Font &font,
                               unsigned size,
                               const std::string &s,
                               float maxWidthPx,
                               sf::Uint32 style = sf::Text::Regular)
    {
      if (s.empty() || maxWidthPx <= 0.f)
        return "...";

      sf::Text t("", font, size);
      t.setStyle(style);

      t.setString(s);
      if (t.getLocalBounds().width <= maxWidthPx)
        return s;

      const std::string ell = "...";
      t.setString(ell);
      if (t.getLocalBounds().width > maxWidthPx)
        return "...";

      int lo = 0;
      int hi = static_cast<int>(s.size());

      while (lo < hi)
      {
        int mid = (lo + hi + 1) / 2;
        std::string probe = s.substr(0, static_cast<std::size_t>(mid)) + ell;
        t.setString(probe);
        if (t.getLocalBounds().width <= maxWidthPx)
          lo = mid;
        else
          hi = mid - 1;
      }

      if (lo <= 0)
        return ell;

      return s.substr(0, static_cast<std::size_t>(lo)) + ell;
    }

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
      m_elo.setCharacterSize(14); // slightly smaller under name

      m_noCaptures.setFont(m_font);
      m_noCaptures.setCharacterSize(14);
      m_noCaptures.setString("no captures");

      m_outcomeText.setFont(m_font);
      m_outcomeText.setCharacterSize(12);
      m_outcomeText.setStyle(sf::Text::Bold);
      m_outcomePill.setOutlineThickness(1.f);
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

  void PlayerInfoView::setOutcome(std::optional<model::analysis::Outcome> outcome)
  {
    // Normalize Unknown -> hide (keeps call sites simple)
    if (outcome && *outcome == model::analysis::Outcome::Unknown)
      outcome.reset();

    m_outcome = outcome;

    if (!m_outcome)
    {
      m_outcomeText.setString("");
      m_outcomePill.setSize({0.f, 0.f});
      layoutText();
      return;
    }

    switch (*m_outcome)
    {
    case model::analysis::Outcome::Win:
      m_outcomeText.setString("WIN");
      break;
    case model::analysis::Outcome::Loss:
      m_outcomeText.setString("LOSS");
      break;
    case model::analysis::Outcome::Draw:
      m_outcomeText.setString("DRAW");
      break;
    default:
      m_outcomeText.setString("");
      break;
    }

    layoutText();
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

    // Store originals; layoutText() will format and ellipsize as needed.
    m_fullName = info.name;
    m_fullElo = info.elo;

    layoutText();
  }

  void PlayerInfoView::setPosition(const Entity::Position &pos)
  {
    m_position = pos;

    m_frame.setPosition(ui::snap({pos.x, pos.y}));
    m_icon.setPosition(ui::snap({pos.x + kIconFrameSize * 0.5f, pos.y + kIconFrameSize * 0.5f}));

    // capture box position depends on board center + captured set
    layoutCaptured();
    // layoutCaptured() calls layoutText() at end
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
    layoutCaptured(); // will also layoutText()
  }

  void PlayerInfoView::layoutText()
  {
    if (m_font.getInfo().family.empty())
      return;

    const float textLeft = m_position.x + kIconFrameSize + kTextGap;

    // --- Outcome pill width reservation ---
    float pillW = 0.f;
    float pillH = 0.f;
    const bool hasOutcome = m_outcome.has_value() && !m_outcomeText.getString().isEmpty();
    if (hasOutcome)
    {
      const auto ob = m_outcomeText.getLocalBounds();
      pillW = ob.width + 2.f * kOutcomePadX;
      pillH = std::max(kOutcomeMinH, ob.height + 2.f * kOutcomePadY);
    }

    float maxTextW = 10000.f;

    const float capLeft = m_captureBox.getPosition().x;
    if (m_boardCenter > 0.f && capLeft > 0.f && textLeft < capLeft)
    {
      // Reserve pill + gap so name never collides with it or the capture box.
      const float reserve = hasOutcome ? (kOutcomeGap + pillW) : 0.f;
      maxTextW = std::max(0.f, capLeft - textLeft - kTextToCaptureGap - reserve);
    }

    const std::string nameDisp =
        ellipsizeRight(m_font, m_name.getCharacterSize(), m_fullName, maxTextW, sf::Text::Bold);
    m_name.setString(nameDisp);

    const std::string eloRaw = m_fullElo.empty() ? "" : ("(" + m_fullElo + ")");
    const std::string eloDisp =
        eloRaw.empty() ? "" : ellipsizeRight(m_font, m_elo.getCharacterSize(), eloRaw, maxTextW);
    m_elo.setString(eloDisp);

    const auto nb = m_name.getLocalBounds();
    const auto eb = m_elo.getLocalBounds();

    const float gap = eloDisp.empty() ? 0.f : kNameEloLineGap;
    const float blockH = nb.height + (eloDisp.empty() ? 0.f : (gap + eb.height));
    const float top = m_position.y + (kIconFrameSize - blockH) * 0.5f;

    const float nameY = top - nb.top;
    m_name.setPosition(ui::snap({textLeft, nameY}));

    if (!eloDisp.empty())
    {
      const float eloY = top + nb.height + gap - eb.top;
      m_elo.setPosition(ui::snap({textLeft, eloY}));
    }
    else
    {
      m_elo.setPosition(ui::snap({textLeft, nameY}));
    }

    // --- Position outcome pill on the name line ---
    if (hasOutcome)
    {
      const float nameRight = textLeft + (nb.left + nb.width);
      const float pillX = ui::snapf(nameRight + kOutcomeGap);
      const float pillY = ui::snapf(nameY + (nb.height - pillH) * 0.5f);

      m_outcomePill.setSize({pillW, pillH});
      m_outcomePill.setPosition({pillX, pillY});

      const auto ob = m_outcomeText.getLocalBounds();
      const float tx = pillX + kOutcomePadX - ob.left;
      const float ty = pillY + (pillH - ob.height) * 0.5f - ob.top;
      m_outcomeText.setPosition(ui::snap({tx, ty}));
    }
    else
    {
      m_outcomePill.setSize({0.f, 0.f});
    }
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
    if (m_outcome && m_outcomePill.getSize().x > 0.f)
    {
      const ui::Theme *th = m_theme;

      sf::Color fill, outline, text;
      if (th)
      {
        // Conservative palette: only uses your existing theme colors.
        outline = th->panelBorder;
        fill = ui::lighten(th->panel, 6);
        text = th->text;

        switch (*m_outcome)
        {
        case model::analysis::Outcome::Win:
          fill = th->accent;
          text = th->onAccent;
          outline = ui::darken(th->accent, 18);
          break;
        case model::analysis::Outcome::Loss:
          fill = ui::darken(th->panel, 8);
          text = th->subtle;
          outline = th->panelBorder;
          break;
        case model::analysis::Outcome::Draw:
          fill = th->button;
          text = th->text;
          outline = th->panelBorder;
          break;
        default:
          break;
        }
      }
      else
      {
        // Fallback: keep it visible with neutral colors if theme not set.
        fill = sf::Color(60, 60, 60);
        outline = sf::Color(20, 20, 20);
        text = sf::Color(230, 230, 230);
      }

      m_outcomePill.setFillColor(fill);
      m_outcomePill.setOutlineColor(outline);
      m_outcomeText.setFillColor(text);

      rt.draw(m_outcomePill);
      rt.draw(m_outcomeText);
    }

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

      layoutText(); // keep text safe vs. updated capture box
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

    layoutText(); // keep text safe vs. updated capture box
  }

} // namespace lilia::view
