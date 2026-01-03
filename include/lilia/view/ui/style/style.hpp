#pragma once

#include <SFML/Graphics.hpp>
#include <algorithm>
#include <cmath>

namespace lilia::view::ui
{
  // ----------------------------
  // Geometry / Pixel snapping
  // ----------------------------
  inline float snapf(float v) { return std::round(v); }

  inline sf::Vector2f snap(sf::Vector2f v) { return {snapf(v.x), snapf(v.y)}; }

  inline sf::FloatRect offsetRect(sf::FloatRect r, sf::Vector2f off)
  {
    r.left += off.x;
    r.top += off.y;
    return r;
  }

  // ----------------------------
  // Text layout helpers
  // ----------------------------
  inline void centerText(sf::Text &t, const sf::FloatRect &box, float dy = 0.f)
  {
    auto b = t.getLocalBounds();
    t.setOrigin(b.left + b.width / 2.f, b.top + b.height / 2.f);
    t.setPosition(snapf(box.left + box.width / 2.f),
                  snapf(box.top + box.height / 2.f + dy));
  }

  inline void leftCenterText(sf::Text &t, const sf::FloatRect &box, float padX, float dy = 0.f)
  {
    auto b = t.getLocalBounds();
    t.setOrigin(b.left, b.top + b.height / 2.f);
    t.setPosition(snapf(box.left + padX),
                  snapf(box.top + box.height / 2.f + dy));
  }

  // ----------------------------
  // Color helpers
  // ----------------------------
  inline sf::Color lighten(sf::Color c, int d)
  {
    auto clip = [](int x)
    { return std::clamp(x, 0, 255); };
    return sf::Color(clip(c.r + d), clip(c.g + d), clip(c.b + d), c.a);
  }

  inline sf::Color darken(sf::Color c, int d) { return lighten(c, -d); }

  inline sf::Color lerpColor(sf::Color a, sf::Color b, float t)
  {
    auto L = [&](int A, int B)
    {
      return static_cast<sf::Uint8>(std::lround(A + (B - A) * t));
    };
    return sf::Color(L(a.r, b.r), L(a.g, b.g), L(a.b, b.b), L(a.a, b.a));
  }

  // ----------------------------
  // Gradient fills
  // ----------------------------
  inline void drawVerticalGradientRect(sf::RenderTarget &rt, const sf::FloatRect &r,
                                       sf::Color top, sf::Color bottom)
  {
    sf::VertexArray va(sf::TriangleStrip, 4);
    va[0].position = {r.left, r.top};
    va[1].position = {r.left + r.width, r.top};
    va[2].position = {r.left, r.top + r.height};
    va[3].position = {r.left + r.width, r.top + r.height};
    va[0].color = va[1].color = top;
    va[2].color = va[3].color = bottom;
    rt.draw(va);
  }

  // Backwards-compatible wrapper (existing call sites keep working).
  inline void drawVerticalGradient(sf::RenderTarget &rt, sf::Vector2u size,
                                   sf::Color top, sf::Color bottom)
  {
    drawVerticalGradientRect(rt,
                             sf::FloatRect(0.f, 0.f, float(size.x), float(size.y)),
                             top, bottom);
  }

  // ----------------------------
  // Shadows
  // ----------------------------
  inline void drawPanelShadow(sf::RenderTarget &rt, const sf::FloatRect &r)
  {
    for (int i = 3; i >= 1; --i)
    {
      float grow = float(i) * 6.f;
      sf::RectangleShape s({r.width + 2.f * grow, r.height + 2.f * grow});
      s.setPosition(snap({r.left - grow, r.top - grow}));
      s.setFillColor(sf::Color(0, 0, 0, sf::Uint8(28 * i)));
      rt.draw(s);
    }
  }

  // Soft shadow with controllable tint/alpha (single implementation; no duplicate).
  inline void drawSoftShadowRect(sf::RenderTarget &rt, const sf::FloatRect &r, sf::Color shadow,
                                 int layers = 1, float step = 2.f)
  {
    const int safeLayers = std::max(1, layers);
    for (int i = safeLayers; i >= 1; --i)
    {
      float grow = float(i) * step;
      sf::RectangleShape s({r.width + 2.f * grow, r.height + 2.f * grow});
      s.setPosition(snap({r.left - grow, r.top - grow}));

      sf::Color c = shadow;
      c.a = static_cast<sf::Uint8>(
          shadow.a * (0.35f + 0.65f * (float(i) / float(safeLayers))));
      s.setFillColor(c);
      rt.draw(s);
    }
  }

  // ----------------------------
  // Bevel / Frames
  // ----------------------------
  // Clean bevel button (paints body).
  inline void drawBevelButton(sf::RenderTarget &t, const sf::FloatRect &r, sf::Color base,
                              bool hovered, bool pressed)
  {
    sf::RectangleShape body({r.width, r.height});
    body.setPosition(snap({r.left, r.top}));

    sf::Color bodyCol = base;
    if (hovered && !pressed)
      bodyCol = lighten(bodyCol, 8);
    if (pressed)
      bodyCol = darken(bodyCol, 6);

    body.setFillColor(bodyCol);
    t.draw(body);

    sf::RectangleShape top({r.width, 1.f});
    top.setPosition(snap({r.left, r.top}));
    top.setFillColor(lighten(bodyCol, 24));
    t.draw(top);

    sf::RectangleShape bot({r.width, 1.f});
    bot.setPosition(snap({r.left, r.top + r.height - 1.f}));
    bot.setFillColor(darken(bodyCol, 24));
    t.draw(bot);

    sf::RectangleShape inset({r.width - 2.f, r.height - 2.f});
    inset.setPosition(snap({r.left + 1.f, r.top + 1.f}));
    inset.setFillColor(sf::Color::Transparent);
    inset.setOutlineThickness(1.f);
    inset.setOutlineColor(darken(bodyCol, 18));
    t.draw(inset);
  }

  inline void drawAccentInset(sf::RenderTarget &t, const sf::FloatRect &r, sf::Color accent)
  {
    sf::RectangleShape inset({r.width - 2.f, r.height - 2.f});
    inset.setPosition(snap({r.left + 1.f, r.top + 1.f}));
    inset.setFillColor(sf::Color::Transparent);
    inset.setOutlineThickness(1.f);
    inset.setOutlineColor(accent);
    t.draw(inset);
  }

  // Bevel ring without painting the body (useful over gradients/textures).
  inline void drawBevelFrame(sf::RenderTarget &rt, const sf::FloatRect &r, sf::Color base,
                             sf::Color bevelBorder)
  {
    sf::RectangleShape top({r.width, 1.f});
    top.setPosition(snap({r.left, r.top}));
    top.setFillColor(lighten(base, 10));
    rt.draw(top);

    sf::RectangleShape bottom({r.width, 1.f});
    bottom.setPosition(snap({r.left, r.top + r.height - 1.f}));
    bottom.setFillColor(darken(base, 12));
    rt.draw(bottom);

    sf::RectangleShape inset({r.width - 2.f, r.height - 2.f});
    inset.setPosition(snap({r.left + 1.f, r.top + 1.f}));
    inset.setFillColor(sf::Color::Transparent);
    inset.setOutlineThickness(1.f);
    inset.setOutlineColor(bevelBorder);
    rt.draw(inset);
  }

} // namespace lilia::view::ui
