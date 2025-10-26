#include "lilia/view/start_screen.hpp"

#include <SFML/Graphics.hpp>
#include <SFML/Window/Clipboard.hpp>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <sstream>
#include <unordered_map>

#include "lilia/bot/bot_info.hpp"
#include "lilia/model/fen_validator.hpp"
#include "lilia/model/pgn_parser.hpp"
#include "lilia/view/color_palette_manager.hpp"
#include "lilia/view/render_constants.hpp"

namespace lilia::view {

namespace {
// --------- Layout ---------
constexpr float PANEL_W = 820.f;
constexpr float PANEL_H = 520.f;

constexpr float BTN_H = 44.f;
constexpr float BTN_W = 180.f;

constexpr float LIST_ITEM_H = 36.f;

// Time panel (no shadows now)
constexpr float TIME_W = 200.f;
constexpr float TIME_H = 120.f;
constexpr float CHIP_H = 24.f;
constexpr float CHIP_GAP = 10.f;
constexpr float TOGGLE_W = TIME_W * 0.80f;
constexpr float TOGGLE_H = 30.f;

// Colors sourced from palette manager
#define colBGTop ColorPaletteManager::get().palette().COL_BG_TOP
#define colBGBottom ColorPaletteManager::get().palette().COL_BG_BOTTOM
#define colPanel ColorPaletteManager::get().palette().COL_PANEL_TRANS
#define colTextPanel ColorPaletteManager::get().palette().COL_PANEL
#define colPanelBorder ColorPaletteManager::get().palette().COL_PANEL_BORDER_ALT
#define colButton ColorPaletteManager::get().palette().COL_BUTTON
#define colButtonActive ColorPaletteManager::get().palette().COL_BUTTON_ACTIVE
#define colAccent ColorPaletteManager::get().palette().COL_ACCENT
#define colText ColorPaletteManager::get().palette().COL_TEXT
#define colSubtle ColorPaletteManager::get().palette().COL_MUTED_TEXT
#define colTimeOff ColorPaletteManager::get().palette().COL_TIME_OFF
#define colInput ColorPaletteManager::get().palette().COL_INPUT_BG
#define colInputBorder ColorPaletteManager::get().palette().COL_INPUT_BORDER
#define colValid ColorPaletteManager::get().palette().COL_VALID
#define colInvalid ColorPaletteManager::get().palette().COL_INVALID

// --------- Utils ---------
inline float snapf(float v) {
  return std::round(v);
}
inline sf::Vector2f snap(sf::Vector2f v) {
  return {snapf(v.x), snapf(v.y)};
}

inline void centerText(sf::Text& t, const sf::FloatRect& box, float dy = 0.f) {
  auto b = t.getLocalBounds();
  t.setOrigin(b.left + b.width / 2.f, b.top + b.height / 2.f);
  t.setPosition(snapf(box.left + box.width / 2.f), snapf(box.top + box.height / 2.f + dy));
}
inline void leftCenterText(sf::Text& t, const sf::FloatRect& box, float padX, float dy = 0.f) {
  auto b = t.getLocalBounds();
  t.setOrigin(b.left, b.top + b.height / 2.f);
  t.setPosition(snapf(box.left + padX), snapf(box.top + box.height / 2.f + dy));
}

inline std::string trimCopy(const std::string& s) {
  std::size_t start = 0;
  while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start]))) ++start;
  std::size_t end = s.size();
  while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1]))) --end;
  return s.substr(start, end - start);
}

void drawVerticalGradient(sf::RenderWindow& window, sf::Color top, sf::Color bottom) {
  sf::VertexArray va(sf::TriangleStrip, 4);
  auto size = window.getSize();
  va[0].position = {0.f, 0.f};
  va[1].position = {static_cast<float>(size.x), 0.f};
  va[2].position = {0.f, static_cast<float>(size.y)};
  va[3].position = {static_cast<float>(size.x), static_cast<float>(size.y)};
  va[0].color = va[1].color = top;
  va[2].color = va[3].color = bottom;
  window.draw(va);
}

inline sf::Color lighten(sf::Color c, int d) {
  auto clip = [](int x) { return std::clamp(x, 0, 255); };
  return sf::Color(clip(c.r + d), clip(c.g + d), clip(c.b + d), c.a);
}
inline sf::Color darken(sf::Color c, int d) {
  return lighten(c, -d);
}

template <typename T>
bool contains(const sf::Rect<T>& r, sf::Vector2f p) {
  return r.contains(p);
}

// Bots
std::vector<BotType> availableBots() {
  return {BotType::Lilia};
}
std::string botDisplayName(BotType t) {
  return getBotConfig(t).info.name;
}

inline std::string formatHMS(int totalSeconds) {
  totalSeconds = std::max(0, totalSeconds);
  int h = totalSeconds / 3600, m = (totalSeconds % 3600) / 60, s = totalSeconds % 60;
  std::ostringstream ss;
  ss << (h < 10 ? "0" : "") << h << ":" << (m < 10 ? "0" : "") << m << ":" << (s < 10 ? "0" : "")
     << s;
  return ss.str();
}
inline int clampBaseSeconds(int s) {
  return std::clamp(s, 60, 2 * 60 * 60);
}
inline int clampIncSeconds(int s) {
  return std::clamp(s, 0, 30);
}

// Bevel button
void drawBevelButton3D(sf::RenderTarget& t, const sf::FloatRect& r, sf::Color base, bool hovered,
                       bool pressed) {
  // No drop shadow (kept super clean)
  sf::RectangleShape body({r.width, r.height});
  body.setPosition(snapf(r.left), snapf(r.top));
  sf::Color bodyCol = base;
  if (hovered && !pressed) bodyCol = lighten(bodyCol, 8);
  if (pressed) bodyCol = darken(bodyCol, 6);
  body.setFillColor(bodyCol);
  t.draw(body);

  sf::RectangleShape top({r.width, 1.f});
  top.setPosition(snapf(r.left), snapf(r.top));
  top.setFillColor(lighten(bodyCol, 24));
  t.draw(top);
  sf::RectangleShape bot({r.width, 1.f});
  bot.setPosition(snapf(r.left), snapf(r.top + r.height - 1.f));
  bot.setFillColor(darken(bodyCol, 24));
  t.draw(bot);

  sf::RectangleShape inset({r.width - 2.f, r.height - 2.f});
  inset.setPosition(snapf(r.left + 1.f), snapf(r.top + 1.f));
  inset.setFillColor(sf::Color::Transparent);
  inset.setOutlineThickness(1.f);
  inset.setOutlineColor(darken(bodyCol, 18));
  t.draw(inset);
}

void drawAccentInset(sf::RenderTarget& t, const sf::FloatRect& r, sf::Color accent) {
  sf::RectangleShape inset({r.width - 2.f, r.height - 2.f});
  inset.setPosition(snapf(r.left + 1.f), snapf(r.top + 1.f));
  inset.setFillColor(sf::Color::Transparent);
  inset.setOutlineThickness(1.f);
  inset.setOutlineColor(accent);
  t.draw(inset);
}

}  // namespace

// ---------------------- class impl ----------------------

StartScreen::StartScreen(sf::RenderWindow& window) : m_window(window) {
  m_font.loadFromFile(constant::STR_FILE_PATH_FONT);
  m_logoTex.loadFromFile(constant::STR_FILE_PATH_ICON_LILIA_START_SCREEN);
  m_logo.setTexture(m_logoTex);

  // FEN starts empty => STANDARD unless user provides one
  m_fenString.clear();

  // Time defaults (off)
  m_baseSeconds = 300;
  m_incrementSeconds = 0;
  m_timeEnabled = false;

  setupUI();
  applyTheme();
  m_listener_id = ColorPaletteManager::get().addListener([this]() { applyTheme(); });
}

StartScreen::~StartScreen() {
  ColorPaletteManager::get().removeListener(m_listener_id);
}

