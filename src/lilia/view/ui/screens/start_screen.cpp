#include "lilia/view/ui/screens/start_screen.hpp"

#include <SFML/Graphics.hpp>

#include <memory>
#include <optional>
#include <string>

#include "lilia/view/ui/style/color_palette_manager.hpp"
#include "lilia/view/ui/interaction/focus.hpp"
#include "lilia/view/ui/render/layout.hpp"
#include "lilia/view/ui/style/modals/modal_stack.hpp"
#include "lilia/view/ui/style/style.hpp"
#include "lilia/view/ui/widgets/button.hpp"
#include "lilia/view/ui/widgets/time_control_picker.hpp"
#include "lilia/view/ui/render/render_constants.hpp"

#include "lilia/view/ui/style/modals/game_setup/game_setup_modal.hpp"
#include "lilia/view/ui/style/modals/palette_picker_modal.hpp"

namespace lilia::view
{

  StartScreen::StartScreen(sf::RenderWindow &window) : m_window(window)
  {
    m_font.loadFromFile(std::string(constant::path::FONT));
    m_logoTex.loadFromFile(std::string(constant::path::ICON_LILIA_START));
    m_logo.setTexture(m_logoTex);
  }

  StartConfig StartScreen::run()
  {
    ui::ModalStack modals;
    ui::FocusManager focus;

    // Ensure crisp 1:1 rendering (important for text).
    m_window.setView(m_window.getDefaultView());

    StartConfig cfg;
    cfg.fen = core::START_FEN;

    cfg.whiteEngine.external = false;
    cfg.whiteEngine.builtin = BotType::Lilia;
    cfg.whiteEngine.displayName = "Lilia";
    cfg.whiteEngine.version = "1.0";
    cfg.blackEngine = cfg.whiteEngine;

    const ui::Theme &theme = m_theme.uiTheme();

    // --- Time Control Picker (top-center; config only visible when ON) ---
    ui::TimeControlPicker timePicker(m_font, theme);
    timePicker.setValue({cfg.timeEnabled, cfg.timeBaseSeconds, cfg.timeIncrementSeconds});

    // Buttons
    ui::Button paletteBtn;
    paletteBtn.setTheme(&theme);
    paletteBtn.setFont(m_font);
    paletteBtn.setText("Color Theme", 16);

    ui::Button whiteHuman, whiteBot;
    whiteHuman.setTheme(&theme);
    whiteHuman.setFont(m_font);
    whiteHuman.setText("Human", 18);

    whiteBot.setTheme(&theme);
    whiteBot.setFont(m_font);
    whiteBot.setText("Bot", 18);

    ui::Button whiteEngineBtn;
    whiteEngineBtn.setTheme(&theme);
    whiteEngineBtn.setFont(m_font);
    whiteEngineBtn.setText("Select Bot...", 16);

    ui::Button blackHuman, blackBot;
    blackHuman.setTheme(&theme);
    blackHuman.setFont(m_font);
    blackHuman.setText("Human", 18);

    blackBot.setTheme(&theme);
    blackBot.setFont(m_font);
    blackBot.setText("Bot", 18);

    ui::Button blackEngineBtn;
    blackEngineBtn.setTheme(&theme);
    blackEngineBtn.setFont(m_font);
    blackEngineBtn.setText("Select Bot...", 16);

    ui::Button startBtn;
    startBtn.setTheme(&theme);
    startBtn.setFont(m_font);
    startBtn.setText("Start Game", 22);
    startBtn.setAccent(true);

    ui::Button loadBtn;
    loadBtn.setTheme(&theme);
    loadBtn.setFont(m_font);
    loadBtn.setText("Load Game or create/input Startpos", 16);

    // Track modals
    ui::PalettePickerModal *paletteModal = nullptr;
    BotCatalogModal *whiteCatalog = nullptr;
    BotCatalogModal *blackCatalog = nullptr;
    ui::GameSetupModal *setupModal = nullptr;

    std::optional<std::string> palettePicked{};

    auto openWhiteCatalog = [&]
    {
      auto m = std::make_unique<BotCatalogModal>(m_font, theme, cfg.whiteEngine);
      whiteCatalog = m.get();
      modals.push(std::move(m));
      modals.layout(m_window.getSize());
    };

    auto openBlackCatalog = [&]
    {
      auto m = std::make_unique<BotCatalogModal>(m_font, theme, cfg.blackEngine);
      blackCatalog = m.get();
      modals.push(std::move(m));
      modals.layout(m_window.getSize());
    };

    paletteBtn.setOnClick([&]
                          {
    auto m = std::make_unique<ui::PalettePickerModal>();
    paletteModal = m.get();

    ui::PalettePickerModal::Params p;
    p.theme = &theme;
    p.font = &m_font;
    p.anchorButton = paletteBtn.bounds();
    p.onPick = [&](const std::string &name)
    { palettePicked = name; };
    p.onClose = [&]() {};

    m->open(m_window.getSize(), std::move(p));
    modals.push(std::move(m));
    modals.layout(m_window.getSize()); });

    // Side selection: Human top, Bot bottom.
    // Requirement: Bot-click opens selection popup.
    whiteHuman.setOnClick([&]
                          { cfg.whiteIsBot = false; });

    whiteBot.setOnClick([&]
                        {
    cfg.whiteIsBot = true;
    openWhiteCatalog(); });

    blackHuman.setOnClick([&]
                          { cfg.blackIsBot = false; });

    blackBot.setOnClick([&]
                        {
    cfg.blackIsBot = true;
    openBlackCatalog(); });

    // Engine buttons remain as "change engine" affordance when Bot is active.
    whiteEngineBtn.setOnClick([&]
                              { openWhiteCatalog(); });
    blackEngineBtn.setOnClick([&]
                              { openBlackCatalog(); });

    loadBtn.setOnClick([&]
                       {
    auto m = std::make_unique<ui::GameSetupModal>(m_font, theme, focus);
    setupModal = m.get();
    modals.push(std::move(m));
    modals.layout(m_window.getSize()); });

    bool done = false;
    sf::Clock frame;
    sf::Vector2f mouse{0.f, 0.f};

    // Geometry cache for clean drawing & consistent layout
    sf::FloatRect panel{};
    sf::FloatRect whiteCol{}, timeCol{}, blackCol{};
    sf::FloatRect whiteCard{}, blackCard{}, timeCard{};
    sf::Vector2f titlePos{}, subtitlePos{};
    sf::Vector2f whiteLabelPos{}, blackLabelPos{};

    auto layoutAll = [&]
    {
      auto ws = m_window.getSize();

      // Keep UI within window (prevents cramped layouts on smaller windows)
      const float panelW = std::min(980.f, std::max(760.f, float(ws.x) - 90.f));
      const float panelH = std::min(620.f, std::max(520.f, float(ws.y) - 140.f));

      panel = ui::anchoredCenter(ws, {panelW, panelH});
      sf::FloatRect inner = ui::inset(panel, 24.f);

      // Bottom-left palette button (outside main panel, like before)
      paletteBtn.setBounds({20.f, float(ws.y) - 54.f, 140.f, 34.f});

      // Bottom actions: Start centered, Load directly below (requirement)
      const float bottomPad = 26.f;
      const float startW = 300.f;
      const float startH = 56.f;
      const float loadH = 42.f;
      const float gap = 12.f;

      const float loadW = std::min(640.f, panel.width - 90.f);
      const float loadX = panel.left + (panel.width - loadW) * 0.5f;
      const float loadY = panel.top + panel.height - bottomPad - loadH;

      const float startX = panel.left + (panel.width - startW) * 0.5f;
      const float startY = loadY - gap - startH;

      startBtn.setBounds({startX, startY, startW, startH});
      loadBtn.setBounds({loadX, loadY, loadW, loadH});

      // Header positions
      titlePos = ui::snap({panel.left + 24.f, panel.top + 18.f});
      subtitlePos = ui::snap({panel.left + 24.f, panel.top + 52.f});

      // Content area between header and actions
      const float contentTop = panel.top + 92.f;
      const float contentBottom = startY - 18.f;
      const float contentH = std::max(0.f, contentBottom - contentTop);

      // Column sizing: favor a wide center for time configuration.
      const float colGap = 22.f;
      const float desiredTimeW = 520.f;
      const float minSideW = 190.f;

      float availableW = inner.width;
      float sideW = (availableW - desiredTimeW - 2.f * colGap) * 0.5f;

      if (sideW < minSideW)
      {
        sideW = minSideW;
      }
      float timeW = availableW - 2.f * sideW - 2.f * colGap;
      if (timeW < 360.f) // keep time panel usable
      {
        // If too tight: take some from sides but keep minimum
        float deficit = 360.f - timeW;
        float take = deficit * 0.5f;
        sideW = std::max(minSideW, sideW - take);
        timeW = availableW - 2.f * sideW - 2.f * colGap;
      }

      whiteCol = {inner.left, contentTop, sideW, contentH};
      timeCol = {whiteCol.left + whiteCol.width + colGap, contentTop, timeW, contentH};
      blackCol = {timeCol.left + timeCol.width + colGap, contentTop, sideW, contentH};

      // Labels
      whiteLabelPos = ui::snap({whiteCol.left, whiteCol.top});
      blackLabelPos = ui::snap({blackCol.left, blackCol.top});

      // Cards (sub-panels for cleaner modern grouping)
      const float cardTopPad = 34.f;
      whiteCard = {whiteCol.left, whiteCol.top + cardTopPad, whiteCol.width, whiteCol.height - cardTopPad};
      blackCard = {blackCol.left, blackCol.top + cardTopPad, blackCol.width, blackCol.height - cardTopPad};
      timeCard = {timeCol.left, timeCol.top, timeCol.width, timeCol.height};

      // Side buttons layout (Human above Bot, engine appears only if Bot active)
      const float btnH = 44.f;
      const float btnGapY = 10.f;

      const float sidePad = 12.f;
      const float x = whiteCard.left + sidePad;
      const float w = whiteCard.width - 2.f * sidePad;

      float yW = whiteCard.top + 14.f;
      whiteHuman.setBounds({x, yW, w, btnH});
      yW += btnH + btnGapY;
      whiteBot.setBounds({x, yW, w, btnH});
      yW += btnH + btnGapY;

      whiteEngineBtn.setBounds({x, yW, w, 40.f});

      float yB = blackCard.top + 14.f;
      const float xb = blackCard.left + sidePad;
      const float wb = blackCard.width - 2.f * sidePad;

      blackHuman.setBounds({xb, yB, wb, btnH});
      yB += btnH + btnGapY;
      blackBot.setBounds({xb, yB, wb, btnH});
      yB += btnH + btnGapY;

      blackEngineBtn.setBounds({xb, yB, wb, 40.f});

      // Time picker bounds: upper-middle column; configuration expands under toggle when ON.
      // Keep it anchored to the top of the column (modern, predictable).
      sf::FloatRect timeBounds = {timeCard.left, timeCard.top, timeCard.width, 160.f};
      timePicker.setBounds(timeBounds);
    };

    layoutAll();

    while (m_window.isOpen() && !done)
    {
      float dt = frame.restart().asSeconds();

      sf::Event e{};
      while (m_window.pollEvent(e))
      {
        if (e.type == sf::Event::Closed)
          m_window.close();

        if (e.type == sf::Event::Resized)
        {
          // Critical for crisp text: prevent implicit stretching/scaling.
          sf::View v(sf::FloatRect(0.f, 0.f, float(e.size.width), float(e.size.height)));
          m_window.setView(v);

          layoutAll();
          modals.layout(m_window.getSize());
        }

        if (e.type == sf::Event::MouseMoved)
          mouse = {float(e.mouseMove.x), float(e.mouseMove.y)};

        if (modals.handleEvent(e, mouse))
          continue;

        // hover
        paletteBtn.updateHover(mouse);

        whiteHuman.updateHover(mouse);
        whiteBot.updateHover(mouse);
        blackHuman.updateHover(mouse);
        blackBot.updateHover(mouse);

        if (cfg.whiteIsBot)
          whiteEngineBtn.updateHover(mouse);
        if (cfg.blackIsBot)
          blackEngineBtn.updateHover(mouse);

        startBtn.updateHover(mouse);
        loadBtn.updateHover(mouse);

        timePicker.updateHover(mouse);

        // events
        if (paletteBtn.handleEvent(e, mouse))
          continue;

        if (whiteHuman.handleEvent(e, mouse))
          continue;
        if (whiteBot.handleEvent(e, mouse))
          continue;

        if (cfg.whiteIsBot && whiteEngineBtn.handleEvent(e, mouse))
          continue;

        if (blackHuman.handleEvent(e, mouse))
          continue;
        if (blackBot.handleEvent(e, mouse))
          continue;

        if (cfg.blackIsBot && blackEngineBtn.handleEvent(e, mouse))
          continue;

        if (loadBtn.handleEvent(e, mouse))
          continue;

        if (timePicker.handleEvent(e, mouse))
          continue;

        if (startBtn.handleEvent(e, mouse))
          done = true;
      }

      modals.update(dt, mouse, [&](ui::Modal &modal)
                    {
      if (paletteModal && &modal == paletteModal)
      {
        if (palettePicked)
        {
          ColorPaletteManager::get().setPalette(*palettePicked);
          palettePicked.reset();
        }
        paletteModal = nullptr;
      }

      if (whiteCatalog && &modal == whiteCatalog)
      {
        if (auto picked = whiteCatalog->picked())
          cfg.whiteEngine = *picked;
        whiteCatalog = nullptr;
      }

      if (blackCatalog && &modal == blackCatalog)
      {
        if (auto picked = blackCatalog->picked())
          cfg.blackEngine = *picked;
        blackCatalog = nullptr;
      }

      if (setupModal && &modal == setupModal)
      {
        if (auto fen = setupModal->resultFen())
          cfg.fen = *fen;
        setupModal = nullptr;
      } });

      // Pull current time settings from picker
      {
        auto tv = timePicker.value();
        cfg.timeEnabled = tv.enabled;
        cfg.timeBaseSeconds = tv.baseSeconds;
        cfg.timeIncrementSeconds = tv.incrementSeconds;
      }

      // Make sure our UI draws 1:1 (prevents blurry text if any other screen changed the view).
      m_window.setView(m_window.getDefaultView());

      // Draw
      m_window.clear();
      ui::drawVerticalGradient(m_window, m_window.getSize(), theme.bgTop, theme.bgBottom);

      // Logo background
      if (m_logoTex.getSize().x > 0)
      {
        sf::Sprite logoBG(m_logoTex);
        auto ws = m_window.getSize();
        float desiredH = ws.y * 0.90f;
        float s = desiredH / float(m_logoTex.getSize().y);
        logoBG.setScale(s, s);
        auto lb = logoBG.getLocalBounds();
        logoBG.setOrigin(lb.width, 0.f);
        logoBG.setPosition(ui::snap({float(ws.x) - 24.f, 24.f}));
        logoBG.setColor(m_theme.colors().COL_LOGO_BG);
        m_window.draw(logoBG, sf::RenderStates(sf::BlendAlpha));
      }

      ui::drawPanelShadow(m_window, panel);

      sf::RectangleShape body({panel.width, panel.height});
      body.setPosition(ui::snap({panel.left, panel.top}));
      body.setFillColor(theme.panel);
      body.setOutlineThickness(1.f);
      body.setOutlineColor(theme.panelBorder);
      m_window.draw(body);

      sf::Text title("Chess Bot Sandbox", m_font, 28);
      title.setFillColor(theme.text);
      title.setPosition(titlePos);
      m_window.draw(title);

      sf::Text subtitle("Select sides, choose engines, and set a start position.", m_font, 18);
      subtitle.setFillColor(theme.subtle);
      subtitle.setPosition(subtitlePos);
      m_window.draw(subtitle);

      // Section labels
      sf::Text wl("White", m_font, 20);
      wl.setFillColor(theme.text);
      wl.setPosition(whiteLabelPos);
      m_window.draw(wl);

      sf::Text bl("Black", m_font, 20);
      bl.setFillColor(theme.text);
      bl.setPosition(blackLabelPos);
      m_window.draw(bl);

      // Sub-cards for cleaner grouping
      auto drawCard = [&](const sf::FloatRect &r)
      {
        // soft shadow + card fill
        ui::drawSoftShadowRect(m_window, r, sf::Color(0, 0, 0, 70), 2, 2.f);

        sf::RectangleShape card({r.width, r.height});
        card.setPosition(ui::snap({r.left, r.top}));
        card.setFillColor(theme.inputBg);
        card.setOutlineThickness(1.f);
        card.setOutlineColor(theme.inputBorder);
        m_window.draw(card);
      };

      drawCard(whiteCard);
      drawCard(blackCard);

      // Button states
      whiteHuman.setActive(!cfg.whiteIsBot);
      whiteBot.setActive(cfg.whiteIsBot);

      blackHuman.setActive(!cfg.blackIsBot);
      blackBot.setActive(cfg.blackIsBot);

      // Engine buttons: only visible when Bot selected
      whiteEngineBtn.setEnabled(cfg.whiteIsBot);
      blackEngineBtn.setEnabled(cfg.blackIsBot);

      whiteEngineBtn.setText(cfg.whiteEngine.displayName + " v" + cfg.whiteEngine.version, 16);
      blackEngineBtn.setText(cfg.blackEngine.displayName + " v" + cfg.blackEngine.version, 16);

      // Draw widgets
      paletteBtn.draw(m_window);

      whiteHuman.draw(m_window);
      whiteBot.draw(m_window);
      if (cfg.whiteIsBot)
        whiteEngineBtn.draw(m_window);

      blackHuman.draw(m_window);
      blackBot.draw(m_window);
      if (cfg.blackIsBot)
        blackEngineBtn.draw(m_window);

      // Time picker (top-center; config only when ON)
      timePicker.draw(m_window);

      // Actions (bottom-center)
      startBtn.draw(m_window);
      loadBtn.draw(m_window);

      // Footer texts
      {
        sf::Text ver(std::string(constant::SANDBOX_VERSION), m_font, 14);
        ver.setFillColor(theme.subtle);
        auto vb = ver.getLocalBounds();
        auto ws = m_window.getSize();
        ver.setPosition(ui::snap({(ws.x - vb.width) * 0.5f, float(ws.y) - 26.f}));
        m_window.draw(ver);
      }

      {
        sf::Text credit("@ 2025 Julian Meyer", m_font, 13);
        credit.setFillColor(theme.subtle);
        auto cb = credit.getLocalBounds();
        auto ws = m_window.getSize();
        credit.setPosition(ui::snap({float(ws.x) - cb.width - 18.f, float(ws.y) - cb.height - 22.f}));
        m_window.draw(credit);
      }

      modals.drawOverlay(m_window);
      modals.drawPanel(m_window);

      m_window.display();
    }

    // Ensure cfg contains final picker values
    {
      auto tv = timePicker.value();
      cfg.timeEnabled = tv.enabled;
      cfg.timeBaseSeconds = tv.baseSeconds;
      cfg.timeIncrementSeconds = tv.incrementSeconds;
    }

    return cfg;
  }

} // namespace lilia::view
