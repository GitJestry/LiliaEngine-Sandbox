#include "lilia/view/ui/views/move_list_view.hpp"

#include <SFML/Graphics/CircleShape.hpp>
#include <SFML/Graphics/ConvexShape.hpp>
#include <SFML/Graphics/RectangleShape.hpp>
#include <SFML/Graphics/View.hpp>
#include <SFML/Window/Clipboard.hpp>
#include <SFML/Window/Mouse.hpp>

#include <algorithm>
#include <cmath>
#include <sstream>
#include <utility>
#include <vector>

#include "lilia/model/analysis/eco_opening_db.hpp"
#include "lilia/view/ui/style/style.hpp"
#include "lilia/view/ui/style/theme.hpp"

namespace lilia::view
{

  namespace
  {

    // ---------- Layout ----------
    constexpr float kPaddingX = 12.f;

    constexpr float kRowH = 26.f;
    constexpr float kNumColW = 56.f;
    constexpr float kMoveGap = 30.f;

    constexpr float kHeaderH = 58.f;
    constexpr float kFenH = 30.f;

    constexpr float kSubHeaderH_Default = 40.f;
    constexpr float kSubHeaderH_Replay = 98.f; // bigger: opening title + info list

    constexpr float kListTopGap = 8.f;

    constexpr float kFooterH = 54.f;
    constexpr float kSlot = 32.f;
    constexpr float kSlotGap = 25.f;
    constexpr float kFooterPadX = 25.f;

    constexpr float kTipPadX = 8.f;
    constexpr float kTipPadY = 5.f;
    constexpr float kTipArrowH = 6.f;

    constexpr unsigned kMoveNumberFontSize = 14;
    constexpr unsigned kMoveFontSize = 15;
    constexpr unsigned kHeaderFontSize = 22;
    constexpr unsigned kSubHeaderFontSize = 16;
    constexpr unsigned kTipFontSize = 13;

    constexpr unsigned kReplayOpeningFontSize = 16;
    constexpr unsigned kReplayMetaFontSize = 13;

    inline sf::Vector2f centerOf(const sf::FloatRect &r)
    {
      return {r.left + r.width * 0.5f, r.top + r.height * 0.5f};
    }

    std::string ellipsizeRightKeepTail(const std::string &s, sf::Text &probe, float maxW)
    {
      probe.setString(s);
      if (probe.getLocalBounds().width <= maxW)
        return s;

      for (std::size_t cut = 0; cut < s.size(); ++cut)
      {
        std::string view = "..." + s.substr(cut);
        probe.setString(view);
        if (probe.getLocalBounds().width <= maxW)
          return view;
      }
      return s;
    }

    void drawTooltip(sf::RenderWindow &win,
                     const sf::Vector2f center,
                     const std::string &label,
                     const sf::Font &font,
                     const ui::Theme &th)
    {
      sf::Text t(label, font, kTipFontSize);
      t.setFillColor(th.text);
      auto b = t.getLocalBounds();

      const float w = b.width + 2.f * kTipPadX;
      const float h = b.height + 2.f * kTipPadY;
      const float x = ui::snapf(center.x - w * 0.5f);
      const float y = ui::snapf(center.y - h - kTipArrowH - 4.f);

      sf::RectangleShape shadow({w, h});
      shadow.setPosition(ui::snap({x + 2.f, y + 2.f}));
      shadow.setFillColor(sf::Color(0, 0, 0, 50));
      win.draw(shadow);

      sf::RectangleShape body({w, h});
      body.setPosition(ui::snap({x, y}));
      body.setFillColor(th.toastBg.a ? th.toastBg : ui::darken(th.panel, 12));
      body.setOutlineThickness(1.f);
      body.setOutlineColor(th.panelBorder);
      win.draw(body);

      sf::ConvexShape arrow(3);
      arrow.setPoint(0, {center.x - 6.f, y + h});
      arrow.setPoint(1, {center.x + 6.f, y + h});
      arrow.setPoint(2, {center.x, y + h + kTipArrowH});
      arrow.setFillColor(body.getFillColor());
      win.draw(arrow);

      t.setPosition(ui::snap({x + kTipPadX - b.left, y + kTipPadY - b.top}));
      win.draw(t);
    }

