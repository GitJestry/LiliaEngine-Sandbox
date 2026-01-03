#pragma once
#include <SFML/Graphics/Color.hpp>

namespace lilia::view::ui
{

  struct Theme
  {
    sf::Color bgTop{};
    sf::Color bgBottom{};

    sf::Color panel{};
    sf::Color panelBorder{};

    sf::Color button{};
    sf::Color buttonHover{};
    sf::Color buttonActive{};

    sf::Color accent{};

    sf::Color text{};
    sf::Color subtle{};

    sf::Color inputBg{};
    sf::Color inputBorder{};
    sf::Color valid{};
    sf::Color invalid{};

    sf::Color toastBg{};

    sf::Color onButton{};
    sf::Color onAccent{};
  };

} // namespace lilia::view::ui
