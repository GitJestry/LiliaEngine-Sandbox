#pragma once

namespace lilia::view::ui {

class Focusable {
 public:
  virtual ~Focusable() = default;
  virtual void onFocusGained() = 0;
  virtual void onFocusLost() = 0;
};

class FocusManager {
 public:
  void request(Focusable* w) {
    if (m_focused == w) return;
    if (m_focused) m_focused->onFocusLost();
    m_focused = w;
    if (m_focused) m_focused->onFocusGained();
  }

  void clear() {
    if (!m_focused) return;
    m_focused->onFocusLost();
    m_focused = nullptr;
  }

  Focusable* focused() const { return m_focused; }

 private:
  Focusable* m_focused{nullptr};
};

}  // namespace lilia::view::ui