    void drawSlot(sf::RenderTarget &rt,
                  const sf::FloatRect &r,
                  const ui::Theme &th,
                  bool hovered,
                  bool pressed)
    {
      ui::drawBevelButton(rt, r, th.button, hovered, pressed);
      sf::RectangleShape hair({r.width - 2.f, r.height - 2.f});
      hair.setPosition(ui::snap({r.left + 1.f, r.top + 1.f}));
      hair.setFillColor(sf::Color::Transparent);
      hair.setOutlineThickness(1.f);
      hair.setOutlineColor(hovered ? th.accent : th.panelBorder);
      rt.draw(hair);
    }

    void drawChevron(sf::RenderWindow &win, const sf::FloatRect &slot, bool left, sf::Color col)
    {
      const float s = std::min(slot.width, slot.height) * 0.50f;
      const float x0 = slot.left + (slot.width - s) * 0.5f;
      const float y0 = slot.top + (slot.height - s) * 0.5f;

      sf::ConvexShape tri(3);
      if (left)
      {
        tri.setPoint(0, {x0 + s, y0});
        tri.setPoint(1, {x0, y0 + s * 0.5f});
        tri.setPoint(2, {x0 + s, y0 + s});
      }
      else
      {
        tri.setPoint(0, {x0, y0});
        tri.setPoint(1, {x0 + s, y0 + s * 0.5f});
        tri.setPoint(2, {x0, y0 + s});
      }
      tri.setFillColor(col);
      win.draw(tri);
    }

    void drawCrossX(sf::RenderWindow &win, const sf::FloatRect &slot, sf::Color col)
    {
      const float s = std::min(slot.width, slot.height) * 0.70f;
      const float cx = slot.left + slot.width * 0.5f;
      const float cy = slot.top + slot.height * 0.5f;
      const float thick = 2.f;

      sf::RectangleShape bar1({s, thick});
      bar1.setOrigin(s * 0.5f, thick * 0.5f);
      bar1.setPosition(ui::snap({cx, cy}));
      bar1.setRotation(45.f);
      bar1.setFillColor(col);

      sf::RectangleShape bar2 = bar1;
      bar2.setRotation(-45.f);

      win.draw(bar1);
      win.draw(bar2);
    }

    void drawRobot(sf::RenderWindow &win, const sf::FloatRect &slot, sf::Color col)
    {
      const float s = std::min(slot.width, slot.height);
      const float cx = slot.left + slot.width * 0.5f;
      const float cy = slot.top + slot.height * 0.5f;

      sf::RectangleShape head({s * 0.55f, s * 0.42f});
      head.setOrigin(head.getSize() * 0.5f);
      head.setPosition(ui::snap({cx, cy + s * 0.04f}));
      head.setFillColor(sf::Color::Transparent);
      head.setOutlineThickness(2.f);
      head.setOutlineColor(col);

      sf::RectangleShape antenna({2.f, s * 0.16f});
      antenna.setOrigin(1.f, antenna.getSize().y);
      antenna.setPosition(ui::snap({cx, cy - s * 0.30f}));
      antenna.setFillColor(col);

      sf::RectangleShape eyeL({s * 0.08f, s * 0.10f});
      eyeL.setOrigin(eyeL.getSize() * 0.5f);
      eyeL.setPosition(ui::snap({cx - s * 0.12f, cy - s * 0.02f}));
      eyeL.setFillColor(col);

      sf::RectangleShape eyeR = eyeL;
      eyeR.setPosition(ui::snap({cx + s * 0.12f, cy - s * 0.02f}));

      win.draw(head);
      win.draw(antenna);
      win.draw(eyeL);
      win.draw(eyeR);
    }

    void drawReload(sf::RenderWindow &win, const sf::FloatRect &slot, sf::Color col)
    {
      const float s = std::min(slot.width, slot.height) * 0.70f;
      const float cx = slot.left + slot.width * 0.5f;
      const float cy = slot.top + slot.height * 0.5f;

      sf::CircleShape ring(s * 0.5f);
      ring.setOrigin(s * 0.5f, s * 0.5f);
      ring.setPosition(ui::snap({cx, cy}));
      ring.setFillColor(sf::Color::Transparent);
      ring.setOutlineThickness(2.f);
      ring.setOutlineColor(col);
      win.draw(ring);

      sf::ConvexShape arrow(3);
      arrow.setPoint(0, {cx + s * 0.12f, cy - s * 0.55f});
      arrow.setPoint(1, {cx + s * 0.42f, cy - s * 0.40f});
      arrow.setPoint(2, {cx + s * 0.15f, cy - s * 0.25f});
      arrow.setFillColor(col);
      win.draw(arrow);
    }

