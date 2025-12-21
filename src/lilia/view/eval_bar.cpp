#include "lilia/view/eval_bar.hpp"

#include <SFML/Graphics/RectangleShape.hpp>
#include <SFML/Graphics/RenderWindow.hpp>
#include <SFML/Graphics/Text.hpp>
#include <SFML/Graphics/VertexArray.hpp>
#include <SFML/Window/Mouse.hpp>
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <string>

#include "lilia/engine/config.hpp"
#include "lilia/model/position.hpp"
#include "lilia/view/render_constants.hpp"
#include "lilia/view/texture_table.hpp"

namespace {

namespace constant = lilia::view::constant;

inline float snapf(float v) {
  return std::round(v);
}

inline sf::Color lighten(sf::Color c, int d) {
  auto clip = [](int x) { return std::clamp(x, 0, 255); };
  return sf::Color(clip(c.r + d), clip(c.g + d), clip(c.b + d), c.a);
}
inline sf::Color darken(sf::Color c, int d) {
  return lighten(c, -d);
}

// Soft 1–2px spread rectangle shadow
inline void drawSoftShadowRect(sf::RenderTarget& t, const sf::FloatRect& r, int layers = 1,
                               float step = 2.f) {
  for (int i = layers; i >= 1; --i) {
    float grow = static_cast<float>(i) * step;
    sf::RectangleShape s({r.width + 2.f * grow, r.height + 2.f * grow});
    s.setPosition(snapf(r.left - grow), snapf(r.top - grow));
    sf::Color sc = constant::COL_SHADOW_LIGHT;
    sc.a = static_cast<sf::Uint8>(22 * i);
    s.setFillColor(sc);
    t.draw(s);
  }
}

// Simple top→bottom vertical gradient fill
inline void drawVerticalGradientRect(sf::RenderTarget& t, const sf::FloatRect& r, sf::Color top,
                                     sf::Color bottom) {
  sf::VertexArray va(sf::TriangleStrip, 4);
  va[0].position = {r.left, r.top};
  va[1].position = {r.left + r.width, r.top};
  va[2].position = {r.left, r.top + r.height};
  va[3].position = {r.left + r.width, r.top + r.height};
  va[0].color = va[1].color = top;
  va[2].color = va[3].color = bottom;
  t.draw(va);
}

// Thin bevel ring (subtle)
inline void drawBevelAround(sf::RenderTarget& t, const sf::FloatRect& r, sf::Color base) {
  sf::RectangleShape top({r.width, 1.f});
  top.setPosition(snapf(r.left), snapf(r.top));
  top.setFillColor(lighten(base, 10));
  t.draw(top);

  sf::RectangleShape bottom({r.width, 1.f});
  bottom.setPosition(snapf(r.left), snapf(r.top + r.height - 1.f));
  bottom.setFillColor(darken(base, 12));
  t.draw(bottom);

  // hairline inside
  sf::RectangleShape inset({r.width - 2.f, r.height - 2.f});
  inset.setPosition(snapf(r.left + 1.f), snapf(r.top + 1.f));
  inset.setFillColor(sf::Color::Transparent);
  inset.setOutlineThickness(1.f);
  inset.setOutlineColor(constant::COL_BORDER_BEVEL);
  t.draw(inset);
}

}  // namespace

