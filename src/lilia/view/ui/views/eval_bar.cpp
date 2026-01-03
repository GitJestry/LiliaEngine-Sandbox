#include "lilia/view/ui/views/eval_bar.hpp"

#include <SFML/Graphics/RectangleShape.hpp>
#include <SFML/Graphics/RenderWindow.hpp>
#include <SFML/Graphics/Text.hpp>
#include <SFML/Window/Mouse.hpp>

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <string>

#include "lilia/engine/config.hpp"
#include "lilia/view/ui/style/palette_cache.hpp"
#include "lilia/view/ui/render/render_constants.hpp"
#include "lilia/view/ui/render/texture_table.hpp"
#include "lilia/view/ui/style/style.hpp"

namespace lilia::view
{

  namespace
  {

    inline float snapf(float v) { return ui::snapf(v); }

    static float evalToWhitePct(float cp)
    {
      constexpr float k = 1000.0f;
      return 0.5f + 0.5f * std::tanh(cp / k);
    }

  } // namespace

  EvalBar::EvalBar() : EvalBar::Entity()
  {
    setTexture(TextureTable::getInstance().get(std::string{constant::tex::TRANSPARENT}));
    setScale(constant::EVAL_BAR_WIDTH, constant::EVAL_BAR_HEIGHT);
    setOriginToCenter();

    m_black_background.setTexture(TextureTable::getInstance().get(std::string{constant::tex::EVAL_BLACK}));
    m_white_fill_eval.setTexture(TextureTable::getInstance().get(std::string{constant::tex::EVAL_WHITE}));

    m_black_background.setScale(constant::EVAL_BAR_WIDTH, constant::EVAL_BAR_HEIGHT);
    m_white_fill_eval.setScale(constant::EVAL_BAR_WIDTH, constant::EVAL_BAR_HEIGHT);
    m_black_background.setOriginToCenter();
    m_white_fill_eval.setOriginToCenter();

    m_font.loadFromFile(std::string{constant::path::FONT});
    m_font.setSmooth(false);

    m_score_text.setFont(m_font);
    m_score_text.setCharacterSize(constant::EVAL_BAR_FONT_SIZE);

    m_toggle_text.setFont(m_font);
    m_toggle_text.setCharacterSize(15);

    refreshPaletteDerivedColors();
  }

  void EvalBar::setFlipped(bool flipped)
  {
    m_flipped = flipped;
    update(static_cast<int>(m_target_eval));
  }

  void EvalBar::setPosition(const Entity::Position &pos)
  {
    Entity::setPosition(pos);
    m_black_background.setPosition(getPosition());
    m_white_fill_eval.setPosition(getPosition());

    const float btnW = static_cast<float>(constant::EVAL_BAR_WIDTH) * 0.90f;
    const float btnH = 24.f;

    const float toggleY = pos.y + static_cast<float>(constant::WINDOW_PX_SIZE) / 2.f +
                          (static_cast<float>(constant::SIDE_MARGIN) - btnH) * 0.5f;

    m_toggle_bounds = sf::FloatRect(pos.x - btnW * 0.5f, toggleY, btnW, btnH);
  }

  void EvalBar::refreshPaletteDerivedColors()
  {
    const auto pal = PaletteCache::get().palette();

    // score text color follows m_scoreUseDarkText (computed in update())
    m_score_text.setFillColor(m_scoreUseDarkText ? pal[ColorId::COL_SCORE_TEXT_DARK]
                                                 : pal[ColorId::COL_SCORE_TEXT_LIGHT]);

    // toggle text base color is set per-frame in render() (hover/visible); keep a safe default here
    m_toggle_text.setFillColor(pal[ColorId::COL_TEXT]);
  }

