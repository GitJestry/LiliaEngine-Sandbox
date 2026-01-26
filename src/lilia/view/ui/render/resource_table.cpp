#include "lilia/view/ui/render/resource_table.hpp"

#include <SFML/Graphics.hpp>
#include <stdexcept>
#include <iostream>
#include <filesystem>

#include "lilia/view/ui/style/palette_cache.hpp"
#include "lilia/view/ui/render/render_constants.hpp"

namespace lilia::view
{

  ResourceTable &ResourceTable::getInstance()
  {
    static ResourceTable instance;
    return instance;
  }

  ResourceTable::ResourceTable()
  {
    // Initial build
    preLoad();

    // React to palette changes via the single, standardized access point.
    m_paletteListenerId = PaletteCache::get().addListener([this]
                                                          { reloadForPalette(); });
  }
  ResourceTable::~ResourceTable()
  {
    if (m_paletteListenerId != 0)
    {
      PaletteCache::get().removeListener(m_paletteListenerId);
    }
  }

  void ResourceTable::reloadForPalette()
  {
    preLoad();
  }

  void ResourceTable::load(std::string_view name, const sf::Color &color, sf::Vector2u size)
  {
    sf::Texture &texture = m_textures[std::string{name}];
    sf::Image image;
    image.create(size.x, size.y, color);
    texture.loadFromImage(image);
  }

  [[nodiscard]] const sf::Texture &ResourceTable::getTexture(const std::string &filename)
  {
    auto it = m_textures.find(filename);
    if (it != m_textures.end())
      return it->second;

    sf::Texture texture;
    if (!texture.loadFromFile(filename))
    {
      throw std::runtime_error("Error when loading texture: " + filename);
    }
    m_textures[filename] = std::move(texture);
    return m_textures[filename];
  }

  [[nodiscard]] const sf::Texture &ResourceTable::getAssetTexture(const std::string &filename)
  {
    auto it = m_textures.find(filename);
    if (it != m_textures.end())
      return it->second;

    sf::Texture texture;

    for (auto dir : {constant::path::ICONS_DIR, constant::path::PIECES_DIR})
    {
      std::filesystem::path p = std::filesystem::path(std::string(dir)) / filename;
      if (!std::filesystem::exists(p))
        continue;

      if (texture.loadFromFile(std::string{dir} + "/" + filename))
      {
        m_textures[filename] = std::move(texture);
        return m_textures[filename];
      }
    }
    throw std::runtime_error("Error when loading asset: " + filename);
  }

  [[nodiscard]] const sf::Image &ResourceTable::getImage(const std::string &filename)
  {
    auto it = m_images.find(filename);
    if (it != m_images.end())
      return it->second;

    sf::Image img;
    for (auto dir : {constant::path::ICONS_DIR})
    {
      if (img.loadFromFile(std::string{dir} + "/" + filename))
      {
        m_images[filename] = std::move(img);
        return m_images[filename];
      }
    }
    throw std::runtime_error("Error when loading image: " + filename);
  }

  namespace
  {

    static const char *captureFrag = R"(
uniform vec2 resolution;
uniform vec4 color;
uniform float centerR;
uniform float halfThickness;
uniform float softness;
uniform float innerShade;

void main()
{
    vec2 uv = gl_FragCoord.xy / resolution;
    vec2 c = vec2(0.5, 0.5);
    float d = distance(uv, c);

    float distFromRing = abs(d - centerR);
    float edge = smoothstep(halfThickness, halfThickness - softness, distFromRing);
    float ringMask = clamp(edge, 0.0, 1.0);

    float shade = mix(1.0, innerShade, smoothstep(0.0, halfThickness, (centerR - d)));

    float alpha = color.a * ringMask;
    vec3 rgb = color.rgb * shade;

    gl_FragColor = vec4(rgb, alpha);
}
)";

