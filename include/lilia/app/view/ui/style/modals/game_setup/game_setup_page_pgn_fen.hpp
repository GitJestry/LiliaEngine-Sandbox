#pragma once

#include <SFML/Graphics.hpp>
#include <functional>
#include <optional>
#include <string>

#include "lilia/app/view/ui/interaction/focus.hpp"
#include "lilia/app/view/ui/render/layout.hpp"
#include "lilia/app/view/ui/widgets/button.hpp"
#include "lilia/app/view/ui/widgets/text_area.hpp"
#include "lilia/app/view/ui/widgets/text_field.hpp"

#include "game_setup_types.hpp"
#include "game_setup_validation.hpp"
#include "game_setup_draw.hpp"

namespace lilia::app::view::ui::game_setup
{
  class PagePgnFen final
  {
  public:
    PagePgnFen(const sf::Font &font, const Theme &theme, FocusManager &focus);

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
    void updateHover(MousePos mouse);
    bool handleEvent(const sf::Event &e, MousePos mouse);

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
    const Theme &m_theme;
    FocusManager &m_focus;

    sf::FloatRect m_bounds{};
    sf::FloatRect m_fenCard{};
    sf::FloatRect m_pgnCard{};
    sf::FloatRect m_resolvedCard{};

    sf::FloatRect m_fenHeader{};
    sf::FloatRect m_pgnHeader{};
    sf::FloatRect m_resolvedHeader{};

    sf::FloatRect m_fenStatusLine{};
    sf::FloatRect m_pgnStatusLine{};

    TextField m_fenField{};
    TextArea m_pgnField{};

    Button m_pasteFen{};
    Button m_resetFen{};

    Button m_uploadPgn{};
    Button m_pastePgn{};
    Button m_clearPgn{};

    Source m_source{Source::Auto};
    mutable Button m_srcAuto{};
    mutable Button m_srcFen{};
    mutable Button m_srcPgn{};

    TextField m_resolvedFen{};
    Button m_copyResolved{};

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

    void setup_action(Button &b, const char *txt, std::function<void()> cb);
    void setup_chip(Button &b, const char *txt, Source s);

    void paste_fen_from_clipboard();

    void revalidate(bool force);
    ResolvedPath resolved_path() const;
    std::string compute_resolved_fen() const;
    void refresh_resolved_field();
  };

} // namespace lilia::view::game_setup