  void EvalBar::render(sf::RenderWindow &window)
  {
    const auto pal = PaletteCache::get().palette();
    refreshPaletteDerivedColors();

    // --- Toggle button (always visible) ---
    {
      sf::Vector2i mp = sf::Mouse::getPosition(window);
      sf::Vector2f mpos = window.mapPixelToCoords(mp);
      const bool hov = m_toggle_bounds.contains(mpos.x, mpos.y);

      ui::drawSoftShadowRect(window, m_toggle_bounds, pal[ColorId::COL_SHADOW_LIGHT], 1, 2.f);

      sf::Color top = m_visible ? ui::lighten(pal[ColorId::COL_ACCENT], 30)
                                : ui::lighten(pal[ColorId::COL_HEADER], 10);
      sf::Color bot = m_visible ? ui::darken(pal[ColorId::COL_ACCENT], 25)
                                : ui::darken(pal[ColorId::COL_HEADER], 12);

      if (hov)
      {
        top = ui::lighten(top, 12);
        bot = ui::lighten(bot, 8);
      }

      ui::drawVerticalGradientRect(window, m_toggle_bounds, top, bot);

      const sf::Color bevelBase = m_visible ? pal[ColorId::COL_ACCENT] : pal[ColorId::COL_HEADER];
      ui::drawBevelFrame(window, m_toggle_bounds, bevelBase, pal[ColorId::COL_BORDER_BEVEL]);

      m_toggle_text.setString(m_visible ? "ON" : "OFF");
      auto tb = m_toggle_text.getLocalBounds();
      m_toggle_text.setOrigin(tb.left + tb.width / 2.f, tb.top + tb.height / 2.f);

      const sf::Color labelCol =
          hov ? pal[ColorId::COL_TEXT]
              : (m_visible ? pal[ColorId::COL_SCORE_TEXT_DARK] : pal[ColorId::COL_TEXT]);

      m_toggle_text.setFillColor(labelCol);
      m_toggle_text.setPosition(snapf(m_toggle_bounds.left + m_toggle_bounds.width / 2.f),
                                snapf(m_toggle_bounds.top + m_toggle_bounds.height / 2.f - 1.f));
      window.draw(m_toggle_text);
    }

    if (!m_visible)
      return;

    // --- Eval bar ---
    const float W = static_cast<float>(constant::EVAL_BAR_WIDTH);
    const float H = static_cast<float>(constant::EVAL_BAR_HEIGHT);
    const float left = snapf(getPosition().x - W * 0.5f);
    const float top = snapf(getPosition().y - H * 0.5f);
    const sf::FloatRect barRect(left, top, W, H);

    ui::drawSoftShadowRect(window, barRect, pal[ColorId::COL_SHADOW_LIGHT], 1, 2.f);

    draw(window);
    m_black_background.draw(window);
    m_white_fill_eval.draw(window);

    // zero-line
    {
      sf::RectangleShape mid({W, 1.f});
      mid.setPosition(left, snapf(top + H * 0.5f));
      mid.setFillColor(pal[ColorId::COL_BORDER]);
      window.draw(mid);
    }

    // advantage strip
    {
      const bool whiteAdv = (m_display_eval >= 0.f);
      sf::RectangleShape strip({W, 3.f});
      strip.setFillColor(whiteAdv ? pal[ColorId::COL_WHITE_DIM] : pal[ColorId::COL_WHITE_FAINT]);
      const bool bottom = (whiteAdv != m_flipped);
      strip.setPosition(left, snapf(bottom ? top + H - 3.f : top));
      window.draw(strip);
    }

    ui::drawBevelFrame(window, barRect, pal[ColorId::COL_HEADER], pal[ColorId::COL_BORDER_BEVEL]);

    // score text (position computed in update())
    if (m_has_result && m_result == "1/2-1/2")
    {
      sf::Text topT = m_score_text;
      sf::Text botT = m_score_text;

      auto bounds = m_score_text.getLocalBounds();
      const float lineHeight = bounds.height;
      const float gap = 2.f;

      const float xPos = getPosition().x;
      const float yCenter = getPosition().y;
      const float barHalfHeight = H * 0.5f;
      const float halfText = lineHeight / 2.f;

      float topY = yCenter - halfText - gap * 0.5f;
      float bottomY = yCenter + halfText + gap * 0.5f;

      const float minY = yCenter - barHalfHeight + halfText;
      const float maxY = yCenter + barHalfHeight - halfText;

      topY = std::clamp(topY, minY, maxY);
      bottomY = std::clamp(bottomY, minY, maxY);

      topT.setPosition(snapf(xPos), snapf(topY));
      botT.setPosition(snapf(xPos), snapf(bottomY));
      window.draw(topT);
      window.draw(botT);
    }
    else
    {
      window.draw(m_score_text);
    }
  }

