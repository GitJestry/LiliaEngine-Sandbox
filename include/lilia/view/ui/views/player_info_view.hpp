#pragma once

#include <SFML/Graphics/Font.hpp>
#include <SFML/Graphics/RectangleShape.hpp>
#include <SFML/Graphics/RenderTarget.hpp>
#include <SFML/Graphics/Text.hpp>

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "lilia/view/ui/render/entity.hpp"
#include "lilia/chess_types.hpp"
#include "lilia/player_info.hpp"
#include "lilia/view/ui/style/theme.hpp"
#include "lilia/model/analysis/outcome.hpp"

namespace lilia::view
{
  class PlayerInfoView
  {
  public:
    PlayerInfoView();
    ~PlayerInfoView() = default;

    void setTheme(const ui::Theme *t) noexcept { m_theme = t; }

    void setInfo(const PlayerInfo &info);
    void setPlayerColor(core::Color color);

    void setOutcome(std::optional<model::analysis::Outcome> outcome);

    void setPositionClamped(const Entity::Position &pos, const sf::Vector2u &viewportSize);
    void setBoardCenter(float centerX);

    void render(sf::RenderWindow &rt);

    void addCapturedPiece(core::PieceType type, core::Color color);
    void removeCapturedPiece();
    void clearCapturedPieces();

  private:
    void setPosition(const Entity::Position &pos);
    void layoutCaptured();
    void layoutText();

    static Entity makeCapturedEntity(core::PieceType type, core::Color color);

    const ui::Theme *m_theme{nullptr};

    Entity m_icon;
    sf::RectangleShape m_frame;      // geometry only
    sf::RectangleShape m_captureBox; // geometry only

    sf::Font m_font;
    sf::Text m_name;
    sf::Text m_elo;
    sf::Text m_noCaptures;

    std::optional<model::analysis::Outcome> m_outcome;
    sf::RectangleShape m_outcomePill;
    sf::Text m_outcomeText;

    // keep originals so we can re-ellipsize on layout changes
    std::string m_fullName;
    std::string m_fullElo;

    core::Color m_playerColor{core::Color::White};
    Entity::Position m_position{};
    float m_boardCenter{0.f};

    std::vector<Entity> m_capturedPieces;
    std::vector<std::pair<core::PieceType, core::Color>> m_capturedInfo;

    std::string m_iconPath;
  };
} // namespace lilia::view