    void drawFenIcon(sf::RenderWindow &win, const sf::FloatRect &slot, bool success, sf::Color col)
    {
      if (success)
      {
        const float s = slot.width * 1.2f;
        const float x = ui::snapf(slot.left + (slot.width - s));
        const float y = ui::snapf(slot.top + (slot.height - s));
        sf::Color ok(40, 170, 40);

        sf::VertexArray check(sf::LinesStrip, 3);
        check[0].position = {x + s * 0.15f, y + s * 0.55f};
        check[1].position = {x + s * 0.4f, y + s * 0.8f};
        check[2].position = {x + s * 0.85f, y + s * 0.25f};
        for (std::size_t i = 0; i < check.getVertexCount(); ++i)
          check[i].color = ok;
        win.draw(check);
        return;
      }

      const float w = slot.width * 0.55f;
      const float h = slot.height * 0.55f;
      const float off = w * 0.25f;

      const float bx = ui::snapf(slot.left + (slot.width - w) * 0.5f - off);
      const float by = ui::snapf(slot.top + (slot.height - h) * 0.5f - off);
      sf::RectangleShape back({w, h});
      back.setPosition({bx, by});
      back.setFillColor(sf::Color::Transparent);
      back.setOutlineThickness(2.f);
      back.setOutlineColor(col);
      win.draw(back);

      const float fx = ui::snapf(slot.left + (slot.width - w) * 0.5f + off);
      const float fy = ui::snapf(slot.top + (slot.height - h) * 0.5f + off);
      sf::RectangleShape front({w, h});
      front.setPosition({fx, fy});
      front.setFillColor(sf::Color::Transparent);
      front.setOutlineThickness(2.f);
      front.setOutlineColor(col);
      win.draw(front);
    }

    static std::string buildReplayOpeningTitle(const model::analysis::ReplayInfo &h)
    {
      // If openingName is missing or is accidentally just "B28", resolve via DB.
      return lilia::model::analysis::EcoOpeningDb::resolveOpeningTitle(h.eco, h.openingName);
    }

  } // namespace

  MoveListView::MoveListView()
  {
    m_font.loadFromFile(std::string{constant::path::FONT_DIR});
    m_font.setSmooth(false);
  }

  MoveListView::~MoveListView() = default;

  float MoveListView::measureMoveWidth(const std::string &s) const
  {
    if (s.empty())
      return 0.f;
    sf::Text t(s, m_font, kMoveFontSize);
    t.setStyle(sf::Text::Bold);
    return t.getLocalBounds().width;
  }

  float MoveListView::listHeightPx() const
  {
    return static_cast<float>(m_height) - kFooterH;
  }

  float MoveListView::subHeaderHeightPx() const
  {
    return m_replay_header ? kSubHeaderH_Replay : kSubHeaderH_Default;
  }

  float MoveListView::contentTopPx() const
  {
    return kHeaderH + kFenH + subHeaderHeightPx() + kListTopGap;
  }

  void MoveListView::setPosition(const Entity::Position &pos)
  {
    m_position = pos;
  }

  void MoveListView::setReplayHeader(std::optional<model::analysis::ReplayInfo> header)
  {
    m_replay_header = std::move(header);

    // If header size changed (replay vs non-replay), keep scroll clamped.
    const float listH = listHeightPx();
    const float topY = contentTopPx();
    const float visible = listH - topY;
    const float content = static_cast<float>(m_rows.size() + (m_result.empty() ? 0 : 1)) * kRowH;
    const float maxOff = std::max(0.f, content - visible);
    m_scroll_offset = std::clamp(m_scroll_offset, 0.f, maxOff);
  }

  void MoveListView::setSize(unsigned int width, unsigned int height)
  {
    m_width = width;
    m_height = height;

    const float listH = listHeightPx();
    const float centerY = listH + (kFooterH * 0.5f);

    const float left1X = kFooterPadX;
    const float left2X = kFooterPadX + kSlot + kSlotGap;

    const float midL = (static_cast<float>(m_width) * 0.7f) - (kSlotGap * 0.5f) - kSlot;
    const float midR = (static_cast<float>(m_width) * 0.7f) + (kSlotGap * 0.5f);

    m_bounds_resign = {left1X, centerY - kSlot * 0.5f, kSlot, kSlot};
    m_bounds_new_bot = {left1X, centerY - kSlot * 0.5f, kSlot, kSlot};
    m_bounds_rematch = {left2X, centerY - kSlot * 0.5f, kSlot, kSlot};
    m_bounds_prev = {midL, centerY - kSlot * 0.5f, kSlot, kSlot};
    m_bounds_next = {midR, centerY - kSlot * 0.5f, kSlot, kSlot};

    const float fenIconSize = 18.f;
    m_bounds_fen_icon = {kPaddingX, kHeaderH + (kFenH - fenIconSize) * 0.5f, fenIconSize, fenIconSize};

    const float topY = contentTopPx();
    const float visible = listH - topY;
    const float content = static_cast<float>(m_rows.size() + (m_result.empty() ? 0 : 1)) * kRowH;
    const float maxOff = std::max(0.f, content - visible);
    m_scroll_offset = std::clamp(m_scroll_offset, 0.f, maxOff);
  }