void StartScreen::setupUI() {
  const sf::Vector2u ws = m_window.getSize();

  // palette button position and options
  m_paletteText.setFont(m_font);
  m_paletteText.setString("Color Theme");
  m_paletteText.setCharacterSize(16);
  m_paletteText.setFillColor(colText);
  auto tb = m_paletteText.getLocalBounds();
  float pad = 8.f;
  m_paletteButton.setSize({tb.width + pad * 2.f, tb.height + pad * 2.f});
  m_paletteButton.setFillColor(colButton);
  m_paletteButton.setPosition(20.f, ws.y - m_paletteButton.getSize().y - 20.f);
  m_paletteText.setPosition(snapf(m_paletteButton.getPosition().x + pad - tb.left),
                            snapf(m_paletteButton.getPosition().y + pad - tb.top));

  m_paletteOptions.clear();
  float itemH = 24.f;
  float width = 120.f;
  float left = m_paletteButton.getPosition().x - 1.f;
  float bottom = m_paletteButton.getPosition().y;
  const auto& names = ColorPaletteManager::get().paletteNames();
  for (std::size_t i = 0; i < names.size(); ++i) {
    PaletteOption opt;
    opt.name = names[i];
    opt.box.setSize({width, itemH});
    opt.box.setPosition(snap({left, bottom - (i + 1) * itemH}));
    opt.box.setFillColor(colButton);
    opt.label.setFont(m_font);
    opt.label.setCharacterSize(14);
    std::string label = names[i];
    opt.label.setString(label);
    opt.label.setFillColor(colText);
    leftCenterText(opt.label, opt.box.getGlobalBounds(), 8.f);
    m_paletteOptions.push_back(opt);
  }
  const std::string& activeName = ColorPaletteManager::get().activePalette();
  m_paletteSelection = 0;
  for (std::size_t i = 0; i < names.size(); ++i) {
    if (names[i] == activeName) {
      m_paletteSelection = i;
      break;
    }
  }

  // Headings
  m_whiteLabel.setFont(m_font);
  m_whiteLabel.setString("White");
  m_whiteLabel.setCharacterSize(22);
  m_whiteLabel.setFillColor(colText);
  m_blackLabel.setFont(m_font);
  m_blackLabel.setString("Black");
  m_blackLabel.setCharacterSize(22);
  m_blackLabel.setFillColor(colText);

  // Player/Bot buttons
  auto initSideBtns = [&](sf::RectangleShape& humanBtn, sf::RectangleShape& botBtn,
                          sf::Text& humanTxt, sf::Text& botTxt) {
    humanBtn.setSize({BTN_W, BTN_H});
    botBtn.setSize({BTN_W, BTN_H});
    humanBtn.setFillColor(colButton);
    botBtn.setFillColor(colButton);
    humanBtn.setOutlineThickness(0.f);
    botBtn.setOutlineThickness(0.f);
    humanTxt.setFont(m_font);
    botTxt.setFont(m_font);
    humanTxt.setCharacterSize(18);
    botTxt.setCharacterSize(18);
    humanTxt.setFillColor(colText);
    botTxt.setFillColor(colText);
    humanTxt.setString("Human");
    botTxt.setString(botDisplayName(BotType::Lilia));
  };
  initSideBtns(m_whitePlayerBtn, m_whiteBotBtn, m_whitePlayerText, m_whiteBotText);
  initSideBtns(m_blackPlayerBtn, m_blackBotBtn, m_blackPlayerText, m_blackBotText);

  // Start
  m_startBtn.setSize({260.f, 54.f});
  m_startBtn.setFillColor(colAccent);
  m_startBtn.setOutlineThickness(0);
  m_startText.setFont(m_font);
  m_startText.setString("Start Game");
  m_startText.setCharacterSize(22);
  m_startText.setFillColor(constant::COL_DARK_TEXT);

  // Layout anchors
  float x0 = (ws.x - PANEL_W) * 0.5f;
  float y0 = (ws.y - PANEL_H) * 0.5f;

  m_whiteLabel.setPosition(snapf(x0 + 80.f), snapf(y0 + 100.f));
  m_blackLabel.setPosition(snapf(x0 + PANEL_W - 80.f - m_blackLabel.getLocalBounds().width),
                           snapf(y0 + 100.f));

  m_whitePlayerBtn.setPosition(snapf(x0 + 60.f), snapf(y0 + 150.f));
  m_whiteBotBtn.setPosition(snapf(x0 + 60.f), snapf(y0 + 208.f));

  m_blackPlayerBtn.setPosition(snapf(x0 + PANEL_W - 60.f - BTN_W), snapf(y0 + 150.f));
  m_blackBotBtn.setPosition(snapf(x0 + PANEL_W - 60.f - BTN_W), snapf(y0 + 208.f));

  m_startBtn.setPosition(snapf(x0 + (PANEL_W - m_startBtn.getSize().x) / 2.f),
                         snapf(y0 + PANEL_H - 120.f));
  centerText(m_startText, m_startBtn.getGlobalBounds());

  // FEN error hint above Start button
  m_fenErrorText.setFont(m_font);
  m_fenErrorText.setString("STANDARD FEN");
  m_fenErrorText.setCharacterSize(14);
  m_fenErrorText.setFillColor(colInvalid);
  centerText(m_fenErrorText, m_startBtn.getGlobalBounds(), -(m_startBtn.getSize().y / 2.f + 15.f));

  // Load game section (FEN & PGN)
  const float loadWidth = PANEL_W * 0.95f;
  const float loadX = x0 + (PANEL_W - loadWidth) * 0.5f;
  const float loadTop = m_startBtn.getPosition().y + m_startBtn.getSize().y + 20.f;

  m_loadHeader.setFont(m_font);
  m_loadHeader.setString("Load Game");
  m_loadHeader.setCharacterSize(18);
  m_loadHeader.setFillColor(colText);
  m_loadHeader.setPosition(snapf(loadX), snapf(loadTop));

  m_fenLabel.setFont(m_font);
  m_fenLabel.setCharacterSize(14);
  m_fenLabel.setFillColor(colSubtle);
  m_fenLabel.setString("FEN (optional)");
  m_fenLabel.setPosition(snapf(loadX), snapf(loadTop + 28.f));

  const float fenW = loadWidth;
  const float fenH = 22.f;
  const float fenY = loadTop + 48.f;

  m_fenInputBox.setSize({fenW, fenH});
  m_fenInputBox.setFillColor(colInput);
  m_fenInputBox.setOutlineThickness(2.f);
  m_fenInputBox.setOutlineColor(colInputBorder);
  m_fenInputBox.setPosition(snapf(loadX), snapf(fenY));

  m_fenInputText.setFont(m_font);
  m_fenInputText.setCharacterSize(15);
  m_fenInputText.setFillColor(colText);
  m_fenInputText.setString(m_fenString);
  leftCenterText(m_fenInputText, m_fenInputBox.getGlobalBounds(), 8.f);

  m_pgnLabel.setFont(m_font);
  m_pgnLabel.setCharacterSize(14);
  m_pgnLabel.setFillColor(colSubtle);
  m_pgnLabel.setString("PGN (optional)");
  const float pgnLabelY = fenY + fenH + 18.f;
  m_pgnLabel.setPosition(snapf(loadX), snapf(pgnLabelY));

  const float pgnH = 108.f;
  const float pgnY = pgnLabelY + 18.f;
  m_pgnInputBox.setSize({loadWidth, pgnH});
  m_pgnInputBox.setFillColor(colInput);
  m_pgnInputBox.setOutlineThickness(2.f);
  m_pgnInputBox.setOutlineColor(colInputBorder);
  m_pgnInputBox.setPosition(snapf(loadX), snapf(pgnY));

  m_pgnInputText.setFont(m_font);
  m_pgnInputText.setCharacterSize(15);
  m_pgnInputText.setFillColor(colText);
  m_pgnInputText.setString("");

  m_pgnStatusText.setFont(m_font);
  m_pgnStatusText.setCharacterSize(14);
  m_pgnStatusText.setFillColor(colSubtle);
  m_pgnStatusText.setString("");
  m_pgnStatusText.setPosition(snapf(loadX), snapf(pgnY + pgnH + 8.f));

  // Build bot option lists
  auto bots = availableBots();
  auto buildList = [&](std::vector<BotOption>& out, float left, float top) {
    out.clear();
    for (std::size_t i = 0; i < bots.size(); ++i) {
      BotOption opt;
      opt.type = bots[i];
      opt.box.setSize({BTN_W, LIST_ITEM_H});
      opt.box.setPosition(snapf(left), snapf(top + (float)i * LIST_ITEM_H));
      opt.box.setFillColor(colButton);
      opt.label.setFont(m_font);
      opt.label.setCharacterSize(16);
      opt.label.setString(botDisplayName(bots[i]));
      opt.label.setFillColor(colText);
      leftCenterText(opt.label, opt.box.getGlobalBounds(), 10.f);
      out.push_back(opt);
    }
  };
  buildList(m_whiteBotOptions, m_whiteBotBtn.getPosition().x - 1.f,
            m_whiteBotBtn.getPosition().y + BTN_H);
  buildList(m_blackBotOptions, m_blackBotBtn.getPosition().x - 1.f,
            m_blackBotBtn.getPosition().y + BTN_H);

  // Time block
  const float timeX = x0 + (PANEL_W - TIME_W) * 0.5f;
  const float timeY = y0 + (PANEL_H - TIME_H) * 0.5f;

  m_timeToggleBtn.setSize({TOGGLE_W, TOGGLE_H});
  m_timeToggleBtn.setPosition(snap({x0 + (PANEL_W - TOGGLE_W) * 0.5f, timeY - 56.f}));
  m_timeToggleBtn.setOutlineThickness(0.f);
  m_timeToggleText.setFont(m_font);
  m_timeToggleText.setCharacterSize(16);

  m_timePanel.setSize({TIME_W, TIME_H});
  m_timePanel.setPosition(snap({timeX, timeY}));
  m_timePanel.setFillColor(ColorPaletteManager::get().palette().COL_HEADER);
  m_timePanel.setOutlineThickness(1.f);
  m_timePanel.setOutlineColor(colPanelBorder);

  m_timeTitle.setFont(m_font);
  m_timeTitle.setCharacterSize(14);
  m_timeTitle.setFillColor(colSubtle);
  m_timeTitle.setString("Time Control");
  m_timeTitle.setPosition(snap({timeX + 10.f, timeY + 8.f}));

  // Warning popup (inactive by default)
  sf::Vector2u ws = m_window.getSize();
  m_warningBackdrop.setSize(sf::Vector2f(static_cast<float>(ws.x), static_cast<float>(ws.y)));
  m_warningBackdrop.setFillColor(sf::Color(0, 0, 0, 120));

  const sf::Vector2f warnSize(420.f, 220.f);
  m_warningPanel.setSize(warnSize);
  m_warningPanel.setFillColor(ColorPaletteManager::get().palette().COL_PANEL);
  m_warningPanel.setOutlineThickness(1.f);
  m_warningPanel.setOutlineColor(colPanelBorder);
  m_warningPanel.setPosition(snapf((ws.x - warnSize.x) * 0.5f), snapf((ws.y - warnSize.y) * 0.5f));

  m_warningTitle.setFont(m_font);
  m_warningTitle.setCharacterSize(20);
  m_warningTitle.setFillColor(colText);
  m_warningTitle.setString("Invalid Load Data");
  auto warnPos = m_warningPanel.getPosition();
  m_warningTitle.setPosition(snapf(warnPos.x + 24.f), snapf(warnPos.y + 22.f));

  m_warningMessage.setFont(m_font);
  m_warningMessage.setCharacterSize(16);
  m_warningMessage.setFillColor(colSubtle);
  m_warningMessage.setString("");
  m_warningMessage.setPosition(snapf(warnPos.x + 24.f), snapf(warnPos.y + 64.f));

  const sf::Vector2f btnSize(140.f, 40.f);
  const float btnSpacing = 24.f;
  float btnTotal = btnSize.x * 2.f + btnSpacing;
  float btnStartX = warnPos.x + (warnSize.x - btnTotal) * 0.5f;
  float btnY = warnPos.y + warnSize.y - btnSize.y - 24.f;

  m_warningCancelBtn.setSize(btnSize);
  m_warningCancelBtn.setPosition(snapf(btnStartX), snapf(btnY));
  m_warningCancelBtn.setFillColor(colButton);
  m_warningCancelBtn.setOutlineThickness(0.f);

  m_warningProceedBtn.setSize(btnSize);
  m_warningProceedBtn.setPosition(snapf(btnStartX + btnSize.x + btnSpacing), snapf(btnY));
  m_warningProceedBtn.setFillColor(colAccent);
  m_warningProceedBtn.setOutlineThickness(0.f);

  m_warningCancelText.setFont(m_font);
  m_warningCancelText.setCharacterSize(16);
  m_warningCancelText.setFillColor(colText);
  m_warningCancelText.setString("Cancel");
  centerText(m_warningCancelText, m_warningCancelBtn.getGlobalBounds());

  m_warningProceedText.setFont(m_font);
  m_warningProceedText.setCharacterSize(16);
  m_warningProceedText.setFillColor(constant::COL_DARK_TEXT);
  m_warningProceedText.setString("Proceed");
  centerText(m_warningProceedText, m_warningProceedBtn.getGlobalBounds());

  m_timeMain.setFont(m_font);
  m_timeMain.setCharacterSize(22);
  m_timeMain.setFillColor(colText);
  m_timeMain.setString(formatHMS(m_baseSeconds));

  m_timeMinusBtn.setSize({28.f, 26.f});
  m_timePlusBtn.setSize({28.f, 26.f});
  m_timeMinusBtn.setFillColor(colButton);
  m_timePlusBtn.setFillColor(colButton);
  m_minusTxt.setFont(m_font);
  m_minusTxt.setCharacterSize(18);
  m_minusTxt.setFillColor(colText);
  m_minusTxt.setString("-");
  m_plusTxt = m_minusTxt;
  m_plusTxt.setString("+");

  m_incLabel.setFont(m_font);
  m_incLabel.setCharacterSize(12);
  m_incLabel.setFillColor(colSubtle);
  m_incLabel.setString("Increment");
  m_incValue.setFont(m_font);
  m_incValue.setCharacterSize(16);
  m_incValue.setFillColor(colText);
  m_incValue.setString("+" + std::to_string(m_incrementSeconds) + "s");
  m_incMinusBtn.setSize({24.f, 22.f});
  m_incPlusBtn.setSize({24.f, 22.f});
  m_incMinusBtn.setFillColor(colButton);
  m_incPlusBtn.setFillColor(colButton);
  m_incMinusTxt.setFont(m_font);
  m_incMinusTxt.setCharacterSize(16);
  m_incMinusTxt.setFillColor(colText);
  m_incMinusTxt.setString("-");
  m_incPlusTxt = m_incMinusTxt;
  m_incPlusTxt.setString("+");

  // Presets
  m_presets.clear();
  auto makeChip = [&](const char* label, int base, int inc) {
    PresetChip c;
    float chipW = 74.f;
    c.box.setSize({chipW, CHIP_H});
    c.box.setFillColor(colButton);
    c.box.setOutlineThickness(0.f);
    c.label.setFont(m_font);
    c.label.setCharacterSize(13);
    c.label.setFillColor(colText);
    c.label.setString(label);
    c.base = base;
    c.inc = inc;
    m_presets.push_back(std::move(c));
  };
  makeChip("Bullet", 60, 0);
  makeChip("Blitz", 180, 2);
  makeChip("Rapid", 600, 0);

  auto layoutTimeControls = [&]() {
    const sf::Vector2f p = m_timePanel.getPosition();
    float row1Y = p.y + 42.f;
    const float gap = 10.f, mw = m_timeMinusBtn.getSize().x, pw = m_timePlusBtn.getSize().x;
    auto mb = m_timeMain.getLocalBounds();
    float totalW = mw + gap + mb.width + gap + pw;
    float left = p.x + (TIME_W - totalW) * 0.5f;

    m_timeMinusBtn.setPosition(snap({left, row1Y - m_timeMinusBtn.getSize().y * 0.5f}));
    m_timePlusBtn.setPosition(
        snap({left + mw + gap + mb.width + gap, row1Y - m_timePlusBtn.getSize().y * 0.5f}));

    sf::FloatRect minusGB = m_timeMinusBtn.getGlobalBounds();
    sf::FloatRect midBox(minusGB.left + minusGB.width + gap, row1Y - 14.f, mb.width, 28.f);
    centerText(m_timeMain, midBox);
    centerText(m_minusTxt, m_timeMinusBtn.getGlobalBounds());
    centerText(m_plusTxt, m_timePlusBtn.getGlobalBounds());

    float row2Y = p.y + 80.f;
    m_incLabel.setPosition(snap({p.x + 10.f, row2Y - 9.f}));
    const float incRight = p.x + TIME_W - 10.f;
    m_incPlusBtn.setPosition(
        snap({incRight - m_incPlusBtn.getSize().x, row2Y - m_incPlusBtn.getSize().y * 0.5f}));
    m_incMinusBtn.setPosition(snap({m_incPlusBtn.getPosition().x - 6.f - m_incMinusBtn.getSize().x,
                                    row2Y - m_incMinusBtn.getSize().y * 0.5f}));

    sf::FloatRect incValBox(m_incMinusBtn.getPosition().x - 6.f - 58.f, row2Y - 12.f, 58.f, 24.f);
    centerText(m_incValue, incValBox);
    centerText(m_incMinusTxt, m_incMinusBtn.getGlobalBounds());
    centerText(m_incPlusTxt, m_incPlusBtn.getGlobalBounds());

    float yChips = p.y + TIME_H + 12.f;
    float chipsTotalW = 3.f * m_presets[0].box.getSize().x + 2.f * CHIP_GAP;
    float chipsLeft = p.x + (TIME_W - chipsTotalW) * 0.5f;
    for (std::size_t i = 0; i < m_presets.size(); ++i) {
      float x = chipsLeft + i * (m_presets[i].box.getSize().x + CHIP_GAP);
      m_presets[i].box.setPosition(snap({x, yChips}));
      centerText(m_presets[i].label, m_presets[i].box.getGlobalBounds(), -1.f);
    }
  };

  m_timeMain.setString(formatHMS(m_baseSeconds));
  m_incValue.setString("+" + std::to_string(m_incrementSeconds) + "s");
  updateTimeToggle();
  layoutTimeControls();
}

