#pragma once

#include <SFML/Graphics/Font.hpp>
#include <SFML/Graphics/RectangleShape.hpp>
#include <SFML/Graphics/RenderTarget.hpp>
#include <SFML/Graphics/Text.hpp>

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "lilia/app/view/ui/render/entity.hpp"
#include "lilia/chess/chess_types.hpp"
#include "lilia/app/view/ui/style/theme.hpp"
#include "lilia/app/domain/outcome.hpp"
#include "lilia/app/domain/replay_info.hpp"

namespace lilia::app::view::ui
{
  class PlayerInfoView
  {
  public:
    PlayerInfoView();
    ~PlayerInfoView() = default;

    void setTheme(const Theme *t) noexcept { m_theme = t; }

    void setInfo(const domain::PlayerInfo &info);
    void setPlayerColor(chess::Color color);

    void setOutcome(std::optional<domain::Outcome> outcome);

    void setPositionClamped(const MousePos &pos, const sf::Vector2u &viewportSize);
    void setBoardCenter(float centerX);

    void render(sf::RenderWindow &rt);

    void addCapturedPiece(chess::PieceType type, chess::Color color);
    void removeCapturedPiece();
    void clearCapturedPieces();

  private:
    void setPosition(const MousePos &pos);
    void layoutCaptured();
    void layoutText();

    static Entity makeCapturedEntity(chess::PieceType type, chess::Color color);

    const Theme *m_theme{nullptr};

    Entity m_icon;
    sf::RectangleShape m_frame;      // geometry only
    sf::RectangleShape m_captureBox; // geometry only

    sf::Font m_font;
    sf::Text m_name;
    sf::Text m_elo;
    sf::Text m_noCaptures;

    std::optional<domain::Outcome> m_outcome;
    sf::RectangleShape m_outcomePill;
    sf::Text m_outcomeText;

    // keep originals so we can re-ellipsize on layout changes
    std::string m_fullName;
    std::string m_fullElo;

    chess::Color m_playerColor{chess::Color::White};
    MousePos m_position{};
    float m_boardCenter{0.f};

    std::vector<Entity> m_capturedPieces;
    std::vector<std::pair<chess::PieceType, chess::Color>> m_capturedInfo;

    std::string m_icon_name;
  };
}