  void MoveListView::setBotMode(bool anyBot)
  {
    m_any_bot = anyBot;
  }

  void MoveListView::setFen(const std::string &fen)
  {
    m_fen_str = fen;
  }

  void MoveListView::addMove(const std::string &uciMove)
  {
    const std::size_t moveIndex = m_move_count;
    const std::size_t rowIndex = moveIndex / 2;
    const bool whiteTurn = (moveIndex % 2) == 0;

    if (whiteTurn)
    {
      if (m_rows.size() <= rowIndex)
        m_rows.resize(rowIndex + 1);
      Row &r = m_rows[rowIndex];
      r.turn = static_cast<unsigned>(rowIndex + 1);
      r.white = uciMove;
      r.whiteW = measureMoveWidth(r.white);
    }
    else
    {
      if (m_rows.empty())
      {
        m_rows.push_back(Row{1u, "", uciMove, 0.f, measureMoveWidth(uciMove)});
      }
      else
      {
        Row &r = m_rows.back();
        r.black = uciMove;
        r.blackW = measureMoveWidth(r.black);
      }
    }

    ++m_move_count;
    m_selected_move = m_move_count ? (m_move_count - 1) : m_selected_move;

    const float listH = listHeightPx();
    const float topY = contentTopPx();
    const float visible = listH - topY;
    const float content = static_cast<float>(m_rows.size() + (m_result.empty() ? 0 : 1)) * kRowH;
    const float maxOff = std::max(0.f, content - visible);
    m_scroll_offset = maxOff;
  }

  void MoveListView::addResult(const std::string &result)
  {
    m_result = result;

    const float listH = listHeightPx();
    const float topY = contentTopPx();
    const float visible = listH - topY;
    const float content = static_cast<float>(m_rows.size() + 1) * kRowH;
    const float maxOff = std::max(0.f, content - visible);
    m_scroll_offset = maxOff;
  }

  void MoveListView::scroll(float delta)
  {
    m_scroll_offset -= delta * kRowH;

    const float listH = listHeightPx();
    const float topY = contentTopPx();
    const float visible = listH - topY;
    const float content = static_cast<float>(m_rows.size() + (m_result.empty() ? 0 : 1)) * kRowH;
    const float maxOff = std::max(0.f, content - visible);
    m_scroll_offset = std::clamp(m_scroll_offset, 0.f, maxOff);
  }

  void MoveListView::clear()
  {
    m_rows.clear();
    m_move_count = 0;
    m_scroll_offset = 0.f;
    m_selected_move = static_cast<std::size_t>(-1);
    m_result.clear();
    m_fen_str.clear();
    m_copySuccess = false;
    m_replay_header.reset();
  }

  void MoveListView::setCurrentMove(std::size_t moveIndex)
  {
    m_selected_move = moveIndex;
    if (moveIndex == static_cast<std::size_t>(-1))
      return;

    const std::size_t rowIndex = moveIndex / 2;
    const float rowY = static_cast<float>(rowIndex) * kRowH;

    const float listH = listHeightPx();
    const float topY = contentTopPx();
    const float visible = listH - topY;

    if (rowY < m_scroll_offset)
      m_scroll_offset = rowY;
    else if (rowY + kRowH > m_scroll_offset + visible)
      m_scroll_offset = rowY + kRowH - visible;

    const float content = static_cast<float>(m_rows.size() + (m_result.empty() ? 0 : 1)) * kRowH;
    const float maxOff = std::max(0.f, content - visible);
    m_scroll_offset = std::clamp(m_scroll_offset, 0.f, maxOff);
  }

