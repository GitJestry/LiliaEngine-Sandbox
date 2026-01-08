#pragma once

#include <SFML/Graphics.hpp>

#include <functional>
#include <optional>
#include <string>

#include "../modal.hpp"
#include "lilia/view/ui/render/layout.hpp"
#include "lilia/view/ui/interaction/focus.hpp"
#include "lilia/view/ui/widgets/button.hpp"

#include "../../theme.hpp"
#include "../../style.hpp"

#include "game_setup_types.hpp"
#include "game_setup_draw.hpp"
#include "game_setup_page_pgn_fen.hpp"
#include "game_setup_page_builder.hpp"
#include "game_setup_page_history.hpp"

namespace lilia::view::ui
{
  class GameSetupModal final : public Modal
  {
  public:
    GameSetupModal(const sf::Font &font, const ui::Theme &theme, ui::FocusManager &focus);

    void setOnRequestPgnUpload(std::function<void()> cb);

    void setFenText(const std::string &fen);
    void setPgnText(const std::string &pgn);
    void setPgnFilename(const std::string &name);

    std::optional<std::string> resultFen() const;

    void layout(sf::Vector2u ws) override;
    void update(float dt) override;
    void updateInput(sf::Vector2f mouse, bool mouseDown) override;

    void drawOverlay(sf::RenderWindow &win) const override;
    void drawPanel(sf::RenderWindow &win) const override;

    bool handleEvent(const sf::Event &e, sf::Vector2f mouse) override;

  private:
    const sf::Font &m_font;
    const ui::Theme &m_theme;
    ui::FocusManager &m_focus;

    sf::Vector2u m_ws{};
    sf::Vector2f m_mouse{};

    sf::FloatRect m_rect{};
    sf::FloatRect m_inner{};
    sf::FloatRect m_pages{};
    sf::FloatRect m_contentRect{};
    sf::FloatRect m_usingPill{};

    sf::Text m_title{};

    ui::Button m_close{};
    ui::Button m_continue{};
    ui::Button m_historyBtn{};
    ui::Button m_backBtn{};

    mutable ui::Button m_tabPgnFen{};
    mutable ui::Button m_tabBuild{};

    bool m_showHistory{false};
    game_setup::Mode m_mode{game_setup::Mode::PgnFen};

    game_setup::PagePgnFen m_pagePgnFen;
    game_setup::PageBuilder m_pageBuilder;
    game_setup::PageHistory m_pageHistory;

    std::function<void()> m_onRequestPgnUpload;
    std::optional<std::string> m_resultFen{};

    void setup_action(ui::Button &b, const char *txt, std::function<void()> cb);

    std::string resolvedFen() const;
    std::string usingLabel() const;
    bool usingOk() const;

    void drawUsingFooter(sf::RenderTarget &rt) const;
  };

} // namespace lilia::view::ui
