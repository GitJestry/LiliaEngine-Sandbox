#pragma once

#include <SFML/Graphics.hpp>
#include <functional>
#include <string>

#include "modal.hpp"
#include "../style.hpp"
#include "../theme.hpp"
#include "lilia/view/ui/widgets/button.hpp"

namespace lilia::view::ui
{

  class ConfirmResignModal final : public Modal
  {
  public:
    struct Params
    {
      const Theme *theme{};
      const sf::Font *font{};
      std::function<void()> onYes{};
      std::function<void()> onNo{};
      std::function<void()> onClose{};
    };

    void open(sf::Vector2u ws, Params p)
    {
      m_open = true;
      m_dismissed = false;
      m_ws = ws;
      m_theme = p.theme;
      m_font = p.font;
      m_onYes = std::move(p.onYes);
      m_onNo = std::move(p.onNo);
      m_onClose = std::move(p.onClose);

      build();
      layout(ws);
    }

    void close()
    {
      m_open = false;
      m_dismissed = true;
    }

    bool isOpen() const { return m_open; }

    // Modal
    void layout(sf::Vector2u ws) override
    {
      m_ws = ws;
      if (!m_open)
        return;

      const float W = 360.f;
      const float H = 180.f;
      const float left = snapf(ws.x * 0.5f - W * 0.5f);
      const float top = snapf(ws.y * 0.5f - H * 0.5f);

      m_panel = sf::FloatRect(left, top, W, H);

      m_title.setPosition(snap({left + 16.f, top + 16.f - m_title.getLocalBounds().top}));

      const float msgTop = top + 56.f;
      m_msg.setPosition(snap({left + 16.f, msgTop - m_msg.getLocalBounds().top}));

      const float by = snapf(top + H - 52.f);
      m_btnYes.setBounds({left + W * 0.5f - 16.f - 120.f, by, 120.f, 36.f});
      m_btnNo.setBounds({left + W * 0.5f + 16.f, by, 120.f, 36.f});

      m_btnClose.setBounds({left + W - 28.f - 10.f, top + 10.f, 28.f, 28.f});
    }

    void update(float /*dt*/) override {}

    void updateInput(sf::Vector2f mouse, bool mouseDown) override
    {
      m_btnYes.updateInput(mouse, mouseDown);
      m_btnNo.updateInput(mouse, mouseDown);
      m_btnClose.updateInput(mouse, mouseDown);
    }

    void drawOverlay(sf::RenderWindow &win) const override
    {
      if (!m_open || !m_theme)
        return;
      sf::RectangleShape ov({float(m_ws.x), float(m_ws.y)});
      ov.setPosition(0.f, 0.f);
      ov.setFillColor(m_theme->toastBg);
      win.draw(ov);
    }

    void drawPanel(sf::RenderWindow &win) const override
    {
      if (!m_open || !m_theme)
        return;

      sf::RectangleShape panel({m_panel.width, m_panel.height});
      panel.setPosition(snap({m_panel.left, m_panel.top}));
      panel.setFillColor(m_theme->panel);
      panel.setOutlineThickness(1.f);
      panel.setOutlineColor(m_theme->panelBorder);
      win.draw(panel);

      win.draw(m_title);
      win.draw(m_msg);

      m_btnYes.draw(win);
      m_btnNo.draw(win);
      m_btnClose.draw(win);
    }

    bool handleEvent(const sf::Event &e, sf::Vector2f mouse) override
    {
      if (!m_theme)
        return false;

      // Clicks
      if (m_btnYes.handleEvent(e, mouse))
        return true;
      if (m_btnNo.handleEvent(e, mouse))
        return true;
      if (m_btnClose.handleEvent(e, mouse))
        return true;

      // Escape closes
      if (e.type == sf::Event::KeyPressed && e.key.code == sf::Keyboard::Escape)
      {
        if (m_onClose)
          m_onClose();
        close();
        return true;
      }

      return false;
    }

    bool dismissed() const override { return m_dismissed; }

  private:
    void build()
    {
      if (!m_theme || !m_font)
        return;

      m_title.setFont(*m_font);
      m_title.setCharacterSize(20);
      m_title.setStyle(sf::Text::Bold);
      m_title.setFillColor(m_theme->text);
      m_title.setString("Confirm Resign");

      m_msg.setFont(*m_font);
      m_msg.setCharacterSize(16);
      m_msg.setFillColor(m_theme->subtle);
      m_msg.setString("Do you really want to resign?");

      m_btnYes.setTheme(m_theme);
      m_btnYes.setFont(*m_font);
      m_btnYes.setText("Yes", 16);
      m_btnYes.setAccent(true);
      m_btnYes.setOnClick([this]
                          {
        if (m_onYes) m_onYes();
        close(); });

      m_btnNo.setTheme(m_theme);
      m_btnNo.setFont(*m_font);
      m_btnNo.setText("No", 16);
      m_btnNo.setOnClick([this]
                         {
        if (m_onNo) m_onNo();
        close(); });

      m_btnClose.setTheme(m_theme);
      m_btnClose.setFont(*m_font);
      m_btnClose.setText("X", 16);
      m_btnClose.setOnClick([this]
                            {
        if (m_onClose) m_onClose();
        close(); });
    }

  private:
    bool m_open{false};
    bool m_dismissed{false};
    sf::Vector2u m_ws{};
    const Theme *m_theme{nullptr};
    const sf::Font *m_font{nullptr};

    sf::FloatRect m_panel{};

    sf::Text m_title{};
    sf::Text m_msg{};

    Button m_btnYes{};
    Button m_btnNo{};
    Button m_btnClose{};

    std::function<void()> m_onYes{};
    std::function<void()> m_onNo{};
    std::function<void()> m_onClose{};
  };

} // namespace lilia::view::ui