  std::size_t MoveListView::getMoveIndexAt(const Entity::Position &pos) const
  {
    const float localX = pos.x - m_position.x;
    const float localY = pos.y - m_position.y;

    const float listH = listHeightPx();
    const float topY = contentTopPx();

    if (localX < 0.f || localY < topY || localX > static_cast<float>(m_width) || localY > listH)
      return static_cast<std::size_t>(-1);

    const float contentY = (localY - topY) + m_scroll_offset;
    const std::size_t rowIndex = static_cast<std::size_t>(std::floor(contentY / kRowH));

    if (rowIndex >= m_rows.size())
      return static_cast<std::size_t>(-1);

    const Row &r = m_rows[rowIndex];

    const float xWhite = kPaddingX + kNumColW;
    const float xBlack = xWhite + r.whiteW + kMoveGap;

    if (!r.white.empty() && localX >= xWhite && localX <= xWhite + r.whiteW)
      return rowIndex * 2;
    if (!r.black.empty() && localX >= xBlack && localX <= xBlack + r.blackW)
      return rowIndex * 2 + 1;

    return static_cast<std::size_t>(-1);
  }

  MoveListView::Option MoveListView::getOptionAt(const Entity::Position &pos) const
  {
    const float localX = pos.x - m_position.x;
    const float localY = pos.y - m_position.y;

    if (m_bounds_fen_icon.contains(localX, localY))
      return Option::ShowFen;

    if (m_game_over)
    {
      if (m_bounds_new_bot.contains(localX, localY))
        return Option::NewBot;
      if (m_bounds_rematch.contains(localX, localY))
        return Option::Rematch;
    }
    else
    {
      if (m_bounds_resign.contains(localX, localY))
        return Option::Resign;
    }

    if (m_bounds_prev.contains(localX, localY))
      return Option::Prev;
    if (m_bounds_next.contains(localX, localY))
      return Option::Next;

    return Option::None;
  }

  void MoveListView::setGameOver(bool over)
  {
    m_game_over = over;
  }

