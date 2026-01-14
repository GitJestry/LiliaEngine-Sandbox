#pragma once

#include <SFML/Graphics/Font.hpp>
#include <SFML/Graphics/Rect.hpp>
#include <SFML/Graphics/RenderWindow.hpp>
#include <SFML/Graphics/Text.hpp>

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include <SFML/System/Clock.hpp>

#include "lilia/view/ui/render/entity.hpp"
#include "lilia/view/ui/render/render_constants.hpp"
#include "lilia/view/ui/style/theme_cache.hpp"
#include "lilia/model/analysis/replay_info.hpp"

namespace lilia::view
{

  class MoveListView
  {
  public:
    MoveListView();
    ~MoveListView();

    void setPosition(const Entity::Position &pos);
    void setSize(unsigned int width, unsigned int height);
    void setFen(const std::string &fen);

    void addMove(const std::string &uciMove);
    void addResult(const std::string &result);
    void setCurrentMove(std::size_t moveIndex);
    void render(sf::RenderWindow &window) const;
    void scroll(float delta);
    void clear();

    void setBotMode(bool anyBot);

    // Replay header (optional)
    void setReplayHeader(std::optional<model::analysis::ReplayInfo> header);

    [[nodiscard]] std::size_t getMoveIndexAt(const Entity::Position &pos) const;

    enum class Option
    {
      None,
      Resign,
      Prev,
      Next,
      Settings,
      NewBot,
      Rematch,
      ShowFen
    };

    [[nodiscard]] Option getOptionAt(const Entity::Position &pos) const;
    void setGameOver(bool over);

  private:
    struct Row
    {
      unsigned turn{0};
      std::string white;
      std::string black;
      float whiteW{0.f};
      float blackW{0.f};
    };

    float measureMoveWidth(const std::string &s) const;

    [[nodiscard]] float listHeightPx() const;
    [[nodiscard]] float subHeaderHeightPx() const;
    [[nodiscard]] float contentTopPx() const;

    sf::Font m_font;
    std::vector<Row> m_rows;
    std::string m_result;

    Entity::Position m_position{};
    unsigned int m_width{constant::MOVE_LIST_WIDTH};
    unsigned int m_height{constant::WINDOW_PX_SIZE};

    float m_scroll_offset{0.f};
    std::size_t m_move_count{0};
    std::size_t m_selected_move{static_cast<std::size_t>(-1)};
    bool m_any_bot{false};
    bool m_game_over{false};
    std::string m_fen_str{};

    // Replay header data
    std::optional<model::analysis::ReplayInfo> m_replay_header{};

    // option hit areas (panel-local coordinates)
    sf::FloatRect m_bounds_resign{};
    sf::FloatRect m_bounds_prev{};
    sf::FloatRect m_bounds_next{};
    sf::FloatRect m_bounds_new_bot{};
    sf::FloatRect m_bounds_rematch{};
    sf::FloatRect m_bounds_fen_icon{};

    // “copied” toast
    mutable bool m_prevLeftDown{false};
    mutable sf::Clock m_copyClock;
    mutable bool m_copySuccess{false};

    ui::ThemeCache m_theme;
  };

} // namespace lilia::view
