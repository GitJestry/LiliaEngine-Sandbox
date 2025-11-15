#pragma once

#include <SFML/Graphics.hpp>
#include <functional>
#include <string>
#include <vector>

#include "lilia/bot/bot_info.hpp"
#include "lilia/constants.hpp"
#include "lilia/view/color_palette_manager.hpp"

namespace lilia::view {

struct StartConfig {
  bool whiteIsBot{false};
  BotType whiteBot{BotType::Lilia};
  bool blackIsBot{true};
  BotType blackBot{BotType::Lilia};
  std::string fen{core::START_FEN};
  int timeBaseSeconds{300};     // default 5 minutes
  int timeIncrementSeconds{0};  // default 0s increment
  bool timeEnabled{true};       // whether clocks are used
};

struct BotOption {
  BotType type;
  sf::RectangleShape box;
  sf::Text label;
};

struct PaletteOption {
  std::string name;
  sf::RectangleShape box;
  sf::Text label;
};

class StartScreen {
 public:
  explicit StartScreen(sf::RenderWindow &window);
  ~StartScreen();
  StartConfig run();

 private:
  sf::RenderWindow &m_window;
  sf::Font m_font;
  sf::Texture m_logoTex;
  sf::Sprite m_logo;
  sf::Text m_devByText;    // "@Developed by Julian Meyer" bottom-right
  sf::Text m_fenInfoText;  // subtle hint below FEN box

  sf::RectangleShape m_whiteSectionBg;
  sf::RectangleShape m_blackSectionBg;
  sf::RectangleShape m_setupSectionBg;
  sf::Text m_setupTitle;
  sf::Text m_setupDescription;
  sf::Text m_fenLabel;

  sf::RectangleShape m_whitePlayerBtn;
  sf::RectangleShape m_whiteBotBtn;
  sf::Text m_whitePlayerText;
  sf::Text m_whiteBotText;
  sf::Text m_whiteLabel;
  std::vector<BotOption> m_whiteBotOptions;
  std::size_t m_whiteBotSelection{0};
  bool m_showWhiteBotList{false};
  bool m_whiteListForceHide{false};
  float m_whiteBotListAnim{0.f};

  sf::RectangleShape m_blackPlayerBtn;
  sf::RectangleShape m_blackBotBtn;
  sf::Text m_blackPlayerText;
  sf::Text m_blackBotText;
  sf::Text m_blackLabel;
  std::vector<BotOption> m_blackBotOptions;
  std::size_t m_blackBotSelection{0};
  bool m_showBlackBotList{false};
  bool m_blackListForceHide{false};
  float m_blackBotListAnim{0.f};

  sf::RectangleShape m_startBtn;
  sf::Text m_startText;
  sf::Text m_creditText;

  // Palette selection UI
  sf::RectangleShape m_paletteButton;
  sf::Text m_paletteText;
  std::vector<PaletteOption> m_paletteOptions;
  std::size_t m_paletteSelection{0};
  bool m_showPaletteList{false};
  bool m_paletteListForceHide{false};
  float m_paletteListAnim{0.f};

  // FEN popup UI
  bool m_showFenPopup{false};
  sf::RectangleShape m_fenPopup;
  sf::RectangleShape m_fenInputBox;
  sf::Text m_fenInputText;
  sf::RectangleShape m_fenBackBtn;
  sf::RectangleShape m_fenContinueBtn;
  sf::Text m_fenBackText;
  sf::Text m_fenContinueText;
  sf::Text m_fenErrorText;
  std::string m_fenString;
  sf::Clock m_errorClock;
  bool m_showError{false};

  // time control state
  int m_baseSeconds{300};
  int m_incrementSeconds{0};
  bool m_timeEnabled{true};

  // time control UI
  sf::RectangleShape m_timeToggleBtn;
  sf::Text m_timeToggleText;
  sf::RectangleShape m_timePanel;
  sf::Text m_timeTitle;
  sf::Text m_timeMain;  // "HH:MM:SS"
  sf::Text m_incLabel;  // "Increment"
  sf::Text m_incValue;  // "+00s … +30s"
  sf::RectangleShape m_timeMinusBtn, m_timePlusBtn;
  sf::RectangleShape m_incMinusBtn, m_incPlusBtn;
  sf::Text m_minusTxt, m_plusTxt, m_incMinusTxt, m_incPlusTxt;

  struct PresetChip {
    sf::RectangleShape box;
    sf::Text label;
    int base;
    int inc;
  };
  std::vector<PresetChip> m_presets;
  int m_presetSelection{-1};

  struct HoldRepeater {
    bool active{false};
    sf::Clock clock;
    int fired{0};
  };
  HoldRepeater m_holdBaseMinus, m_holdBasePlus, m_holdIncMinus, m_holdIncPlus;
  // mouse (for hover + in-bounds while holding)
  sf::Vector2f m_mousePos{0.f, 0.f};

  ColorPaletteManager::ListenerID m_listener_id{0};

  void setupUI();
  void applyTheme();
  bool handleMouse(sf::Vector2f pos, StartConfig &cfg);
  bool handleFenMouse(sf::Vector2f pos, StartConfig &cfg);
  bool isValidFen(const std::string &fen);
  void updateTimeToggle();
  void processHoldRepeater(HoldRepeater &r, const sf::FloatRect &bounds, sf::Vector2f mouse,
                           std::function<void()> stepFn, float initialDelay = 0.35f,
                           float repeatRate = 0.06f);
};

}  // namespace lilia::view