namespace lilia::view {

EvalBar::EvalBar() : EvalBar::Entity() {
  // base invisible sprite (kept)
  setTexture(TextureTable::getInstance().get(constant::STR_TEXTURE_TRANSPARENT));
  setScale(constant::EVAL_BAR_WIDTH, constant::EVAL_BAR_HEIGHT);
  setOriginToCenter();

  // background & white fill textures (kept)
  m_black_background.setTexture(TextureTable::getInstance().get(constant::STR_TEXTURE_EVAL_BLACK));
  m_white_fill_eval.setTexture(TextureTable::getInstance().get(constant::STR_TEXTURE_EVAL_WHITE));
  m_black_background.setScale(constant::EVAL_BAR_WIDTH, constant::EVAL_BAR_HEIGHT);
  m_white_fill_eval.setScale(constant::EVAL_BAR_WIDTH, constant::EVAL_BAR_HEIGHT);
  m_black_background.setOriginToCenter();
  m_white_fill_eval.setOriginToCenter();

  // font & labels
  m_font.loadFromFile(constant::STR_FILE_PATH_FONT);
  m_font.setSmooth(false);

  m_score_text.setFont(m_font);
  m_score_text.setCharacterSize(constant::EVAL_BAR_FONT_SIZE);
  m_score_text.setFillColor(constant::COL_SCORE_TEXT_DARK);  // default

  m_toggle_text.setFont(m_font);
  m_toggle_text.setCharacterSize(15);
  m_paletteListener =
      ColorPaletteManager::get().addListener([this]() { onPaletteChanged(); });
  onPaletteChanged();
}

EvalBar::~EvalBar() { ColorPaletteManager::get().removeListener(m_paletteListener); }

void EvalBar::setFlipped(bool flipped) {
  m_flipped = flipped;
  update(static_cast<int>(m_target_eval));
}

void EvalBar::setPosition(const Entity::Position& pos) {
  Entity::setPosition(pos);
  m_black_background.setPosition(getPosition());
  m_white_fill_eval.setPosition(getPosition());

  // compact pill centered below the bar
  const float btnW = static_cast<float>(constant::EVAL_BAR_WIDTH) * 0.90f;
  const float btnH = 24.f;
  const float toggleY = pos.y + static_cast<float>(constant::WINDOW_PX_SIZE) / 2.f +
                        (static_cast<float>(constant::SIDE_MARGIN) - btnH) * 0.5f;
  m_toggle_bounds = sf::FloatRect(pos.x - btnW * 0.5f, toggleY, btnW, btnH);
}

void EvalBar::render(sf::RenderWindow& window) {
  // --- Toggle button (always visible) with 3D pill look ---
  {
    sf::Vector2i mp = sf::Mouse::getPosition(window);
    sf::Vector2f mpos = window.mapPixelToCoords(mp);
    const bool hov = m_toggle_bounds.contains(mpos.x, mpos.y);

    // tiny shadow + gradient body
    drawSoftShadowRect(window, m_toggle_bounds, /*layers=*/1, /*step=*/2.f);

    sf::Color top = m_visible ? lighten(constant::COL_ACCENT, 30)
                              : lighten(constant::COL_HEADER, 10);
    sf::Color bot = m_visible ? darken(constant::COL_ACCENT, 25)
                              : darken(constant::COL_HEADER, 12);
    if (hov) {
      top = lighten(top, 12);
      bot = lighten(bot, 8);
    }
    drawVerticalGradientRect(window, m_toggle_bounds, top, bot);

    // bevel ring
    drawBevelAround(window, m_toggle_bounds,
                   m_visible ? constant::COL_ACCENT : constant::COL_HEADER);

    // label
    m_toggle_text.setString(m_visible ? "ON" : "OFF");
    auto tb = m_toggle_text.getLocalBounds();
    m_toggle_text.setOrigin(tb.left + tb.width / 2.f, tb.top + tb.height / 2.f);
    m_toggle_text.setFillColor(hov ? constant::COL_TEXT
                                  : (m_visible ? constant::COL_SCORE_TEXT_DARK : constant::COL_TEXT));
    m_toggle_text.setPosition(snapf(m_toggle_bounds.left + m_toggle_bounds.width / 2.f),
                              snapf(m_toggle_bounds.top + m_toggle_bounds.height / 2.f - 1.f));
    window.draw(m_toggle_text);
  }

  if (!m_visible) return;

  // --- Eval bar with micro-shadow, textures, zero-line, bevel & accents ---
  const float W = static_cast<float>(constant::EVAL_BAR_WIDTH);
  const float H = static_cast<float>(constant::EVAL_BAR_HEIGHT);
  const float left = snapf(getPosition().x - W * 0.5f);
  const float top = snapf(getPosition().y - H * 0.5f);
  const sf::FloatRect barRect(left, top, W, H);

  // soft shadow behind the bar
  drawSoftShadowRect(window, barRect, /*layers=*/1, /*step=*/2.f);

  // textured body
  draw(window);                     // base (transparent)
  m_black_background.draw(window);  // dark background
  m_white_fill_eval.draw(window);   // white fill scaled to eval

  // faint zero-line (center) for reference
  {
    sf::RectangleShape mid({W, 1.f});
    mid.setPosition(left, snapf(top + H * 0.5f));
    mid.setFillColor(constant::COL_BORDER);
    window.draw(mid);
  }

  // advantage accent strip (3px) on the advantaged side
  {
    const bool whiteAdv = (m_display_eval >= 0.f);
    sf::RectangleShape strip({W, 3.f});
    strip.setFillColor(whiteAdv ? constant::COL_WHITE_DIM : constant::COL_WHITE_FAINT);
    bool bottom = (whiteAdv != m_flipped);
    strip.setPosition(left, snapf(bottom ? top + H - 3.f : top));
    window.draw(strip);
  }

  // subtle bevel ring
  drawBevelAround(window, barRect, constant::COL_HEADER);

  // score text (already positioned/colored in update())
  if (m_has_result && m_result == "1/2-1/2") {
    sf::Text top = m_score_text;
    sf::Text bottom = m_score_text;
    auto bounds = m_score_text.getLocalBounds();
    const float lineHeight = bounds.height;
    const float gap = 2.f;
    const float xPos = getPosition().x;
    const float yCenter = getPosition().y;
    const float barHalfHeight = static_cast<float>(constant::EVAL_BAR_HEIGHT) * 0.5f;
    const float halfText = lineHeight / 2.f;
    float topY = yCenter - halfText - gap * 0.5f;
    float bottomY = yCenter + halfText + gap * 0.5f;
    const float minY = yCenter - barHalfHeight + halfText;
    const float maxY = yCenter + barHalfHeight - halfText;
    topY = std::clamp(topY, minY, maxY);
    bottomY = std::clamp(bottomY, minY, maxY);
    top.setPosition(snapf(xPos), snapf(topY));
    bottom.setPosition(snapf(xPos), snapf(bottomY));
    window.draw(top);
    window.draw(bottom);
  } else {
    window.draw(m_score_text);
  }
}

void EvalBar::update(int eval) {
  if (!m_has_result) {
    m_target_eval = static_cast<float>(eval);
    m_display_eval += (m_target_eval - m_display_eval) * 0.05f;
  }
  scaleToEval(m_display_eval);

  // score string
  if (m_has_result) {
    if (m_result == "1/2-1/2") {
      m_score_text.setString("1/2");
    } else {
      m_score_text.setString(m_result);
    }
  } else {
    int absEval = std::abs(static_cast<int>(m_display_eval));
    if (absEval >= engine::MATE_THR) {
      int moves = (engine::MATE - absEval) / 2;
      m_score_text.setString(std::string("M") + std::to_string(moves));
    } else {
      double val = std::abs(m_display_eval / 100.0);
      std::ostringstream ss;
      ss.setf(std::ios::fixed);
      ss << std::setprecision(1) << val;
      m_score_text.setString(ss.str());
    }
  }

  auto b = m_score_text.getLocalBounds();
  m_score_text.setOrigin(b.width / 2.f, b.height / 2.f);

  // place/contrast
  const float offset = 10.f;
  const float barHalfHeight = static_cast<float>(constant::EVAL_BAR_HEIGHT) * 0.5f;

  float xPos = getPosition().x;
  float yPos = getPosition().y;

  bool whiteAdv = (m_display_eval >= 0.f);
  if (m_has_result && m_result == "1/2-1/2") {
    m_score_text.setFillColor(constant::COL_SCORE_TEXT_DARK);
    yPos = getPosition().y;
  } else if (whiteAdv) {
    m_score_text.setFillColor(constant::COL_SCORE_TEXT_DARK);
    yPos += (m_flipped ? -barHalfHeight + offset : barHalfHeight - offset * 1.5f);
  } else {
    m_score_text.setFillColor(constant::COL_SCORE_TEXT_LIGHT);
    yPos += (m_flipped ? barHalfHeight - offset * 1.5f : -barHalfHeight + offset);
  }

  m_score_text.setPosition(snapf(xPos), snapf(yPos));
}

static float evalToWhitePct(float cp) {
  // smoother saturation so the bar doesn’t slam to extremes
  constexpr float k = 1000.0f;
  return 0.5f + 0.5f * std::tanh(cp / k);
}

void EvalBar::scaleToEval(float e) {
  const float H = static_cast<float>(constant::EVAL_BAR_HEIGHT);
  const float W = static_cast<float>(constant::EVAL_BAR_WIDTH);

  const float pctWhite = evalToWhitePct(e);
  const float whitePx = std::clamp(pctWhite * H, 0.0f, H);

  auto whiteOrig = m_white_fill_eval.getOriginalSize();
  if (whiteOrig.x <= 0.f || whiteOrig.y <= 0.f) return;

  // desired pixels / original pixels
  const float sx = W / whiteOrig.x;
  const float sy = whitePx / whiteOrig.y;
  m_white_fill_eval.setScale(sx, sy);

  // keep white anchored to the appropriate side (origin is center)
  const auto p = getPosition();
  float offset = (H - whitePx) * 0.5f;
  m_white_fill_eval.setPosition(Entity::Position{p.x, m_flipped ? p.y - offset : p.y + offset});

  // make sure background hugs full bar
  auto bgOrig = m_black_background.getOriginalSize();
  if (bgOrig.x > 0.f && bgOrig.y > 0.f) {
    m_black_background.setScale(W / bgOrig.x, H / bgOrig.y);
    m_black_background.setPosition(p);
  }
}

void EvalBar::setResult(const std::string& result) {
  m_has_result = true;
  m_result = result;
  if (result == "1-0") {
    m_display_eval = m_target_eval = static_cast<float>(engine::MATE);
  } else if (result == "0-1") {
    m_display_eval = m_target_eval = -static_cast<float>(engine::MATE);
  } else {
    m_display_eval = m_target_eval = 0.f;
  }
  update(static_cast<int>(m_display_eval));
}

void EvalBar::onPaletteChanged() {
  setTexture(TextureTable::getInstance().get(constant::STR_TEXTURE_TRANSPARENT));
  m_black_background.setTexture(
      TextureTable::getInstance().get(constant::STR_TEXTURE_EVAL_BLACK));
  m_white_fill_eval.setTexture(
      TextureTable::getInstance().get(constant::STR_TEXTURE_EVAL_WHITE));
  m_score_text.setFillColor(constant::COL_SCORE_TEXT_DARK);
  m_toggle_text.setFillColor(constant::COL_TEXT);
  // Refresh textures in case the underlying palette-dependent images changed.
  m_black_background.setTexture(
      TextureTable::getInstance().get(constant::STR_TEXTURE_EVAL_BLACK));
  m_white_fill_eval.setTexture(
      TextureTable::getInstance().get(constant::STR_TEXTURE_EVAL_WHITE));
}

void EvalBar::reset() {
  m_has_result = false;
  m_result.clear();
  m_display_eval = 0.f;
  m_target_eval = 0.f;
  m_score_text.setString("0.0");
  auto b = m_score_text.getLocalBounds();
  m_score_text.setOrigin(b.width / 2.f, b.height / 2.f);
  scaleToEval(0.f);
}

void EvalBar::toggleVisibility() {
  m_visible = !m_visible;
}

bool EvalBar::isOnToggle(core::MousePos mousePos) const {
  return m_toggle_bounds.contains(static_cast<float>(mousePos.x), static_cast<float>(mousePos.y));
}

}  // namespace lilia::view