  void EvalBar::update(int eval)
  {
    if (!m_has_result)
    {
      m_target_eval = static_cast<float>(eval);
      m_display_eval += (m_target_eval - m_display_eval) * 0.05f;
    }
    scaleToEval(m_display_eval);

    // score string
    if (m_has_result)
    {
      m_score_text.setString(m_result == "1/2-1/2" ? "1/2" : m_result);
    }
    else
    {
      int absEval = std::abs(static_cast<int>(m_display_eval));
      if (absEval >= engine::MATE_THR)
      {
        int moves = (engine::MATE - absEval) / 2;
        m_score_text.setString(std::string("M") + std::to_string(moves));
      }
      else
      {
        double val = std::abs(m_display_eval / 100.0);
        std::ostringstream ss;
        ss.setf(std::ios::fixed);
        ss << std::setprecision(1) << val;
        m_score_text.setString(ss.str());
      }
    }

    auto b = m_score_text.getLocalBounds();
    m_score_text.setOrigin(b.width / 2.f, b.height / 2.f);

    const float offset = 10.f;
    const float barHalfHeight = static_cast<float>(constant::EVAL_BAR_HEIGHT) * 0.5f;

    float xPos = getPosition().x;
    float yPos = getPosition().y;

    const bool whiteAdv = (m_display_eval >= 0.f);

    if (m_has_result && m_result == "1/2-1/2")
    {
      m_scoreUseDarkText = true;
      yPos = getPosition().y;
    }
    else if (whiteAdv)
    {
      m_scoreUseDarkText = true;
      yPos += (m_flipped ? -barHalfHeight + offset : barHalfHeight - offset * 1.5f);
    }
    else
    {
      m_scoreUseDarkText = false;
      yPos += (m_flipped ? barHalfHeight - offset * 1.5f : -barHalfHeight + offset);
    }

    m_score_text.setPosition(snapf(xPos), snapf(yPos));
    refreshPaletteDerivedColors();
  }

  void EvalBar::scaleToEval(float e)
  {
    const float H = static_cast<float>(constant::EVAL_BAR_HEIGHT);
    const float W = static_cast<float>(constant::EVAL_BAR_WIDTH);

    const float pctWhite = evalToWhitePct(e);
    const float whitePx = std::clamp(pctWhite * H, 0.0f, H);

    auto whiteOrig = m_white_fill_eval.getOriginalSize();
    if (whiteOrig.x <= 0.f || whiteOrig.y <= 0.f)
      return;

    const float sx = W / whiteOrig.x;
    const float sy = whitePx / whiteOrig.y;
    m_white_fill_eval.setScale(sx, sy);

    const auto p = getPosition();
    float off = (H - whitePx) * 0.5f;
    m_white_fill_eval.setPosition(Entity::Position{p.x, m_flipped ? p.y - off : p.y + off});

    auto bgOrig = m_black_background.getOriginalSize();
    if (bgOrig.x > 0.f && bgOrig.y > 0.f)
    {
      m_black_background.setScale(W / bgOrig.x, H / bgOrig.y);
      m_black_background.setPosition(p);
    }
  }

  void EvalBar::setResult(const std::string &result)
  {
    m_has_result = true;
    m_result = result;

    if (result == "1-0")
    {
      m_display_eval = m_target_eval = static_cast<float>(engine::MATE);
    }
    else if (result == "0-1")
    {
      m_display_eval = m_target_eval = -static_cast<float>(engine::MATE);
    }
    else
    {
      m_display_eval = m_target_eval = 0.f;
    }

    update(static_cast<int>(m_display_eval));
  }

  void EvalBar::reset()
  {
    m_has_result = false;
    m_result.clear();
    m_display_eval = 0.f;
    m_target_eval = 0.f;

    m_score_text.setString("0.0");
    auto b = m_score_text.getLocalBounds();
    m_score_text.setOrigin(b.width / 2.f, b.height / 2.f);

    scaleToEval(0.f);
    update(0);
  }

  void EvalBar::toggleVisibility() { m_visible = !m_visible; }

  bool EvalBar::isOnToggle(core::MousePos mousePos) const
  {
    return m_toggle_bounds.contains(static_cast<float>(mousePos.x), static_cast<float>(mousePos.y));
  }

} // namespace lilia::view