void StartScreen::applyTheme() {
  m_paletteText.setFillColor(colText);
  m_paletteButton.setFillColor(colButton);
  for (auto& opt : m_paletteOptions) {
    opt.box.setFillColor(colButton);
    opt.label.setFillColor(colText);
  }

  m_whiteLabel.setFillColor(colText);
  m_blackLabel.setFillColor(colText);

  m_whitePlayerBtn.setFillColor(colButton);
  m_whiteBotBtn.setFillColor(colButton);
  m_whitePlayerText.setFillColor(colText);
  m_whiteBotText.setFillColor(colText);
  m_blackPlayerBtn.setFillColor(colButton);
  m_blackBotBtn.setFillColor(colButton);
  m_blackPlayerText.setFillColor(colText);
  m_blackBotText.setFillColor(colText);

  m_startBtn.setFillColor(colAccent);
  m_startText.setFillColor(constant::COL_DARK_TEXT);

  m_fenErrorText.setFillColor(colInvalid);
  m_fenInputBox.setFillColor(colInput);
  m_fenInputBox.setOutlineColor(colInputBorder);
  m_fenInputText.setFillColor(colText);
  m_loadHeader.setFillColor(colText);
  m_fenLabel.setFillColor(colSubtle);
  m_pgnLabel.setFillColor(colSubtle);
  m_pgnStatusText.setFillColor(colSubtle);
  m_pgnInputBox.setFillColor(colInput);
  m_pgnInputBox.setOutlineColor(colInputBorder);
  m_pgnInputText.setFillColor(colText);

  for (auto& opt : m_whiteBotOptions) {
    opt.box.setFillColor(colButton);
    opt.label.setFillColor(colText);
  }
  for (auto& opt : m_blackBotOptions) {
    opt.box.setFillColor(colButton);
    opt.label.setFillColor(colText);
  }

  m_timePanel.setFillColor(ColorPaletteManager::get().palette().COL_HEADER);
  m_timePanel.setOutlineColor(colPanelBorder);
  m_timeTitle.setFillColor(colSubtle);
  m_timeMain.setFillColor(colText);
  m_timeMinusBtn.setFillColor(colButton);
  m_timePlusBtn.setFillColor(colButton);
  m_minusTxt.setFillColor(colText);
  m_incLabel.setFillColor(colSubtle);
  m_incValue.setFillColor(colText);
  m_incMinusBtn.setFillColor(colButton);
  m_incPlusBtn.setFillColor(colButton);
  m_incMinusTxt.setFillColor(colText);

  for (auto& c : m_presets) {
    c.box.setFillColor(colButton);
    c.label.setFillColor(colText);
  }

  m_warningPanel.setFillColor(ColorPaletteManager::get().palette().COL_PANEL);
  m_warningPanel.setOutlineColor(colPanelBorder);
  m_warningTitle.setFillColor(colText);
  m_warningMessage.setFillColor(colSubtle);
  m_warningCancelBtn.setFillColor(colButton);
  m_warningProceedBtn.setFillColor(colAccent);
  m_warningCancelText.setFillColor(colText);
  m_warningProceedText.setFillColor(constant::COL_DARK_TEXT);

  updateTimeToggle();
}