    [[nodiscard]] sf::Texture makeCaptureCircleTexture(unsigned int size, sf::Color marker)
    {
      sf::RenderTexture rt;
      rt.create(size, size);
      rt.clear(sf::Color::Transparent);

      sf::Shader shader;
      bool shaderOk = shader.loadFromMemory(captureFrag, sf::Shader::Fragment);

      if (!shaderOk)
      {
        float radius = size * 0.45f;
        float thickness = size * 0.1f;
        sf::CircleShape ring(radius);
        ring.setOrigin(radius, radius);
        ring.setPosition(size * 0.5f, size * 0.5f);
        ring.setFillColor(sf::Color::Transparent);
        ring.setOutlineThickness(-thickness);
        ring.setOutlineColor(marker);
        rt.draw(ring, sf::BlendAlpha);
        rt.display();
        return rt.getTexture();
      }

      float outerR_px = size * 0.45f;
      float thickness_px = size * 0.11f;
      float centerR = outerR_px / (float)size;
      float halfThickness = (thickness_px * 0.5f) / (float)size;
      float softness = 3.0f / (float)size;

      sf::Glsl::Vec4 col(marker.r / 255.f, marker.g / 255.f, marker.b / 255.f, marker.a / 255.f);

      shader.setUniform("resolution", sf::Glsl::Vec2((float)size, (float)size));
      shader.setUniform("color", col);
      shader.setUniform("centerR", centerR);
      shader.setUniform("halfThickness", halfThickness);
      shader.setUniform("softness", softness);
      shader.setUniform("innerShade", 0.92f);

      sf::RectangleShape quad(sf::Vector2f((float)size, (float)size));
      quad.setPosition(0.f, 0.f);
      rt.draw(quad, &shader);

      rt.display();
      return rt.getTexture();
    }

    static const char *dotFrag = R"(
uniform vec2 resolution;
uniform vec4 color;
uniform float radius;
uniform float softness;
uniform float coreBoost;
uniform float highlight;

void main()
{
    vec2 uv = gl_FragCoord.xy / resolution;
    vec2 c = vec2(0.5, 0.5);
    float d = distance(uv, c);

    float a = 1.0 - smoothstep(radius - softness, radius + softness, d);
    a = pow(a, 1.2);

    float core = 1.0 + coreBoost * (1.0 - smoothstep(0.0, radius * 0.9, d));

    float h = 1.0 - smoothstep(0.0, radius * 0.5, d);
    float highlightMask = pow(h, 3.0) * highlight;

    vec3 rgb = color.rgb * core + vec3(highlightMask);
    float alpha = color.a * a;

    gl_FragColor = vec4(rgb, alpha);
}
)";

    [[nodiscard]] sf::Texture makeAttackDotTexture(unsigned int size, sf::Color marker)
    {
      sf::RenderTexture rt;
      rt.create(size, size);
      rt.clear(sf::Color::Transparent);

      sf::Shader shader;
      bool shaderOk = shader.loadFromMemory(dotFrag, sf::Shader::Fragment);

      if (!shaderOk)
      {
        float maxRadius = size * 0.35f;
        sf::CircleShape core(maxRadius);
        core.setOrigin(maxRadius, maxRadius);
        core.setPosition(size * 0.5f, size * 0.5f);
        core.setFillColor(marker);
        rt.draw(core, sf::BlendAlpha);
        rt.display();
        return rt.getTexture();
      }

      float maxRadius_px = size * 0.35f;
      float radius_frac = maxRadius_px / (float)size;
      float softness = 3.0f / (float)size;

      sf::Glsl::Vec4 col(marker.r / 255.f, marker.g / 255.f, marker.b / 255.f, marker.a / 255.f);

      shader.setUniform("resolution", sf::Glsl::Vec2((float)size, (float)size));
      shader.setUniform("color", col);
      shader.setUniform("radius", radius_frac);
      shader.setUniform("softness", softness);
      shader.setUniform("coreBoost", 0.08f);
      shader.setUniform("highlight", 0.18f);

      sf::RectangleShape quad(sf::Vector2f((float)size, (float)size));
      quad.setPosition(0.f, 0.f);
      rt.draw(quad, &shader);

      rt.display();
      return rt.getTexture();
    }

    [[nodiscard]] sf::Texture makeSquareHoverTexture(unsigned int size, sf::Color outline)
    {
      sf::RenderTexture rt;
      rt.create(size, size);
      rt.clear(sf::Color::Transparent);

      float thickness = size / 8.f;
      sf::RectangleShape rect(sf::Vector2f(size - thickness, size - thickness));
      rect.setPosition(thickness / 2.f, thickness / 2.f);
      rect.setFillColor(sf::Color::Transparent);
      rect.setOutlineColor(outline);
      rect.setOutlineThickness(thickness);

      rt.draw(rect);
      rt.display();
      return rt.getTexture();
    }

    // (Your rounded rect + shadow code can remain as-is; just pass colors in via parameters
    // when you create them. Keeping your originals, but removing constant::COL_* defaults.)

    static const char *roundedRectFrag = R"SHADER(
#version 120
uniform vec2 resolution;
uniform float radius;
uniform float softness;
uniform vec4 color;

