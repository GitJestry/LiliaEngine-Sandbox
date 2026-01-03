#pragma once
#include <SFML/Graphics.hpp>

namespace lilia::view::ui
{

  inline sf::FloatRect anchoredCenter(sf::Vector2u ws, sf::Vector2f size)
  {
    float x = (float(ws.x) - size.x) * 0.5f;
    float y = (float(ws.y) - size.y) * 0.5f;
    return {x, y, size.x, size.y};
  }

  inline sf::FloatRect inset(sf::FloatRect r, float pad)
  {
    r.left += pad;
    r.top += pad;
    r.width -= 2.f * pad;
    r.height -= 2.f * pad;
    return r;
  }

  inline sf::FloatRect rowConsume(sf::FloatRect &r, float h, float gap = 0.f)
  {
    sf::FloatRect out{r.left, r.top, r.width, h};
    r.top += h + gap;
    r.height -= h + gap;
    return out;
  }

  inline sf::FloatRect colConsume(sf::FloatRect &r, float w, float gap = 0.f)
  {
    sf::FloatRect out{r.left, r.top, w, r.height};
    r.left += w + gap;
    r.width -= w + gap;
    return out;
  }

  inline sf::FloatRect anchoredTopCenter(sf::Vector2u ws, sf::Vector2f size, float top = 72.f)
  {
    return {(float(ws.x) - size.x) * 0.5f, top, size.x, size.y};
  }

} // namespace lilia::view::ui