void StartScreen::updateTimeToggle() {
  if (m_timeEnabled) {
    m_timeToggleBtn.setFillColor(colAccent);
    m_timeToggleText.setFillColor(constant::COL_DARK_TEXT);
    m_timeToggleText.setString("TIME ON");
  } else {
    m_timeToggleBtn.setFillColor(colTimeOff);
    m_timeToggleText.setFillColor(colText);
    m_timeToggleText.setString("TIME OFF");
  }
  centerText(m_timeToggleText, m_timeToggleBtn.getGlobalBounds());
}

static void drawPanelWithShadow(sf::RenderWindow& win, const sf::Vector2f& topLeft) {
  // Keep panel shadowed (overall), not the clock panel
  sf::FloatRect rect(topLeft.x, topLeft.y, PANEL_W, PANEL_H);
  // soft shadow
  for (int i = 3; i >= 1; --i) {
    float grow = (float)i * 6.f;
    sf::RectangleShape s({rect.width + 2.f * grow, rect.height + 2.f * grow});
    s.setPosition(snapf(rect.left - grow), snapf(rect.top - grow));
    sf::Color sc(0, 0, 0, (sf::Uint8)(30 * i));
    s.setFillColor(sc);
    win.draw(s);
  }
  // border + body
  sf::RectangleShape border({rect.width + 2.f, rect.height + 2.f});
  border.setPosition(snapf(rect.left - 1.f), snapf(rect.top - 1.f));
  border.setFillColor(colPanelBorder);
  win.draw(border);
  sf::RectangleShape panel({rect.width, rect.height});
  panel.setPosition(snapf(rect.left), snapf(rect.top));
  panel.setFillColor(colPanel);
  win.draw(panel);
}

bool StartScreen::handleMouse(sf::Vector2f pos, StartConfig& cfg) {
  if (m_showPaletteList) {
    for (std::size_t i = 0; i < m_paletteOptions.size(); ++i) {
      auto& opt = m_paletteOptions[i];
      if (contains(opt.box.getGlobalBounds(), pos)) {
        ColorPaletteManager::get().setPalette(opt.name);
        setupUI();
        m_showPaletteList = false;
        m_paletteListForceHide = true;
        return false;
      }
    }
  }

  // White toggles
  if (contains(m_whitePlayerBtn.getGlobalBounds(), pos)) {
    cfg.whiteIsBot = false;
    return false;
  }
  if (contains(m_whiteBotBtn.getGlobalBounds(), pos)) {
    cfg.whiteIsBot = true;
    return false;
  }
  if (m_showWhiteBotList) {
    for (std::size_t i = 0; i < m_whiteBotOptions.size(); ++i) {
      if (contains(m_whiteBotOptions[i].box.getGlobalBounds(), pos)) {
        m_whiteBotSelection = i;
        cfg.whiteBot = m_whiteBotOptions[i].type;
        cfg.whiteIsBot = true;
        m_whiteBotText.setString(botDisplayName(cfg.whiteBot));
        m_showWhiteBotList = false;
        m_whiteListForceHide = true;
        return false;
      }
    }
  }

  // Black toggles
  if (contains(m_blackPlayerBtn.getGlobalBounds(), pos)) {
    cfg.blackIsBot = false;
    return false;
  }
  if (contains(m_blackBotBtn.getGlobalBounds(), pos)) {
    cfg.blackIsBot = true;
    return false;
  }
  if (m_showBlackBotList) {
    for (std::size_t i = 0; i < m_blackBotOptions.size(); ++i) {
      if (contains(m_blackBotOptions[i].box.getGlobalBounds(), pos)) {
        m_blackBotSelection = i;
        cfg.blackBot = m_blackBotOptions[i].type;
        cfg.blackIsBot = true;
        m_blackBotText.setString(botDisplayName(cfg.blackBot));
        m_showBlackBotList = false;
        m_blackListForceHide = true;
        return false;
      }
    }
  }

  // Time presets (if enabled)
  if (m_timeEnabled) {
    for (std::size_t i = 0; i < m_presets.size(); ++i) {
      auto& chip = m_presets[i];
      if (contains(chip.box.getGlobalBounds(), pos)) {
        m_presetSelection = (int)i;
        m_baseSeconds = clampBaseSeconds(chip.base);
        m_incrementSeconds = clampIncSeconds(chip.inc);
        m_timeMain.setString(formatHMS(m_baseSeconds));
        m_incValue.setString("+" + std::to_string(m_incrementSeconds) + "s");
        return false;
      }
    }
  }

  // Start
  if (contains(m_startBtn.getGlobalBounds(), pos)) return true;

  return false;
}

