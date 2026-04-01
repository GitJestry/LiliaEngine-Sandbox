#pragma once

namespace sf
{
  class Event;
}

#include <chrono>
#include <functional>
#include <optional>

#include "lilia/app/view/mousepos.hpp"

namespace lilia::app::controller
{

  class InputManager
  {
  public:
    using ClickCallback = std::function<void(view::MousePos)>;

    using DragCallback = std::function<void(view::MousePos start, view::MousePos current)>;

    using DropCallback = std::function<void(view::MousePos start, view::MousePos end)>;

    void setOnClick(ClickCallback cb);
    void setOnDrag(DragCallback cb);
    void setOnDrop(DropCallback cb);

    void processEvent(const sf::Event &event);
    void cancelDrag();

  private:
    bool m_dragging = false;                    // Indicates whether a drag operation is active.
    std::optional<view::MousePos> m_drag_start; // Starting position of an active drag.

    ClickCallback m_on_click = nullptr; // Registered click callback.
    DragCallback m_on_drag = nullptr;   // Registered drag callback.
    DropCallback m_on_drop = nullptr;   // Registered drop callback.

    [[nodiscard]] bool isClick(const view::MousePos &start, const view::MousePos &end,
                               int threshold = 4) const;
  };

}
