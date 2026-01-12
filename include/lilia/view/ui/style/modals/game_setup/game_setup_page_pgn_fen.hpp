#pragma once

#include <SFML/Graphics.hpp>
#include <functional>
#include <optional>
#include <string>

#include "lilia/constants.hpp"
#include "lilia/view/ui/interaction/focus.hpp"
#include "lilia/view/ui/render/layout.hpp"
#include "lilia/view/ui/widgets/button.hpp"
#include "lilia/view/ui/widgets/text_area.hpp"
#include "lilia/view/ui/widgets/text_field.hpp"

#include "game_setup_types.hpp"
#include "game_setup_validation.hpp"
#include "game_setup_draw.hpp"

namespace lilia::view::ui::game_setup
{
  class PagePgnFen final
  {
  public:
    PagePgnFen(const sf::Font &font, const ui::Theme &theme, ui::FocusManager &focus);

    void setOnRequestPgnUpload(std::function<void()> cb);
    void setPgnFilename(const std::string &name);

    void setFenText(const std::string &fen);
    void setPgnText(const std::string &pgn, bool fromUpload = false);

    std::string pgnText() const;
    std::string pgnFilename() const;
    bool hasPgnText() const;

    void setSource(Source s);
    Source source() const;

    void layout(const sf::FloatRect &bounds);

    void update();
    void updateHover(sf::Vector2f mouse);
    bool handleEvent(const sf::Event &e, sf::Vector2f mouse);

    void draw(sf::RenderTarget &rt) const;

    std::string resolvedFen() const;
    bool resolvedFenOk() const;

    // Used by modal footer
    std::string actual_source_label() const;
    bool using_custom_position() const;

    // Used by modal Ctrl+V routing
    bool paste_auto_from_clipboard();

    // Also used by modal setter
    void paste_auto_from_clipboard_into_fields();

  private:
    const sf::Font &m_font;
    const ui::Theme &m_theme;
    ui::FocusManager &m_focus;

    sf::FloatRect m_bounds{};
    sf::FloatRect m_fenCard{};
    sf::FloatRect m_pgnCard{};
    sf::FloatRect m_resolvedCard{};

    sf::FloatRect m_fenHeader{};
    sf::FloatRect m_pgnHeader{};
    sf::FloatRect m_resolvedHeader{};

    sf::FloatRect m_fenStatusLine{};
    sf::FloatRect m_pgnStatusLine{};

    ui::TextField m_fenField{};
    ui::TextArea m_pgnField{};

    ui::Button m_pasteFen{};
    ui::Button m_resetFen{};

    ui::Button m_uploadPgn{};
    ui::Button m_pastePgn{};
    ui::Button m_clearPgn{};

    Source m_source{Source::Auto};
    mutable ui::Button m_srcAuto{};
    mutable ui::Button m_srcFen{};
    mutable ui::Button m_srcPgn{};

    ui::TextField m_resolvedFen{};
    ui::Button m_copyResolved{};

    std::function<void()> m_onRequestPgnUpload;
    std::string m_pgnFilename;

    // Cached validation state (updated by revalidate())
    std::string m_lastFenRaw;
    std::string m_lastPgnRaw;

    bool m_fenOk{false};
    std::string m_fenSanitized; // sanitized output when ok, else empty
    PgnStatus m_pgnStatus{};

    enum class ResolvedPath
    {
      Start,
      Fen,
      Pgn
    };

    void setup_action(ui::Button &b, const char *txt, std::function<void()> cb);
    void setup_chip(ui::Button &b, const char *txt, Source s);

    void paste_fen_from_clipboard();

    void revalidate(bool force);
    ResolvedPath resolved_path() const;
    std::string compute_resolved_fen() const;
    void refresh_resolved_field();
  };

} // namespace lilia::view::ui::game_setup