bool StartScreen::handleFenMouse(sf::Vector2f pos, StartConfig& cfg) {
  (void)pos;
  (void)cfg;
  return false;
}

bool StartScreen::isValidFen(const std::string& fen) {
  return model::fen::is_basic_fen_valid(fen);
}

void StartScreen::updateWarningMessage() {
  std::string msg;
  if (m_warnFenInvalid)
    msg += "• FEN is invalid. The standard starting position will be used.\n";
  if (m_warnPgnInvalid)
    msg += "• PGN is invalid. No PGN will be loaded.\n";
  if (msg.empty()) msg = "All load data is valid.";
  m_warningMessage.setString(msg);
}

void StartScreen::processHoldRepeater(HoldRepeater& r, const sf::FloatRect& bounds,
                                      sf::Vector2f mouse, std::function<void()> stepFn,
                                      float initialDelay, float repeatRate) {
  if (!r.active) return;
  if (!bounds.contains(mouse)) return;
  float t = r.clock.getElapsedTime().asSeconds();
  if (t < initialDelay) return;
  int ticks = (int)((t - initialDelay) / repeatRate);
  while (r.fired < ticks) {
    stepFn();
    r.fired++;
  }
}

StartConfig StartScreen::run() {
  StartConfig cfg;
  cfg.whiteIsBot = false;
  cfg.blackIsBot = true;
  cfg.whiteBot = BotType::Lilia;
  cfg.blackBot = BotType::Lilia;
  cfg.fen = core::START_FEN;
  cfg.timeBaseSeconds = m_baseSeconds;
  cfg.timeIncrementSeconds = m_incrementSeconds;
  cfg.timeEnabled = m_timeEnabled;

  bool fenInputActive = false;
  bool fenUserEdited = false;
  bool pgnInputActive = false;

  const float fenPadX = 8.f;
  const float pgnPadX = 8.f;
  const float pgnPadY = 6.f;

  bool toastVisible = false;
  sf::Clock toastClock;
  std::string toastMsg;

  sf::Clock caretClock;

  std::optional<model::pgn::ParsedPgn> parsedPgn;
  std::string pgnError;
  bool pgnDirty = true;

  auto recomputePgn = [&]() {
    if (m_pgnString.empty()) {
      parsedPgn.reset();
      pgnError.clear();
      return;
    }
    parsedPgn = model::pgn::parse(m_pgnString, &pgnError);
    if (!parsedPgn) {
      if (pgnError.empty()) pgnError = "PGN is invalid.";
    } else {
      pgnError.clear();
    }
  };

  auto refreshPgn = [&]() {
    if (pgnDirty) {
      recomputePgn();
      pgnDirty = false;
    }
  };

  auto computeCurrentConfig = [&](bool applyDefaults) {
    refreshPgn();
    StartConfig out = cfg;
    out.timeBaseSeconds = m_baseSeconds;
    out.timeIncrementSeconds = m_incrementSeconds;
    out.timeEnabled = m_timeEnabled;

    std::string trimmedFen = trimCopy(m_fenString);
    bool fenProvided = !trimmedFen.empty();
    bool fenValid = trimmedFen.empty() || isValidFen(trimmedFen);

    bool pgnProvided = !m_pgnString.empty();
    bool pgnValid = parsedPgn.has_value();

    if (applyDefaults) {
      if (m_warnFenInvalid) {
        m_fenString.clear();
        trimmedFen.clear();
        fenProvided = false;
        fenValid = true;
        fenUserEdited = false;
      }
      if (m_warnPgnInvalid) {
        m_pgnString.clear();
        parsedPgn.reset();
        pgnError.clear();
        pgnProvided = false;
        pgnValid = false;
        pgnDirty = false;
      }
    }

    if (pgnProvided && pgnValid) {
      out.loadFromPgn = true;
      out.pgn = parsedPgn->sanitized;
      out.fen = parsedPgn->finalFen;
    } else {
      out.loadFromPgn = false;
      out.pgn.clear();
      if (fenProvided && fenValid)
        out.fen = trimmedFen;
      else
        out.fen = core::START_FEN;
    }
    return out;
  };

  struct WarningState {
    bool fen{false};
    bool pgn{false};
  };

  auto gatherWarnings = [&]() -> WarningState {
    refreshPgn();
    std::string trimmedFen = trimCopy(m_fenString);
    bool fenProvided = !trimmedFen.empty();
    bool fenValid = trimmedFen.empty() || isValidFen(trimmedFen);
    bool pgnProvided = !m_pgnString.empty();
    bool pgnValid = parsedPgn.has_value();
    WarningState ws;
    ws.fen = fenProvided && !fenValid;
    ws.pgn = pgnProvided && !pgnValid;
    return ws;
  };

  auto attemptStart = [&]() -> std::optional<StartConfig> {
    WarningState ws = gatherWarnings();
    if (ws.fen || ws.pgn) {
      m_warnFenInvalid = ws.fen;
      m_warnPgnInvalid = ws.pgn;
      m_showLoadWarning = true;
      updateWarningMessage();
      return std::nullopt;
    }
    return computeCurrentConfig(false);
  };

  auto wrapPgnText = [&](const std::string& text) {
    std::vector<std::string> lines;
    std::string current;
    std::string word;
    const float maxWidth = m_pgnInputBox.getSize().x - 2.f * pgnPadX;

    auto pushWord = [&](const std::string& w) {
      if (w.empty()) return;
      std::string candidate = current.empty() ? w : current + " " + w;
      sf::Text probe(candidate, m_font, 15);
      if (probe.getLocalBounds().width <= maxWidth) {
        current = candidate;
      } else {
        if (!current.empty()) lines.push_back(current);
        current = w;
      }
    };

    for (char c : text) {
      if (c == '\r') continue;
      if (c == '\n') {
        if (!word.empty()) {
          pushWord(word);
          word.clear();
        }
        if (!current.empty()) {
          lines.push_back(current);
          current.clear();
        } else {
          lines.emplace_back();
        }
        continue;
      }
      if (std::isspace(static_cast<unsigned char>(c))) {
        pushWord(word);
        word.clear();
      } else {
        word.push_back(c);
      }
    }
    if (!word.empty()) {
      pushWord(word);
      word.clear();
    }
    if (!current.empty()) lines.push_back(current);
    if (lines.empty()) lines.emplace_back();
    return lines;
  };

  auto drawUI = [&]() {
    drawVerticalGradient(m_window, colBGTop, colBGBottom);

    bool palHover = contains(m_paletteButton.getGlobalBounds(), m_mousePos) || m_showPaletteList ||
                    m_paletteListAnim > 0.f;
    m_paletteButton.setFillColor(palHover ? colButtonActive : colButton);
    m_paletteText.setFillColor(colText);
    m_window.draw(m_paletteButton);
    m_window.draw(m_paletteText);
    if (m_paletteListAnim > 0.f) {
      for (std::size_t i = 0; i < m_paletteOptions.size(); ++i) {
        const auto& opt = m_paletteOptions[i];
        auto r = opt.box.getGlobalBounds();
        bool hov = contains(r, m_mousePos);
        bool sel = (i == m_paletteSelection);
        sf::Color base = sel ? colButtonActive : colButton;
        base.a = static_cast<sf::Uint8>(base.a * m_paletteListAnim);
        drawBevelButton3D(m_window, r, base, hov, sel);
        sf::Text label = opt.label;
        sf::Color lc = label.getFillColor();
        lc.a = static_cast<sf::Uint8>(lc.a * m_paletteListAnim);
        label.setFillColor(lc);
        leftCenterText(label, r, 8.f);
        m_window.draw(label);
        if (sel) {
          sf::Color ac = colAccent;
          ac.a = static_cast<sf::Uint8>(ac.a * m_paletteListAnim);
          drawAccentInset(m_window, r, ac);
        }
      }
    }

    if (m_logoTex.getSize().x > 0 && m_logoTex.getSize().y > 0) {
      sf::Sprite logoBG(m_logoTex);
      const auto ws = m_window.getSize();
      const float desiredH = ws.y * 0.90f;
      const float s = desiredH / (float)m_logoTex.getSize().y;
      logoBG.setScale(s, s);
      auto lb = logoBG.getLocalBounds();
      logoBG.setOrigin(lb.width, 0.f);
      logoBG.setPosition(snapf((float)ws.x - 24.f), snapf(24.f));
      logoBG.setColor(ColorPaletteManager::get().palette().COL_LOGO_BG);
      m_window.draw(logoBG, sf::RenderStates(sf::BlendAlpha));
    }

    sf::Vector2f panelPos((m_window.getSize().x - PANEL_W) * 0.5f,
                          (m_window.getSize().y - PANEL_H) * 0.5f);
    drawPanelWithShadow(m_window, panelPos);

    sf::Text title("Lilia Engine - Bot Sandbox", m_font, 28);
    title.setFillColor(colText);
    title.setPosition(snapf(panelPos.x + 24.f), snapf(panelPos.y + 18.f));
    m_window.draw(title);

    sf::Text subtitle("Try different chess bots. Choose sides & engine.", m_font, 18);
    subtitle.setFillColor(colSubtle);
    subtitle.setPosition(snapf(panelPos.x + 24.f), snapf(panelPos.y + 52.f));
    m_window.draw(subtitle);

    m_window.draw(m_whiteLabel);
    m_window.draw(m_blackLabel);

    {
      auto humanR = m_whitePlayerBtn.getGlobalBounds();
      auto botR = m_whiteBotBtn.getGlobalBounds();
      bool hovH = contains(humanR, m_mousePos);
      bool hovB = contains(botR, m_mousePos) || m_showWhiteBotList || m_whiteBotListAnim > 0.f;
      bool selH = !cfg.whiteIsBot, selB = cfg.whiteIsBot;
      drawBevelButton3D(m_window, humanR, selH ? colButtonActive : colButton, hovH, selH);
      centerText(m_whitePlayerText, humanR);
      m_window.draw(m_whitePlayerText);
      if (selH) drawAccentInset(m_window, humanR, colAccent);
      drawBevelButton3D(m_window, botR, selB ? colButtonActive : colButton, hovB, selB);
      centerText(m_whiteBotText, botR);
      m_window.draw(m_whiteBotText);
      if (selB) drawAccentInset(m_window, botR, colAccent);
    }

    {
      auto humanR = m_blackPlayerBtn.getGlobalBounds();
      auto botR = m_blackBotBtn.getGlobalBounds();
      bool hovH = contains(humanR, m_mousePos);
      bool hovB = contains(botR, m_mousePos) || m_showBlackBotList || m_blackBotListAnim > 0.f;
      bool selH = !cfg.blackIsBot, selB = cfg.blackIsBot;
      drawBevelButton3D(m_window, humanR, selH ? colButtonActive : colButton, hovH, selH);
      centerText(m_blackPlayerText, humanR);
      m_window.draw(m_blackPlayerText);
      if (selH) drawAccentInset(m_window, humanR, colAccent);
      drawBevelButton3D(m_window, botR, selB ? colButtonActive : colButton, hovB, selB);
      centerText(m_blackBotText, botR);
      m_window.draw(m_blackBotText);
      if (selB) drawAccentInset(m_window, botR, colAccent);
    }

    auto drawBotList = [&](const std::vector<BotOption>& list, std::size_t selIdx, float anim) {
      for (std::size_t i = 0; i < list.size(); ++i) {
        const auto& opt = list[i];
        auto r = opt.box.getGlobalBounds();
        bool hov = contains(r, m_mousePos);
        bool sel = (i == selIdx);
        sf::Color base = sel ? colButtonActive : colButton;
        base.a = static_cast<sf::Uint8>(base.a * anim);
        drawBevelButton3D(m_window, r, base, hov, sel);
        sf::Text label = opt.label;
        sf::Color lc = label.getFillColor();
        lc.a = static_cast<sf::Uint8>(lc.a * anim);
        label.setFillColor(lc);
        leftCenterText(label, r, 10.f);
        m_window.draw(label);
        if (sel) {
          sf::Color ac = colAccent;
          ac.a = static_cast<sf::Uint8>(ac.a * anim);
          drawAccentInset(m_window, r, ac);
        }
      }
    };
    if (m_whiteBotListAnim > 0.f)
      drawBotList(m_whiteBotOptions, m_whiteBotSelection, m_whiteBotListAnim);
    if (m_blackBotListAnim > 0.f)
      drawBotList(m_blackBotOptions, m_blackBotSelection, m_blackBotListAnim);

    {
      auto gb = m_timeToggleBtn.getGlobalBounds();
      bool hov = contains(gb, m_mousePos);
      bool on = m_timeEnabled;
      sf::Color base = on ? colAccent : colTimeOff;
      drawBevelButton3D(m_window, gb, base, hov, on);
      centerText(m_timeToggleText, gb);
      m_window.draw(m_timeToggleText);
    }

    if (m_timeEnabled) {
      auto gb = m_timePanel.getGlobalBounds();
      m_window.draw(m_timePanel);
      sf::RectangleShape top({gb.width, 1.f});
      top.setPosition(gb.left, gb.top);
      top.setFillColor(ColorPaletteManager::get().palette().COL_TOP_HILIGHT);
      m_window.draw(top);
      sf::RectangleShape bot({gb.width, 1.f});
      bot.setPosition(gb.left, gb.top + gb.height - 1.f);
      bot.setFillColor(ColorPaletteManager::get().palette().COL_BOTTOM_SHADOW);
      m_window.draw(bot);

      m_window.draw(m_timeTitle);

      auto step = [&](sf::RectangleShape& box, sf::Text& txt, bool hold) {
        auto r = box.getGlobalBounds();
        bool hov = contains(r, m_mousePos);
        bool pressed = hold && hov;
        drawBevelButton3D(m_window, r, colButton, hov, pressed);
        centerText(txt, r);
        m_window.draw(txt);
      };
      step(m_timeMinusBtn, m_minusTxt, m_holdBaseMinus.active);
      step(m_timePlusBtn, m_plusTxt, m_holdBasePlus.active);
      m_window.draw(m_timeMain);

      m_window.draw(m_incLabel);
      step(m_incMinusBtn, m_incMinusTxt, m_holdIncMinus.active);
      step(m_incPlusBtn, m_incPlusTxt, m_holdIncPlus.active);
      m_window.draw(m_incValue);

      for (std::size_t i = 0; i < m_presets.size(); ++i) {
        auto& c = m_presets[i];
        auto r = c.box.getGlobalBounds();
        bool hov = contains(r, m_mousePos);
        bool sel = (m_presetSelection == (int)i);
        drawBevelButton3D(m_window, r, sel ? colButtonActive : colButton, hov, sel);
        centerText(c.label, r, -1.f);
        m_window.draw(c.label);
        if (sel) drawAccentInset(m_window, r, colAccent);
      }
    }

    refreshPgn();
    std::string trimmedFen = trimCopy(m_fenString);
    bool fenEmpty = trimmedFen.empty();
    bool fenValid = trimmedFen.empty() || isValidFen(trimmedFen);
    bool pgnEmpty = m_pgnString.empty();
    bool pgnValid = !pgnEmpty && parsedPgn.has_value();

    m_window.draw(m_loadHeader);
    m_window.draw(m_fenLabel);
    m_window.draw(m_pgnLabel);

    if (!fenEmpty && !fenValid) {
      m_fenErrorText.setString("INVALID FEN - DEFAULT WILL BE USED");
      m_window.draw(m_fenErrorText);
    } else {
      m_fenErrorText.setString("STANDARD FEN");
    }

    m_fenInputBox.setOutlineColor(fenEmpty ? colInputBorder : (fenValid ? colValid : colInvalid));
    m_window.draw(m_fenInputBox);

    if (m_fenString.empty()) {
      sf::Text placeholder("STANDARD FEN", m_font, 15);
      placeholder.setFillColor(colSubtle);
      leftCenterText(placeholder, m_fenInputBox.getGlobalBounds(), fenPadX);
      m_window.draw(placeholder);
    } else {
      m_fenInputText.setString(m_fenString);
      leftCenterText(m_fenInputText, m_fenInputBox.getGlobalBounds(), fenPadX);
      m_window.draw(m_fenInputText);
    }

    if (fenInputActive) {
      float t = std::fmod(caretClock.getElapsedTime().asSeconds(), 1.0f);
      if (t < 0.5f) {
        sf::Text probe(m_fenString, m_font, 15);
        auto b = probe.getLocalBounds();
        float left = m_fenInputBox.getPosition().x + fenPadX;
        float caretX = left + b.width + 1.f;
        float maxX = m_fenInputBox.getPosition().x + m_fenInputBox.getSize().x - 2.f;
        caretX = std::min(caretX, maxX - 1.f);
        float caretHeight = b.height;
        if (caretHeight <= 0.f) caretHeight = m_font.getLineSpacing(15) * 0.8f;
        float caretY = m_fenInputBox.getPosition().y + (m_fenInputBox.getSize().y - caretHeight) * 0.5f - b.top;
        sf::RectangleShape caret({2.f, caretHeight});
        caret.setPosition(snapf(caretX), snapf(caretY));
        caret.setFillColor(colText);
        m_window.draw(caret);
      }
    }

    m_pgnInputBox.setOutlineColor(pgnEmpty ? colInputBorder : (pgnValid ? colValid : colInvalid));
    m_window.draw(m_pgnInputBox);

    if (pgnEmpty) {
      sf::Text placeholder("Paste PGN (SAN moves)", m_font, 15);
      placeholder.setFillColor(colSubtle);
      auto lb = placeholder.getLocalBounds();
      placeholder.setPosition(snapf(m_pgnInputBox.getPosition().x + pgnPadX - lb.left),
                              snapf(m_pgnInputBox.getPosition().y + pgnPadY - lb.top));
      m_window.draw(placeholder);
    }

    auto lines = wrapPgnText(m_pgnString);
    sf::Text lineText("", m_font, 15);
    lineText.setFillColor(colText);
    float lineSpacing = m_font.getLineSpacing(15);
    float baseX = m_pgnInputBox.getPosition().x + pgnPadX;
    float baseY = m_pgnInputBox.getPosition().y + pgnPadY;
    for (std::size_t i = 0; i < lines.size(); ++i) {
      const std::string& line = lines[i];
      if (line.empty() && pgnEmpty) continue;
      lineText.setString(line);
      auto lb = lineText.getLocalBounds();
      lineText.setPosition(snapf(baseX - lb.left), snapf(baseY + i * lineSpacing - lb.top));
      m_window.draw(lineText);
    }

    if (pgnInputActive) {
      float t = std::fmod(caretClock.getElapsedTime().asSeconds(), 1.0f);
      if (t < 0.5f) {
        std::string lastLine = lines.empty() ? std::string{} : lines.back();
        sf::Text probe(lastLine, m_font, 15);
        auto lb = probe.getLocalBounds();
        float caretHeight = lb.height;
        if (caretHeight <= 0.f) caretHeight = m_font.getLineSpacing(15) * 0.8f;
        float caretX = baseX + lb.width + 1.f;
        float maxX = m_pgnInputBox.getPosition().x + m_pgnInputBox.getSize().x - pgnPadX - 1.f;
        caretX = std::min(caretX, maxX);
        float caretY = baseY + (lines.size() - 1) * lineSpacing - lb.top;
        sf::RectangleShape caret({2.f, caretHeight});
        caret.setPosition(snapf(caretX), snapf(caretY));
        caret.setFillColor(colText);
        m_window.draw(caret);
      }
    }

    if (pgnEmpty) {
      m_pgnStatusText.setString("Paste PGN (SAN) to load a game.");
      m_pgnStatusText.setFillColor(colSubtle);
    } else if (pgnValid) {
      m_pgnStatusText.setString("PGN is valid. Final position will be used.");
      m_pgnStatusText.setFillColor(colValid);
    } else {
      std::string msg = pgnError.empty() ? "PGN is invalid." : pgnError;
      m_pgnStatusText.setString(msg);
      m_pgnStatusText.setFillColor(colInvalid);
    }
    m_window.draw(m_pgnStatusText);

    auto startBounds = m_startBtn.getGlobalBounds();
    bool hovStart = contains(startBounds, m_mousePos);
    drawBevelButton3D(m_window, startBounds, colAccent, hovStart, false);
    centerText(m_startText, startBounds);
    m_window.draw(m_startText);

    if (toastVisible) {
      float elapsed = toastClock.getElapsedTime().asSeconds();
      if (elapsed < 2.2f) {
        sf::Text ttxt(toastMsg, m_font, 14);
        ttxt.setFillColor(colText);
        auto tb = ttxt.getLocalBounds();
        float pad = 12.f;
        float bw = tb.width + pad * 2.f;
        float bh = tb.height + pad * 2.f;
        sf::Vector2u ws = m_window.getSize();
        float x = (ws.x - bw) * 0.5f;
        float y = ws.y - bh - 24.f;
        sf::RectangleShape bg({bw, bh});
        bg.setPosition(snapf(x), snapf(y));
        bg.setFillColor(ColorPaletteManager::get().palette().COL_PANEL_ALPHA220);
        bg.setOutlineThickness(1.f);
        bg.setOutlineColor(colPanelBorder);
        m_window.draw(bg);
        ttxt.setPosition(snapf(x + pad - tb.left), snapf(y + pad - tb.top));
        m_window.draw(ttxt);
      } else {
        toastVisible = false;
      }
    }

    {
      sf::Text credit("@ 2025 Julian Meyer", m_font, 13);
      credit.setFillColor(colSubtle);
      auto cb = credit.getLocalBounds();
      sf::Vector2u ws = m_window.getSize();
      credit.setPosition(snapf((float)ws.x - cb.width - 18.f),
                         snapf((float)ws.y - cb.height - 22.f));
      m_window.draw(credit);
    }

    if (m_showLoadWarning) {
      m_window.draw(m_warningBackdrop);
      m_window.draw(m_warningPanel);
      m_window.draw(m_warningTitle);
      m_window.draw(m_warningMessage);

      auto cancelBounds = m_warningCancelBtn.getGlobalBounds();
      bool cancelHover = contains(cancelBounds, m_mousePos);
      drawBevelButton3D(m_window, cancelBounds, colButton, cancelHover, false);
      centerText(m_warningCancelText, cancelBounds);
      m_window.draw(m_warningCancelText);

      auto proceedBounds = m_warningProceedBtn.getGlobalBounds();
      bool proceedHover = contains(proceedBounds, m_mousePos);
      drawBevelButton3D(m_window, proceedBounds, colAccent, proceedHover, false);
      centerText(m_warningProceedText, proceedBounds);
      m_window.draw(m_warningProceedText);
    }
  };

  sf::Clock frameClock;
  while (m_window.isOpen()) {
    float dt = frameClock.restart().asSeconds();
    sf::Event e{};
    while (m_window.pollEvent(e)) {
      if (e.type == sf::Event::Closed) {
        m_window.close();
        break;
      }
      if (e.type == sf::Event::Resized) {
        setupUI();
      }

      if (m_showLoadWarning) {
        if (e.type == sf::Event::MouseMoved) {
          m_mousePos = {(float)e.mouseMove.x, (float)e.mouseMove.y};
        } else if (e.type == sf::Event::MouseButtonPressed &&
                   e.mouseButton.button == sf::Mouse::Left) {
          sf::Vector2f mp((float)e.mouseButton.x, (float)e.mouseButton.y);
          if (contains(m_warningCancelBtn.getGlobalBounds(), mp)) {
            m_showLoadWarning = false;
            m_warnFenInvalid = m_warnPgnInvalid = false;
          } else if (contains(m_warningProceedBtn.getGlobalBounds(), mp)) {
            StartConfig out = computeCurrentConfig(true);
            m_showLoadWarning = false;
            m_warnFenInvalid = m_warnPgnInvalid = false;
            pgnDirty = true;
            return out;
          }
        } else if (e.type == sf::Event::KeyPressed) {
          if (e.key.code == sf::Keyboard::Escape) {
            m_showLoadWarning = false;
            m_warnFenInvalid = m_warnPgnInvalid = false;
          } else if (e.key.code == sf::Keyboard::Enter) {
            StartConfig out = computeCurrentConfig(true);
            m_showLoadWarning = false;
            m_warnFenInvalid = m_warnPgnInvalid = false;
            pgnDirty = true;
            return out;
          }
        }
        continue;
      }

      if (e.type == sf::Event::MouseMoved) {
        m_mousePos = {(float)e.mouseMove.x, (float)e.mouseMove.y};
        auto updateHover = [&](bool& show, bool& forceHide, const sf::FloatRect& btn,
                               const auto& options) {
          bool overBtn = contains(btn, m_mousePos);
          bool overList = false;
          for (const auto& opt : options) {
            if (contains(opt.box.getGlobalBounds(), m_mousePos)) {
              overList = true;
              break;
            }
          }
          if (!overList) forceHide = false;
          show = !forceHide && (overBtn || overList);
        };
        updateHover(m_showPaletteList, m_paletteListForceHide, m_paletteButton.getGlobalBounds(),
                    m_paletteOptions);
        updateHover(m_showWhiteBotList, m_whiteListForceHide, m_whiteBotBtn.getGlobalBounds(),
                    m_whiteBotOptions);
        updateHover(m_showBlackBotList, m_blackListForceHide, m_blackBotBtn.getGlobalBounds(),
                    m_blackBotOptions);
      }

      if (e.type == sf::Event::KeyPressed) {
        if (m_timeEnabled && e.key.code == sf::Keyboard::Left) {
          m_baseSeconds = clampBaseSeconds(m_baseSeconds - (e.key.shift ? 300 : 60));
          m_timeMain.setString(formatHMS(m_baseSeconds));
        } else if (m_timeEnabled && e.key.code == sf::Keyboard::Right) {
          m_baseSeconds = clampBaseSeconds(m_baseSeconds + (e.key.shift ? 300 : 60));
          m_timeMain.setString(formatHMS(m_baseSeconds));
        } else if (m_timeEnabled && e.key.code == sf::Keyboard::Down) {
          m_incrementSeconds = clampIncSeconds(m_incrementSeconds - 1);
          m_incValue.setString("+" + std::to_string(m_incrementSeconds) + "s");
        } else if (m_timeEnabled && e.key.code == sf::Keyboard::Up) {
          m_incrementSeconds = clampIncSeconds(m_incrementSeconds + 1);
          m_incValue.setString("+" + std::to_string(m_incrementSeconds) + "s");
        } else if (e.key.code == sf::Keyboard::Enter && !pgnInputActive) {
          if (auto out = attemptStart()) return *out;
        }
      }

      if (e.type == sf::Event::MouseButtonPressed && e.mouseButton.button == sf::Mouse::Left) {
        sf::Vector2f mp((float)e.mouseButton.x, (float)e.mouseButton.y);

        if (m_timeEnabled && contains(m_timeMinusBtn.getGlobalBounds(), mp)) {
          m_baseSeconds = clampBaseSeconds(m_baseSeconds - 60);
          m_timeMain.setString(formatHMS(m_baseSeconds));
          m_holdBaseMinus.active = true;
          m_holdBaseMinus.clock.restart();
          m_holdBaseMinus.fired = 0;
        } else if (m_timeEnabled && contains(m_timePlusBtn.getGlobalBounds(), mp)) {
          m_baseSeconds = clampBaseSeconds(m_baseSeconds + 60);
          m_timeMain.setString(formatHMS(m_baseSeconds));
          m_holdBasePlus.active = true;
          m_holdBasePlus.clock.restart();
          m_holdBasePlus.fired = 0;
        } else if (m_timeEnabled && contains(m_incMinusBtn.getGlobalBounds(), mp)) {
          m_incrementSeconds = clampIncSeconds(m_incrementSeconds - 1);
          m_incValue.setString("+" + std::to_string(m_incrementSeconds) + "s");
          m_holdIncMinus.active = true;
          m_holdIncMinus.clock.restart();
          m_holdIncMinus.fired = 0;
        } else if (m_timeEnabled && contains(m_incPlusBtn.getGlobalBounds(), mp)) {
          m_incrementSeconds = clampIncSeconds(m_incrementSeconds + 1);
          m_incValue.setString("+" + std::to_string(m_incrementSeconds) + "s");
          m_holdIncPlus.active = true;
          m_holdIncPlus.clock.restart();
          m_holdIncPlus.fired = 0;
        } else if (contains(m_timeToggleBtn.getGlobalBounds(), mp)) {
          m_timeEnabled = !m_timeEnabled;
          updateTimeToggle();
        } else if (contains(m_fenInputBox.getGlobalBounds(), mp)) {
          fenInputActive = true;
          pgnInputActive = false;
          caretClock.restart();
        } else if (contains(m_pgnInputBox.getGlobalBounds(), mp)) {
          pgnInputActive = true;
          fenInputActive = false;
          caretClock.restart();
        } else {
          if (fenInputActive) {
            fenInputActive = false;
            if (fenUserEdited && !m_fenString.empty() && !isValidFen(m_fenString)) {
              toastMsg = "Incorrect FEN. Default will be used.";
              toastVisible = true;
              toastClock.restart();
            }
          }
          if (pgnInputActive) pgnInputActive = false;

          if (handleMouse(mp, cfg)) {
            if (auto out = attemptStart()) return *out;
          }
        }
      }

      if (e.type == sf::Event::MouseButtonReleased && e.mouseButton.button == sf::Mouse::Left) {
        m_holdBaseMinus.active = m_holdBasePlus.active = m_holdIncMinus.active =
            m_holdIncPlus.active = false;
      }

      if (fenInputActive && e.type == sf::Event::KeyPressed && (e.key.control || e.key.system) &&
          e.key.code == sf::Keyboard::V) {
        auto clip = sf::Clipboard::getString().toAnsiString();
        clip.erase(std::remove(clip.begin(), clip.end(), '\n'), clip.end());
        clip.erase(std::remove(clip.begin(), clip.end(), '\r'), clip.end());
        const float avail = m_fenInputBox.getSize().x - 2.f * fenPadX - 2.f;
        std::string out = m_fenString;
        for (char c : clip) {
          sf::Text probe(out + c, m_font, 15);
          if (probe.getLocalBounds().width <= avail)
            out.push_back(c);
          else
            break;
        }
        if (out != m_fenString) {
          m_fenString = out;
          fenUserEdited = true;
        }
      }

      if (fenInputActive && e.type == sf::Event::TextEntered) {
        if (e.text.unicode == 8) {
          if (!m_fenString.empty()) {
            m_fenString.pop_back();
            fenUserEdited = true;
          }
        } else if (e.text.unicode >= 32 && e.text.unicode < 127) {
          const float avail = m_fenInputBox.getSize().x - 2.f * fenPadX - 2.f;
          std::string tmp = m_fenString;
          tmp.push_back((char)e.text.unicode);
          sf::Text probe(tmp, m_font, 15);
          if (probe.getLocalBounds().width <= avail) {
            m_fenString.push_back((char)e.text.unicode);
            fenUserEdited = true;
          }
        }
      }

      if (pgnInputActive && e.type == sf::Event::KeyPressed && (e.key.control || e.key.system) &&
          e.key.code == sf::Keyboard::V) {
        auto clip = sf::Clipboard::getString().toAnsiString();
        for (char c : clip) {
          if (c == '\r') continue;
          if (c >= 32 || c == '\n') m_pgnString.push_back(c);
        }
        pgnDirty = true;
      }

      if (pgnInputActive && e.type == sf::Event::TextEntered) {
        if (e.text.unicode == 8) {
          if (!m_pgnString.empty()) {
            m_pgnString.pop_back();
            pgnDirty = true;
          }
        } else if (e.text.unicode == 13) {
          // ignore carriage return
        } else if (e.text.unicode == 10) {
          m_pgnString.push_back('\n');
          pgnDirty = true;
        } else if (e.text.unicode >= 32 && e.text.unicode < 127) {
          m_pgnString.push_back((char)e.text.unicode);
          pgnDirty = true;
        }
      }
    }

    if (m_timeEnabled) {
      processHoldRepeater(m_holdBaseMinus, m_timeMinusBtn.getGlobalBounds(), m_mousePos, [&] {
        m_baseSeconds = clampBaseSeconds(m_baseSeconds - 60);
        m_timeMain.setString(formatHMS(m_baseSeconds));
      });
      processHoldRepeater(m_holdBasePlus, m_timePlusBtn.getGlobalBounds(), m_mousePos, [&] {
        m_baseSeconds = clampBaseSeconds(m_baseSeconds + 60);
        m_timeMain.setString(formatHMS(m_baseSeconds));
      });
      processHoldRepeater(m_holdIncMinus, m_incMinusBtn.getGlobalBounds(), m_mousePos, [&] {
        m_incrementSeconds = clampIncSeconds(m_incrementSeconds - 1);
        m_incValue.setString("+" + std::to_string(m_incrementSeconds) + "s");
      });
      processHoldRepeater(m_holdIncPlus, m_incPlusBtn.getGlobalBounds(), m_mousePos, [&] {
        m_incrementSeconds = clampIncSeconds(m_incrementSeconds + 1);
        m_incValue.setString("+" + std::to_string(m_incrementSeconds) + "s");
      });
    }

    auto animateList = [&](bool show, float& anim) {
      const float speed = 10.f;
      if (show)
        anim = std::min(1.f, anim + speed * dt);
      else
        anim = std::max(0.f, anim - speed * dt);
    };
    animateList(m_showPaletteList, m_paletteListAnim);
    animateList(m_showWhiteBotList, m_whiteBotListAnim);
    animateList(m_showBlackBotList, m_blackBotListAnim);

    m_window.clear();
    drawUI();
    m_window.display();
  }

  return computeCurrentConfig(false);
}

}  // namespace lilia::view
