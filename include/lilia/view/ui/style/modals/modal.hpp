#pragma once

#include <SFML/Graphics.hpp>

namespace lilia::view::ui
{

  // Shared modal actions consumed by GameView / controller.
  //
  // Design rules:
  // - Keep semantically small and stable.
  // - Prefer adding new values over changing meaning of existing values.
  // - If you need payloads (strings, ids, paths), emit them via callbacks
  //   or a dedicated result object (do not overload this enum).
  enum class ModalAction
  {
    None,

    // Confirm-resign modal
    ResignYes,
    ResignNo,

    // Game-over modal
    NewBot,
    Rematch,

    // Generic close
    Close,
  };

  class Modal
  {
  public:
    virtual ~Modal() = default;

    // Required layout + tick
    virtual void layout(sf::Vector2u ws) = 0;
    virtual void update(float dt) = 0;

    // Per-frame pointer sync for responsive hover/press visuals.
    // Called by the owning stack/view for the top-most modal only.
    virtual void updateInput(sf::Vector2f /*mouse*/, bool /*mouseDown*/) {}

    // Rendering:
    // Overlay is optional (dropdown/menu modals may not dim the background).
    virtual void drawOverlay(sf::RenderWindow & /*win*/) const {}
    virtual void drawPanel(sf::RenderWindow &win) const = 0;

    // Input dispatch (top-most modal only, via ModalStack)
    virtual bool handleEvent(const sf::Event &e, sf::Vector2f mouse) = 0;

    // Lifecycle:
    // Default implementation uses the built-in flag. Modals may override if needed.
    virtual bool dismissed() const { return m_dismissed; }

    // Optional policy hooks (useful for consistent behavior across modals).
    virtual bool closeOnEsc() const { return true; }
    virtual bool dimBackground() const { return true; }

  protected:
    // Unified "close/dismiss" mechanism for derived classes.
    void requestDismiss() { m_dismissed = true; }
    void clearDismissed() { m_dismissed = false; }
    void setDismissed(bool v) { m_dismissed = v; }

  private:
    bool m_dismissed{false};
  };

} // namespace lilia::view::ui
