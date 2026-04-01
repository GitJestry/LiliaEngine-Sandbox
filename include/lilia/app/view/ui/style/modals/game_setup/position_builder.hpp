#pragma once

#include <SFML/Graphics.hpp>
#include <memory>
#include <optional>
#include <string>
#include "lilia/app/view/mousepos.hpp"

namespace lilia::app::view::ui
{
  class Theme;
}

namespace lilia::app::view::ui::game_setup
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

    void updateHover(MousePos mouse, MousePos offset = {});
    bool handleEvent(const sf::Event &e, MousePos mouse, MousePos offset = {});

    void draw(sf::RenderTarget &rt, MousePos offset = {}) const;

  private:
    struct Impl;
    std::unique_ptr<Impl> m;
  };

}
