#pragma once

#include <SFML/Graphics.hpp>
#include <memory>
#include <optional>
#include <string>

namespace lilia::view::ui
{
  class Theme;
}

namespace lilia::view
{
  class PositionBuilder
  {
  public:
    PositionBuilder();
    ~PositionBuilder();

    PositionBuilder(const PositionBuilder &) = delete;
    PositionBuilder &operator=(const PositionBuilder &) = delete;

    void onOpen();

    void setTheme(const ui::Theme *t);
    void setFont(const sf::Font *f);
    void setBounds(sf::FloatRect r);

    void clear(bool remember = true);
    void resetToStart(bool remember = true);

    std::string fen() const;
    std::string fenForUse() const;

    bool kingsOk() const;
    int whiteKings() const;
    int blackKings() const;

    void updateHover(sf::Vector2f mouse, sf::Vector2f offset = {});
    bool handleEvent(const sf::Event &e, sf::Vector2f mouse, sf::Vector2f offset = {});

    void draw(sf::RenderTarget &rt, sf::Vector2f offset = {}) const;

  private:
    struct Impl;
    std::unique_ptr<Impl> m;
  };

} // namespace lilia::view
