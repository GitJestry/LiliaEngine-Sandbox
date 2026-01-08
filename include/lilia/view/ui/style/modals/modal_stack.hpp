#pragma once

#include <SFML/Graphics.hpp>
#include <memory>
#include <type_traits>
#include <vector>

#include "modal.hpp"

namespace lilia::view::ui
{

  class ModalStack
  {
  public:
    void push(std::unique_ptr<Modal> m) { m_modals.emplace_back(std::move(m)); }
    void pop()
    {
      if (!m_modals.empty())
        m_modals.pop_back();
    }
    void clear() { m_modals.clear(); }

    bool empty() const { return m_modals.empty(); }
    std::size_t size() const { return m_modals.size(); }

    Modal *top() { return m_modals.empty() ? nullptr : m_modals.back().get(); }
    const Modal *top() const { return m_modals.empty() ? nullptr : m_modals.back().get(); }

    void layout(sf::Vector2u ws)
    {
      for (auto &m : m_modals)
        m->layout(ws);
    }

    bool hasOpenModal() const { return !m_modals.empty(); }

    // Update without dismiss callback.
    void update(float dt, sf::Vector2f mouse)
    {
      update(dt, mouse, nullptr);
    }

    // Backwards compatible: update without mouse (hover won't be synced).
    void update(float dt)
    {
      update(dt, sf::Vector2f{}, nullptr);
    }

    template <class OnDismiss>
    void update(float dt, OnDismiss &&onDismiss)
    {
      update(dt, sf::Vector2f{}, std::forward<OnDismiss>(onDismiss));
    }

    // Main update: update all modals; sync pointer state for the top modal; pop dismissed.
    template <class OnDismiss>
    void update(float dt, sf::Vector2f mouse, OnDismiss &&onDismiss)
    {
      for (auto &m : m_modals)
        m->update(dt);

      if (!m_modals.empty())
      {
        const bool mouseDown = sf::Mouse::isButtonPressed(sf::Mouse::Left);
        m_modals.back()->updateInput(mouse, mouseDown);
      }

      while (!m_modals.empty() && m_modals.back()->dismissed())
      {
        if constexpr (!std::is_same_v<std::decay_t<OnDismiss>, std::nullptr_t>)
          onDismiss(*m_modals.back());
        m_modals.pop_back();
      }
    }

    void drawOverlay(sf::RenderWindow &win) const
    {
      if (m_modals.empty())
        return;
      m_modals.back()->drawOverlay(win);
    }

    void drawPanel(sf::RenderWindow &win) const
    {
      if (m_modals.empty())
        return;
      m_modals.back()->drawPanel(win);
    }

    bool handleEvent(const sf::Event &e, sf::Vector2f mouse)
    {
      if (m_modals.empty())
        return false;
      return m_modals.back()->handleEvent(e, mouse);
    }

  private:
    std::vector<std::unique_ptr<Modal>> m_modals;
  };

} // namespace lilia::view::ui