  void MoveListView::render(sf::RenderWindow &window) const
  {
    const ui::Theme &th = m_theme.uiTheme();
    const sf::View oldView = window.getView();

    sf::View view(sf::FloatRect(0.f, 0.f, static_cast<float>(m_width), static_cast<float>(m_height)));
    view.setViewport(sf::FloatRect(m_position.x / static_cast<float>(window.getSize().x),
                                   m_position.y / static_cast<float>(window.getSize().y),
                                   static_cast<float>(m_width) / static_cast<float>(window.getSize().x),
                                   static_cast<float>(m_height) / static_cast<float>(window.getSize().y)));
    window.setView(view);

    sf::Vector2i mousePx = sf::Mouse::getPosition(window);
    sf::Vector2f mouseLocal = window.mapPixelToCoords(mousePx, view);

    const float listH = listHeightPx();
    const float subH = subHeaderHeightPx();
    const float topY = contentTopPx();

    // --- Panel shadow + border ---
    {
      const sf::FloatRect panelRect(0.f, 0.f, static_cast<float>(m_width), static_cast<float>(m_height));
      ui::drawPanelShadow(window, panelRect);

      sf::RectangleShape border({panelRect.width, panelRect.height});
      border.setPosition(ui::snap({panelRect.left, panelRect.top}));
      border.setFillColor(sf::Color::Transparent);
      border.setOutlineThickness(1.f);
      border.setOutlineColor(th.panelBorder);
      window.draw(border);
    }

    // --- Background ---
    sf::RectangleShape bg({static_cast<float>(m_width), static_cast<float>(m_height)});
    bg.setPosition(0.f, 0.f);
    bg.setFillColor(th.panel);
    window.draw(bg);

    sf::Color headerCol = ui::darken(th.panel, 10);
    sf::Color bandCol = th.panel;
    sf::Color hair = th.panelBorder;

    sf::RectangleShape headerBG({static_cast<float>(m_width), kHeaderH});
    headerBG.setPosition(0.f, 0.f);
    headerBG.setFillColor(headerCol);
    window.draw(headerBG);

    sf::RectangleShape fenBG({static_cast<float>(m_width), kFenH});
    fenBG.setPosition(0.f, kHeaderH);
    fenBG.setFillColor(bandCol);
    window.draw(fenBG);

    sf::RectangleShape subBG({static_cast<float>(m_width), subH});
    subBG.setPosition(0.f, kHeaderH + kFenH);
    subBG.setFillColor(bandCol);
    window.draw(subBG);

    // hairlines
    sf::RectangleShape sep({static_cast<float>(m_width), 1.f});
    sep.setFillColor(hair);
    sep.setPosition(0.f, kHeaderH);
    window.draw(sep);
    sep.setPosition(0.f, kHeaderH + kFenH);
    window.draw(sep);
    sep.setPosition(0.f, kHeaderH + kFenH + subH);
    window.draw(sep);
    sep.setPosition(0.f, listH);
    window.draw(sep);

    // list background
    sf::RectangleShape listBG({static_cast<float>(m_width), listH - topY});
    listBG.setPosition(0.f, topY);
    listBG.setFillColor(bandCol);
    window.draw(listBG);

    // --- Header title ---
    std::string headerTitle = m_replay_header ? "Replay" : (m_any_bot ? "Play Bots" : "2 Player");
    sf::Text header(headerTitle, m_font, kHeaderFontSize);
    header.setStyle(sf::Text::Bold);
    header.setFillColor(th.text);
    auto hb = header.getLocalBounds();
    header.setPosition(ui::snap({(m_width - hb.width) * 0.5f - hb.left,
                                 (kHeaderH - hb.height) * 0.5f - hb.top - 2.f}));
    window.draw(header);

    // --- Replay subheader: opening title + info list ---
    const float subY = kHeaderH + kFenH;
    if (m_replay_header)
    {
      const model::analysis::ReplayInfo &rh = *m_replay_header;

      const std::string openingTitle = buildReplayOpeningTitle(rh);
      const std::string ecoNorm = lilia::model::analysis::EcoOpeningDb::nameForEco(rh.eco).empty()
                                      ? std::string{}
                                      : ("ECO " + lilia::model::analysis::EcoOpeningDb::resolveOpeningTitle(rh.eco, rh.eco));

      // Opening title (centered)
      std::string openDisp = openingTitle.empty() ? "Unknown Opening" : openingTitle;
      openDisp = ui::ellipsizeMiddle(m_font, kReplayOpeningFontSize, openDisp,
                                     float(m_width) - 2.f * kPaddingX);

      sf::Text openTxt(openDisp, m_font, kReplayOpeningFontSize);
      openTxt.setStyle(sf::Text::Bold);
      openTxt.setFillColor(th.subtle);
      auto ob = openTxt.getLocalBounds();
      openTxt.setPosition(ui::snap({(m_width - ob.width) * 0.5f - ob.left,
                                    subY + 10.f - ob.top}));
      window.draw(openTxt);

      // Info panel box under opening title
      const sf::FloatRect infoRect{
          kPaddingX,
          subY + 48.f,
          float(m_width) - 2.f * kPaddingX,
          subH - 56.f};

      // Box background + frame
      sf::RectangleShape infoBg({infoRect.width, infoRect.height});
      infoBg.setPosition(ui::snap({infoRect.left, infoRect.top}));
      infoBg.setFillColor(ui::darken(th.panel, 4));
      window.draw(infoBg);
      ui::drawBevelFrame(window, infoRect, ui::darken(th.panel, 4), th.panelBorder);

      // Build key/value list
      std::vector<std::pair<std::string, std::string>> items;
      auto pushKV = [&](const char *k, const std::string &v)
      {
        if (!v.empty())
          items.emplace_back(k, v);
      };

      pushKV("Event", rh.event);
      pushKV("Site", rh.site);
      pushKV("Date", rh.date);
      pushKV("Round", rh.round);

      // Two-column layout
      const float innerPad = 10.f;
      const float colW = (infoRect.width - 2.f * innerPad) * 0.5f;
      const float leftX = infoRect.left + innerPad;
      const float rightX = infoRect.left + innerPad + colW;

      const float rowH = 16.f;
      const float startY = infoRect.top + 8.f;

      for (std::size_t i = 0; i < items.size(); ++i)
      {
        const bool right = (i % 2) == 1;
        const std::size_t row = i / 2;

        const float x = right ? rightX : leftX;
        const float y = startY + float(row) * rowH;

        const std::string &k = items[i].first;
        const std::string &v0 = items[i].second;

        sf::Text kTxt(k + ":", m_font, kReplayMetaFontSize);
        kTxt.setFillColor(th.subtle);
        kTxt.setPosition(ui::snap({x, y}));

        // Value: ellipsize to fit remaining space in column
        const float labelW = kTxt.getLocalBounds().width + 6.f;
        const float maxV = std::max(0.f, colW - labelW - 6.f);
        const std::string v = ui::ellipsizeMiddle(m_font, kReplayMetaFontSize, v0, maxV);

        sf::Text vTxt(v, m_font, kReplayMetaFontSize);
        vTxt.setFillColor(th.text);
        vTxt.setPosition(ui::snap({x + labelW, y}));

        window.draw(kTxt);
        window.draw(vTxt);
      }
    }
    else
    {
      // Non-replay subheader
      sf::Text sub("Move List", m_font, kSubHeaderFontSize);
      sub.setStyle(sf::Text::Bold);
      sub.setFillColor(th.subtle);
      auto sb = sub.getLocalBounds();
      sub.setPosition(ui::snap({(m_width - sb.width) * 0.5f - sb.left,
                                subY + (subH - sb.height) * 0.5f - sb.top - 2.f}));
      window.draw(sub);
    }

    // --- FEN line ---
    const bool hovFen = m_bounds_fen_icon.contains(mouseLocal.x, mouseLocal.y);
    bool showCheck = false;
    if (m_copySuccess)
    {
      const float t = m_copyClock.getElapsedTime().asSeconds();
      if (t < 2.f)
        showCheck = true;
      else
        m_copySuccess = false;
    }

    const bool leftDown = sf::Mouse::isButtonPressed(sf::Mouse::Left);
    if (leftDown && !m_prevLeftDown && hovFen)
    {
      sf::Clipboard::setString(m_fen_str);
      m_copySuccess = true;
      m_copyClock.restart();
    }
    m_prevLeftDown = leftDown;

    drawFenIcon(window, m_bounds_fen_icon, showCheck, hovFen ? th.accent : th.text);

    if (showCheck)
    {
      const float t = m_copyClock.getElapsedTime().asSeconds();
      const float prog = std::clamp(t / 2.f, 0.f, 1.f);
      sf::Text msg("copied!", m_font, kTipFontSize);
      auto mb = msg.getLocalBounds();
      sf::Vector2f c(m_bounds_fen_icon.left + m_bounds_fen_icon.width * 0.5f, m_bounds_fen_icon.top);
      float x = ui::snapf(c.x - mb.width * 0.5f - mb.left);
      float y = ui::snapf(c.y - 6.f - mb.height - mb.top - prog * 20.f);
      sf::Color col = th.text;
      col.a = static_cast<sf::Uint8>(255 * (1.f - prog));
      msg.setFillColor(col);
      msg.setPosition({x, y});
      window.draw(msg);
    }

    float textX = m_bounds_fen_icon.left + m_bounds_fen_icon.width + 6.f;
    float availW = static_cast<float>(m_width) - textX - kPaddingX;
    sf::Text probe("", m_font, kMoveFontSize);
    std::string fenDisp = ellipsizeRightKeepTail("FEN: " + m_fen_str, probe, availW);
    sf::Text fenTxt(fenDisp, m_font, kMoveFontSize);
    fenTxt.setFillColor(th.subtle);
    auto fb = fenTxt.getLocalBounds();
    fenTxt.setPosition(ui::snap({textX, kHeaderH + (kFenH - fb.height) * 0.5f - fb.top - 2.f}));
    window.draw(fenTxt);

    // --- Clip scrolling content ---
    sf::View listView(sf::FloatRect(0.f, topY, static_cast<float>(m_width), listH - topY));
    const auto winSize = window.getSize();
    listView.setViewport(sf::FloatRect(m_position.x / static_cast<float>(winSize.x),
                                       (m_position.y + topY) / static_cast<float>(winSize.y),
                                       static_cast<float>(m_width) / static_cast<float>(winSize.x),
                                       (listH - topY) / static_cast<float>(winSize.y)));
    window.setView(listView);

    const std::size_t totalLines = m_rows.size() + (m_result.empty() ? 0 : 1);

    const sf::Color rowEven = ui::lighten(th.panel, 4);
    const sf::Color rowOdd = ui::darken(th.panel, 2);
    const sf::Color hiRow = ui::lighten(th.buttonActive, 6);

    for (std::size_t i = 0; i < totalLines; ++i)
    {
      float y = topY + static_cast<float>(i) * kRowH - m_scroll_offset;
      if (y + kRowH < topY || y > listH)
        continue;

      sf::RectangleShape row({static_cast<float>(m_width), kRowH});
      row.setPosition(0.f, ui::snapf(y));
      row.setFillColor((i % 2 == 0) ? rowEven : rowOdd);
      window.draw(row);
    }

    if (m_selected_move != static_cast<std::size_t>(-1))
    {
      std::size_t rowIdx = m_selected_move / 2;
      float y = topY + static_cast<float>(rowIdx) * kRowH - m_scroll_offset;
      if (y + kRowH >= topY && y <= listH)
      {
        sf::RectangleShape hi({static_cast<float>(m_width), kRowH});
        hi.setPosition(0.f, ui::snapf(y));
        hi.setFillColor(hiRow);
        window.draw(hi);

        sf::RectangleShape bar({3.f, kRowH});
        bar.setPosition(0.f, ui::snapf(y));
        bar.setFillColor(th.accent);
        window.draw(bar);
      }
    }

    for (std::size_t i = 0; i < totalLines; ++i)
    {
      float y = topY + static_cast<float>(i) * kRowH - m_scroll_offset + 3.f;
      if (y + kRowH < topY || y > listH)
        continue;

      if (i == m_rows.size() && !m_result.empty())
      {
        sf::Text res(m_result, m_font, kMoveFontSize);
        res.setStyle(sf::Text::Bold);
        res.setFillColor(th.subtle);
        auto rb = res.getLocalBounds();
        res.setPosition(ui::snap({(m_width - rb.width) * 0.5f - rb.left, y}));
        window.draw(res);
        continue;
      }

      const Row &r = m_rows[i];

      sf::Text num(std::to_string(r.turn) + ".", m_font, kMoveNumberFontSize);
      num.setFillColor(th.subtle);
      num.setPosition(ui::snap({kPaddingX, y}));
      window.draw(num);

      const float xWhite = kPaddingX + kNumColW;
      const float xBlack = xWhite + r.whiteW + kMoveGap;

      sf::Text w(r.white, m_font, kMoveFontSize);
      w.setStyle(sf::Text::Bold);
      w.setFillColor((m_selected_move == i * 2) ? th.text : th.subtle);
      w.setPosition(ui::snap({xWhite, y}));
      window.draw(w);

      if (!r.black.empty())
      {
        sf::Text b(r.black, m_font, kMoveFontSize);
        b.setStyle(sf::Text::Bold);
        b.setFillColor((m_selected_move == i * 2 + 1) ? th.text : th.subtle);
        b.setPosition(ui::snap({xBlack, y}));
        window.draw(b);
      }
    }

    // back to panel-local view for footer
    window.setView(view);

    // --- Footer ---
    sf::RectangleShape footer({static_cast<float>(m_width), kFooterH});
    footer.setPosition(0.f, listH);
    footer.setFillColor(headerCol);
    window.draw(footer);

    const bool hovPrev = m_bounds_prev.contains(mouseLocal.x, mouseLocal.y);
    const bool hovNext = m_bounds_next.contains(mouseLocal.x, mouseLocal.y);
    const bool hovResign = m_bounds_resign.contains(mouseLocal.x, mouseLocal.y);
    const bool hovNewBot = m_bounds_new_bot.contains(mouseLocal.x, mouseLocal.y);
    const bool hovRematch = m_bounds_rematch.contains(mouseLocal.x, mouseLocal.y);

    const bool pressed = sf::Mouse::isButtonPressed(sf::Mouse::Left);

    if (m_game_over)
    {
      drawSlot(window, m_bounds_new_bot, th, hovNewBot, hovNewBot && pressed);
      drawRobot(window, m_bounds_new_bot, hovNewBot ? th.accent : th.text);

      drawSlot(window, m_bounds_rematch, th, hovRematch, hovRematch && pressed);
      drawReload(window, m_bounds_rematch, hovRematch ? th.accent : th.text);
    }
    else
    {
      drawSlot(window, m_bounds_resign, th, hovResign, hovResign && pressed);
      drawCrossX(window, m_bounds_resign, hovResign ? th.accent : th.text);
    }

    drawSlot(window, m_bounds_prev, th, hovPrev, hovPrev && pressed);
    drawChevron(window, m_bounds_prev, true, hovPrev ? th.accent : th.text);

    drawSlot(window, m_bounds_next, th, hovNext, hovNext && pressed);
    drawChevron(window, m_bounds_next, false, hovNext ? th.accent : th.text);

    if (hovPrev)
      drawTooltip(window, centerOf(m_bounds_prev), "Previous move", m_font, th);
    if (hovNext)
      drawTooltip(window, centerOf(m_bounds_next), "Next move", m_font, th);
    if (m_game_over)
    {
      if (hovNewBot)
        drawTooltip(window, centerOf(m_bounds_new_bot), "New Bot", m_font, th);
      if (hovRematch)
        drawTooltip(window, centerOf(m_bounds_rematch), "Rematch", m_font, th);
    }
    else
    {
      if (hovResign)
        drawTooltip(window, centerOf(m_bounds_resign), "Resign", m_font, th);
    }
    if (hovFen)
    {
      auto c = centerOf(m_bounds_fen_icon);
      drawTooltip(window, {c.x + 10.f, c.y}, "copy", m_font, th);
    }

    window.setView(oldView);
  }

} // namespace lilia::view
