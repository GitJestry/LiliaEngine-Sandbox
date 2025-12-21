#include "lilia/view/player_info_view.hpp"

#include <algorithm>  // std::clamp, std::min, std::max
#include <cmath>

#include "lilia/view/render_constants.hpp"
#include "lilia/view/texture_table.hpp"

namespace lilia::view {

namespace {

// Layout
constexpr float kIconFrameSize = 32.f;
constexpr float kIconOutline = 1.f;
constexpr float kTextGap = 12.f;
constexpr float kEloGap = 6.f;
constexpr float kCapPad = 4.f;
constexpr float kCapMinH = 18.f;
constexpr float kCapMaxH = 28.f;
constexpr float kPieceAdvance = 0.86f;

inline float snapf(float v) {
  return std::round(v);
}
inline sf::Vector2f snap(sf::Vector2f p) {
  return {snapf(p.x), snapf(p.y)};
}

inline sf::Color lighten(sf::Color c, int d) {
  auto clip = [](int x) { return std::clamp(x, 0, 255); };
  return sf::Color(clip(c.r + d), clip(c.g + d), clip(c.b + d), c.a);
}
inline sf::Color darken(sf::Color c, int d) {
  return lighten(c, -d);
}

// micro soft-shadow (very small footprint)
inline void drawSoftShadowRect(sf::RenderTarget& t, const sf::FloatRect& r, int layers = 1,
                               float step = 2.f) {
  for (int i = layers; i >= 1; --i) {
    float grow = static_cast<float>(i) * step;  // 2px or 4px total spread
    sf::RectangleShape s({r.width + 2.f * grow, r.height + 2.f * grow});
    s.setPosition(snapf(r.left - grow), snapf(r.top - grow));
    sf::Color sc = constant::COL_SHADOW_LIGHT;
    sc.a = static_cast<sf::Uint8>(22 * i);  // faint
    s.setFillColor(sc);
    t.draw(s);
  }
}

// softer bevel (reduced contrast)
inline void drawBevelAround(sf::RenderTarget& t, const sf::FloatRect& r, sf::Color base) {
  sf::RectangleShape top({r.width, 1.f});
  top.setPosition(snapf(r.left), snapf(r.top));
  top.setFillColor(lighten(base, 12));
  t.draw(top);

  sf::RectangleShape bottom({r.width, 1.f});
  bottom.setPosition(snapf(r.left), snapf(r.top + r.height - 1.f));
  bottom.setFillColor(darken(base, 14));
  t.draw(bottom);

  sf::RectangleShape inset({r.width - 2.f, r.height - 2.f});
  inset.setPosition(snapf(r.left + 1.f), snapf(r.top + 1.f));
  inset.setFillColor(sf::Color::Transparent);
  inset.setOutlineThickness(1.f);
  inset.setOutlineColor(constant::COL_BORDER_BEVEL);
  t.draw(inset);
}

}  // namespace

PlayerInfoView::PlayerInfoView() {
  // 32x32 icon frame
  m_frame.setOutlineThickness(kIconOutline);
  m_frame.setSize({kIconFrameSize, kIconFrameSize});

  if (m_font.loadFromFile(constant::STR_FILE_PATH_FONT)) {
    m_font.setSmooth(false);

    m_name.setFont(m_font);
    m_name.setCharacterSize(16);
    m_name.setFillColor(constant::COL_TEXT);
    m_name.setStyle(sf::Text::Bold);

    m_elo.setFont(m_font);
    m_elo.setCharacterSize(15);
    m_elo.setFillColor(constant::COL_MUTED_TEXT);
    m_elo.setStyle(sf::Text::Regular);

    m_noCaptures.setFont(m_font);
    m_noCaptures.setCharacterSize(14);
    m_noCaptures.setString("no captures");
  }

  // let bevel define the edge; remove extra outline thickness on the box
  m_captureBox.setOutlineThickness(0.f);
  m_captureBox.setOutlineColor(sf::Color::Transparent);

  applyTheme();
  m_listener_id = ColorPaletteManager::get().addListener([this]() { applyTheme(); });
}

PlayerInfoView::~PlayerInfoView() {
  ColorPaletteManager::get().removeListener(m_listener_id);
}

void PlayerInfoView::setPlayerColor(core::Color color) {
  m_playerColor = color;
  if (m_playerColor == core::Color::White) {
    m_captureBox.setFillColor(constant::COL_LIGHT_BG);
    m_noCaptures.setFillColor(constant::COL_HEADER);
  } else {
    m_captureBox.setFillColor(constant::COL_DARK_BG);
    m_noCaptures.setFillColor(constant::COL_MUTED_TEXT);
  }
}

void PlayerInfoView::applyTheme() {
  m_frame.setFillColor(constant::COL_HEADER);
  m_frame.setOutlineColor(constant::COL_BORDER);
  m_name.setFillColor(constant::COL_TEXT);
  m_elo.setFillColor(constant::COL_MUTED_TEXT);
  setPlayerColor(m_playerColor);
  if (!m_iconPath.empty()) {
    m_icon.setTexture(TextureTable::getInstance().get(m_iconPath));
  }
  m_capturedPieces.clear();
  for (auto [type, color] : m_capturedInfo) {
    std::uint8_t numTypes = 6;
    std::string filename = constant::ASSET_PIECES_FILE_PATH + std::string("/piece_") +
                           std::to_string(static_cast<std::uint8_t>(type) +
                                          numTypes * static_cast<std::uint8_t>(color)) +
                           ".png";
    const sf::Texture& texture = TextureTable::getInstance().get(filename);
    Entity piece(texture);
    piece.setScale(1.f, 1.f);
    m_capturedPieces.push_back(std::move(piece));
  }
  layoutCaptured();
}

void PlayerInfoView::setInfo(const PlayerInfo& info) {
  m_iconPath = info.iconPath;
  m_icon.setTexture(TextureTable::getInstance().get(m_iconPath));

  const auto size = m_icon.getOriginalSize();
  if (size.x > 0.f && size.y > 0.f) {
    const float innerPad = 2.f;
    const float targetW = kIconFrameSize - 2.f * innerPad;
    const float targetH = kIconFrameSize - 2.f * innerPad;
    const float sx = targetW / size.x;
    const float sy = targetH / size.y;
    const float s = std::min(sx, sy);
    m_icon.setScale(s, s);
  }
  m_icon.setOriginToCenter();

  m_name.setString(info.name);
  if (info.elo.empty()) {
    m_elo.setString("");
  } else {
    m_elo.setString(" (" + info.elo + ")");
  }
}

void PlayerInfoView::setPosition(const Entity::Position& pos) {
  m_position = pos;

  m_frame.setPosition(snap({pos.x, pos.y}));
  m_icon.setPosition(snap({pos.x + kIconFrameSize * 0.5f, pos.y + kIconFrameSize * 0.5f}));

  auto nb = m_name.getLocalBounds();
  const float nameBaseY = pos.y + (kIconFrameSize - nb.height) * 0.5f - nb.top;
  const float textLeft = pos.x + kIconFrameSize + kTextGap;
  m_name.setPosition(snap({textLeft, nameBaseY}));

  auto nB = m_name.getLocalBounds();
  m_elo.setPosition(snap({textLeft + nB.width + kEloGap, nameBaseY}));

  layoutCaptured();
}

void PlayerInfoView::setPositionClamped(const Entity::Position& pos,
                                        const sf::Vector2u& viewportSize) {
  const float outerW = kIconFrameSize + 2.f * kIconOutline;
  const float outerH = kIconFrameSize + 2.f * kIconOutline;

  const float pad = 8.f;
  Entity::Position clamped = pos;
  clamped.x = std::clamp(clamped.x, pad, static_cast<float>(viewportSize.x) - outerW - pad);
  clamped.y = std::clamp(clamped.y, pad, static_cast<float>(viewportSize.y) - outerH - pad);

  setPosition(clamped);
}

void PlayerInfoView::setBoardCenter(float centerX) {
  m_boardCenter = centerX;
  layoutCaptured();
}

void PlayerInfoView::render(sf::RenderWindow& window) {
  // ultra-subtle shadow & bevel on avatar frame
  const auto fb = m_frame.getGlobalBounds();
  drawSoftShadowRect(window, fb, /*layers*/ 1, /*step*/ 2.f);
  window.draw(m_frame);
  drawBevelAround(window, fb, constant::COL_HEADER);

  m_icon.draw(window);
  window.draw(m_name);
  window.draw(m_elo);

  // micro shadow & bevel on capture box
  const auto cb = m_captureBox.getGlobalBounds();
  drawSoftShadowRect(window, cb, /*layers*/ 1, /*step*/ 2.f);
  window.draw(m_captureBox);
  drawBevelAround(window, cb, m_captureBox.getFillColor());

  if (m_capturedPieces.empty()) {
    window.draw(m_noCaptures);
  } else {
    for (auto& piece : m_capturedPieces) {
      piece.draw(window);
    }
  }
}

void PlayerInfoView::addCapturedPiece(core::PieceType type, core::Color color) {
  m_capturedInfo.emplace_back(type, color);
  std::uint8_t numTypes = 6;
  std::string filename = constant::ASSET_PIECES_FILE_PATH + "/piece_" +
                         std::to_string(static_cast<std::uint8_t>(type) +
                                        numTypes * static_cast<std::uint8_t>(color)) +
                         ".png";
  const sf::Texture& texture = TextureTable::getInstance().get(filename);

  Entity piece(texture);
  piece.setScale(1.f, 1.f);
  m_capturedPieces.push_back(std::move(piece));
  layoutCaptured();
}

void PlayerInfoView::removeCapturedPiece() {
  if (!m_capturedPieces.empty()) {
    m_capturedPieces.pop_back();
    if (!m_capturedInfo.empty()) m_capturedInfo.pop_back();
    layoutCaptured();
  }
}

void PlayerInfoView::clearCapturedPieces() {
  m_capturedPieces.clear();
  m_capturedInfo.clear();
  layoutCaptured();
}

// Keep your special offsets; just ensure pieces fit and box hugs content.
void PlayerInfoView::layoutCaptured() {
  const float capH = std::clamp(kIconFrameSize - 6.f, kCapMinH, kCapMaxH);

  const float baseY = snapf(m_frame.getPosition().y + (kIconFrameSize - capH) * 0.5f);

  if (m_capturedPieces.empty()) {
    auto tb = m_noCaptures.getLocalBounds();
    const float boxW = tb.width + 2.f * kCapPad;
    const float baseX = snapf(m_boardCenter - boxW * 0.5f);
    m_captureBox.setSize({boxW, capH});
    m_captureBox.setPosition({baseX, baseY});

    const float tx = baseX + kCapPad;
    const float ty = baseY + (capH - tb.height) * 0.5f - tb.top;
    m_noCaptures.setPosition(snap({tx, ty}));
    return;
  }

  const float targetH = capH - 2.f * kCapPad;
  std::vector<sf::Vector2f> sizes;
  sizes.reserve(m_capturedPieces.size());
  float x = kCapPad;

  for (auto& piece : m_capturedPieces) {
    const auto orig = piece.getOriginalSize();
    if (orig.x <= 0.f || orig.y <= 0.f) {
      sizes.emplace_back(0.f, 0.f);
      continue;
    }

    const float s = (targetH / orig.y) * 1.1f;  // fit height (no overscale)
    piece.setScale(s, s);

    const auto size = piece.getCurrentSize();
    sizes.push_back(size);
    x += size.x * kPieceAdvance;
  }

  const float contentW = x + kCapPad - 4.f;
  const float baseX = snapf(m_boardCenter - contentW * 0.5f);
  m_captureBox.setSize({contentW, capH});
  m_captureBox.setPosition({baseX, baseY});

  float posX = kCapPad;
  for (std::size_t i = 0; i < m_capturedPieces.size(); ++i) {
    auto& piece = m_capturedPieces[i];
    auto size = sizes[i];
    if (size.x <= 0.f || size.y <= 0.f) continue;
    const float px = baseX + posX + 6.f;
    const float py = baseY + (capH - size.y) * 2.20f;  // (kept on purpose)
    piece.setPosition(snap({px, py}));
    posX += size.x * kPieceAdvance;
  }
}

}  // namespace lilia::view