void main()
{
    vec2 coord = gl_TexCoord[0].xy;
    vec2 uv = coord / resolution;

    vec2 pos = uv * resolution - 0.5 * resolution;
    vec2 halfSize = 0.5 * resolution;

    vec2 q = abs(pos) - (halfSize - vec2(radius));
    vec2 qpos = max(q, vec2(0.0));
    float dist = length(qpos) - radius;

    float edge0 = -softness;
    float edge1 = softness;
    float a = 1.0 - smoothstep(edge0, edge1, dist);
    a = clamp(a, 0.0, 1.0);

    float innerShade = mix(1.0, 0.98, smoothstep(-radius*0.6, 0.0, dist));

    vec3 rgb = color.rgb * innerShade;
    float alpha = color.a * a;

    gl_FragColor = vec4(rgb, alpha);
}
)SHADER";

    static const char *shadowFrag = R"SHADER(
#version 120
uniform vec2 resolution;
uniform vec2 rectSize;
uniform float radius;
uniform float blur;
uniform float offsetY;
uniform vec4 shadowColor;

void main()
{
    vec2 coord = gl_TexCoord[0].xy;
    vec2 uv = coord / resolution;
    vec2 pos = uv * resolution - 0.5 * resolution - vec2(0.0, -offsetY);

    vec2 halfSize = 0.5 * rectSize;
    vec2 q = abs(pos) - (halfSize - vec2(radius));
    vec2 qpos = max(q, vec2(0.0));
    float dist = length(qpos) - radius;

    float a = 1.0 - smoothstep(0.0, blur, dist);
    a = clamp(pow(a, 1.1), 0.0, 1.0);

    vec3 rgb = shadowColor.rgb;
    float alpha = shadowColor.a * a;

    gl_FragColor = vec4(rgb, alpha);
}
)SHADER";

    static sf::VertexArray makeFullQuadVA(unsigned int width, unsigned int height)
    {
      sf::VertexArray va(sf::Quads, 4);
      va[0].position = sf::Vector2f(0.f, 0.f);
      va[1].position = sf::Vector2f((float)width, 0.f);
      va[2].position = sf::Vector2f((float)width, (float)height);
      va[3].position = sf::Vector2f(0.f, (float)height);

      va[0].texCoords = sf::Vector2f(0.f, 0.f);
      va[1].texCoords = sf::Vector2f((float)width, 0.f);
      va[2].texCoords = sf::Vector2f((float)width, (float)height);
      va[3].texCoords = sf::Vector2f(0.f, (float)height);
      return va;
    }

    [[nodiscard]] sf::Texture makeRoundedRectTexture(unsigned int width, unsigned int height,
                                                     float radius_px, sf::Color fillColor,
                                                     float softness_px)
    {
      sf::RenderTexture rt;
      rt.create(width, height);
      rt.clear(sf::Color::Transparent);

      sf::Shader shader;
      bool ok = shader.loadFromMemory(roundedRectFrag, sf::Shader::Fragment);

      if (ok)
      {
        shader.setUniform("resolution", sf::Glsl::Vec2((float)width, (float)height));
        shader.setUniform("radius", radius_px);
        shader.setUniform("softness", softness_px);
        shader.setUniform("color", sf::Glsl::Vec4(fillColor.r / 255.f, fillColor.g / 255.f,
                                                  fillColor.b / 255.f, fillColor.a / 255.f));

        sf::VertexArray quad = makeFullQuadVA(width, height);
        rt.draw(quad, &shader);
        rt.display();

        sf::Texture tex = rt.getTexture();
        tex.setSmooth(false);
        tex.setRepeated(false);
        return tex;
      }

      // Fallback
      float cx = width * 0.5f, cy = height * 0.5f;
      sf::RectangleShape body(
          sf::Vector2f((float)width - 2.f * radius_px, (float)height - 2.f * radius_px));
      body.setOrigin(body.getSize() * 0.5f);
      body.setPosition(cx, cy);
      body.setFillColor(fillColor);
      rt.draw(body);

      sf::CircleShape corner(radius_px);
      corner.setFillColor(fillColor);
      corner.setOrigin(radius_px, radius_px);
      corner.setPosition(radius_px, radius_px);
      rt.draw(corner);
      corner.setPosition((float)width - radius_px, radius_px);
      rt.draw(corner);
      corner.setPosition(radius_px, (float)height - radius_px);
      rt.draw(corner);
      corner.setPosition((float)width - radius_px, (float)height - radius_px);
      rt.draw(corner);

      rt.display();
      sf::Texture tex = rt.getTexture();
      tex.setSmooth(false);
      tex.setRepeated(false);
      return tex;
    }

    [[nodiscard]] sf::Texture makeRoundedRectShadowTexture(unsigned int width, unsigned int height,
                                                           float rectWidth_px, float rectHeight_px,
                                                           float radius_px, float blur_px,
                                                           sf::Color shadowColor, float offsetY_px)
    {
      sf::RenderTexture rt;
      rt.create(width, height);
      rt.clear(sf::Color::Transparent);

      sf::Shader shader;
      bool ok = shader.loadFromMemory(shadowFrag, sf::Shader::Fragment);

      if (ok)
      {
        shader.setUniform("resolution", sf::Glsl::Vec2((float)width, (float)height));
        shader.setUniform("rectSize", sf::Glsl::Vec2(rectWidth_px, rectHeight_px));
        shader.setUniform("radius", radius_px);
        shader.setUniform("blur", blur_px);
        shader.setUniform("offsetY", offsetY_px);
        shader.setUniform("shadowColor", sf::Glsl::Vec4(shadowColor.r / 255.f, shadowColor.g / 255.f,
                                                        shadowColor.b / 255.f, shadowColor.a / 255.f));

        sf::VertexArray quad = makeFullQuadVA(width, height);
        rt.draw(quad, &shader);
        rt.display();

        sf::Texture tex = rt.getTexture();
        tex.setSmooth(true);
        tex.setRepeated(false);
        return tex;
      }

      // Fallback (simple blur approximation)
      int steps = 16;
      for (int i = steps - 1; i >= 0; --i)
      {
        float t = (float)i / (float)(steps - 1);
        float grow = blur_px * (1.0f - t);
        sf::Color c(shadowColor.r, shadowColor.g, shadowColor.b,
                    static_cast<sf::Uint8>(shadowColor.a * t));
        float rw = rectWidth_px + grow * 2.f;
        float rh = rectHeight_px + grow * 2.f;

        sf::RectangleShape body(sf::Vector2f(rw - 2.f * radius_px, rh - 2.f * radius_px));
        body.setOrigin(body.getSize() * 0.5f);
        body.setPosition((float)width * 0.5f, (float)height * 0.5f + offsetY_px);
        body.setFillColor(c);
        rt.draw(body);

        sf::CircleShape corner(radius_px + grow);
        corner.setFillColor(c);
        corner.setOrigin(radius_px + grow, radius_px + grow);
        corner.setPosition((float)(width * 0.5f - rw * 0.5f) + radius_px + grow,
                           (float)(height * 0.5f - rh * 0.5f) + radius_px + grow + offsetY_px);
        rt.draw(corner);
      }

      rt.display();
      sf::Texture tex = rt.getTexture();
      tex.setSmooth(true);
      tex.setRepeated(false);
      return tex;
    }

  } // namespace

  void ResourceTable::preLoad()
  {
    const auto &p = PaletteCache::get().palette();

    load(constant::tex::EVAL_WHITE, p[ColorId::COL_EVAL_WHITE]);
    load(constant::tex::EVAL_BLACK, p[ColorId::COL_EVAL_BLACK]);

    load(constant::tex::WHITE, p[ColorId::COL_BOARD_LIGHT]);
    load(constant::tex::BLACK, p[ColorId::COL_BOARD_DARK]);
    load(constant::tex::SELECT_HL, p[ColorId::COL_SELECT_HIGHLIGHT]);
    load(constant::tex::PREMOVE_HL, p[ColorId::COL_PREMOVE_HIGHLIGHT]);
    load(constant::tex::WARNING_HL, p[ColorId::COL_WARNING_HIGHLIGHT]);
    load(constant::tex::RCLICK_HL, p[ColorId::COL_RCLICK_HIGHLIGHT]);

    m_textures[std::string{constant::tex::ATTACK_HL}] =
        makeAttackDotTexture(constant::ATTACK_DOT_PX_SIZE, p[ColorId::COL_MARKER]);

    m_textures[std::string{constant::tex::HOVER_HL}] =
        makeSquareHoverTexture(constant::HOVER_PX_SIZE, p[ColorId::COL_HOVER_OUTLINE]);

    m_textures[std::string{constant::tex::CAPTURE_HL}] =
        makeCaptureCircleTexture(constant::CAPTURE_CIRCLE_PX_SIZE, p[ColorId::COL_MARKER]);

    m_textures[std::string{constant::tex::PROMOTION}] =
        makeRoundedRectTexture(constant::SQUARE_PX_SIZE, 4 * constant::SQUARE_PX_SIZE,
                               6.f, p[ColorId::COL_PANEL_ALPHA220], 1.0f);
    m_textures[std::string{constant::tex::PROMOTION_SHADOW}] =
        makeRoundedRectShadowTexture((unsigned int)(constant::SQUARE_PX_SIZE * 1.1f),
                                     4 * constant::SQUARE_PX_SIZE,
                                     constant::SQUARE_PX_SIZE * 1.1f,
                                     4 * constant::SQUARE_PX_SIZE,
                                     6.f, 12.f, p[ColorId::COL_SHADOW_STRONG], 4.f);

    load(constant::tex::TRANSPARENT, sf::Color::Transparent);
  }

} // namespace lilia::view
