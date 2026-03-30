#pragma once

#include <SFML/Graphics.hpp>

#include <optional>
#include <string>

#include "lilia/app/view/ui/style/modals/modal.hpp"
#include "lilia/app/view/ui/style/modals/modal_stack.hpp"
#include "lilia/app/view/ui/style/theme_cache.hpp"
#include "lilia/app/view/ui/style/modals/confirm_resign_modal.hpp"
#include "lilia/app/view/ui/style/modals/game_over_modal.hpp"

namespace lilia::app::view::ui
{

  // ModalView is the bridge between the game/view layer and UI modals.
  // It owns a ModalStack, routes SFML events, and exposes a small, stable API
  // used by GameView.
  class ModalView
  {
  public:
    ModalView() = default;
    ~ModalView() = default;

    // Assets
    bool loadFont(const std::string &path);

    // Resizing/layout
    void onResize(sf::Vector2u ws, MousePos boardCenter);

    // Modals
    void showResign(sf::Vector2u ws, MousePos anchorCenter);
    void hideResign();
    [[nodiscard]] bool isResignOpen() const;

    // NOTE: ws is passed explicitly to make the modal robust even if the caller
    // triggers it before the next layout()/onResize() tick.
    void showGameOver(sf::Vector2u ws, const std::string &msg, bool won, MousePos anchorCenter);
    void hideGameOver();
    [[nodiscard]] bool isGameOverOpen() const;

    // Events + per-frame tick
    bool handleEvent(const sf::Event &e, MousePos mouse);
    void update(float dt, MousePos mouse);
    void update(float dt) { update(dt, sf::Vector2f{}); }

    void drawOverlay(sf::RenderWindow &win) const;
    void drawPanel(sf::RenderWindow &win) const;

    // Retrieve and clear the most recent action.
    [[nodiscard]] ModalAction consumeAction();

  private:
    void trackDismissed(Modal &m);

    sf::Vector2u m_ws{};
    MousePos m_boardCenter{};

    ThemeCache m_theme; // self-updating via PaletteCache
    sf::Font m_font{};
    bool m_fontLoaded{false};

    ModalStack m_stack;

    ConfirmResignModal *m_resign{nullptr};
    GameOverModal *m_gameOver{nullptr};

    std::optional<ModalAction> m_pending{};
  };

} // namespace lilia::view
