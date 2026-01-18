#include "lilia/view/ui/screens/start_screen.hpp"

#include <SFML/Graphics.hpp>
#include <SFML/Window/Clipboard.hpp>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "lilia/constants.hpp"
#include "lilia/engine/uci/engine_registry.hpp"
#include "lilia/view/ui/interaction/focus.hpp"
#include "lilia/view/ui/render/layout.hpp"
#include "lilia/view/ui/render/render_constants.hpp"
#include "lilia/view/ui/style/color_palette_manager.hpp"
#include "lilia/view/ui/style/modals/modal_stack.hpp"
#include "lilia/view/ui/style/style.hpp"
#include "lilia/view/ui/widgets/button.hpp"
#include "lilia/view/ui/widgets/time_control_picker.hpp"

#include "lilia/view/ui/style/modals/game_setup/game_setup_modal.hpp"
#include "lilia/view/ui/style/modals/game_setup/game_setup_validation.hpp"
#include "lilia/view/ui/style/modals/palette_picker_modal.hpp"

namespace lilia::view
{
  namespace
  {
    static void stripTrailingNewlines(std::string &s)
    {
      while (!s.empty() && (s.back() == '\n' || s.back() == '\r'))
        s.pop_back();
    }

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <commdlg.h>

    static std::optional<std::string> openPgnFileDialog()
    {
      char fileBuf[MAX_PATH] = {0};

      OPENFILENAMEA ofn;
      ZeroMemory(&ofn, sizeof(ofn));
      ofn.lStructSize = sizeof(ofn);
      ofn.lpstrFile = fileBuf;
      ofn.nMaxFile = MAX_PATH;
      ofn.lpstrFilter = "PGN Files\0*.pgn;*.PGN;*.txt\0All Files\0*.*\0\0";
      ofn.nFilterIndex = 1;
      ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_EXPLORER;

      if (GetOpenFileNameA(&ofn))
        return std::string(fileBuf);

      return std::nullopt;
    }

#else

    static std::optional<std::string> popenReadAll(const std::string &cmd)
    {
      FILE *pipe = popen(cmd.c_str(), "r");
      if (!pipe)
        return std::nullopt;

      std::string out;
      char buf[1024];
      while (fgets(buf, sizeof(buf), pipe))
        out += buf;

      pclose(pipe);
      stripTrailingNewlines(out);
      if (out.empty())
        return std::nullopt;
      return out;
    }

    static std::optional<std::string> openPgnFileDialog()
    {
#if defined(__APPLE__)
      auto path = popenReadAll("osascript -e 'POSIX path of (choose file with prompt \"Select PGN file\")' 2>/dev/null");
      if (path)
        return path;
#else
      auto path = popenReadAll("zenity --file-selection --title=\"Select PGN file\" 2>/dev/null");
      if (path)
        return path;

      path = popenReadAll("kdialog --getopenfilename . \"*.pgn *.PGN *.txt|PGN files\" 2>/dev/null");
      if (path)
        return path;
#endif
      return std::nullopt;
    }

#endif

    static std::string engineLabel(const lilia::config::EngineRef &r)
    {
      const std::string name =
          !r.displayName.empty() ? r.displayName
                                 : (!r.engineId.empty() ? r.engineId : std::string("Select Bot..."));
      return name;
    }
  } // namespace

  StartScreen::StartScreen(sf::RenderWindow &window) : m_window(window)
  {
    m_font.loadFromFile(std::string(constant::path::FONT));
    m_logoTex.loadFromFile(std::string(constant::path::ICON_LILIA_START));
    m_logo.setTexture(m_logoTex);
  }

  lilia::config::StartConfig StartScreen::run()
  {
    ui::ModalStack modals;
    ui::FocusManager focus;

    // Ensure crisp 1:1 rendering (important for text).
    m_window.setView(m_window.getDefaultView());

    // Make sure registry is hydrated (disk + builtins should already be ensured by Engine::init()).
    auto &reg = lilia::engine::uci::EngineRegistry::instance();
    reg.load();

    // Canonical StartConfig (single source of truth).
    lilia::config::StartConfig cfg{};
    cfg.game.startFen = core::START_FEN;
    cfg.game.tc.enabled = false;
    cfg.game.tc.baseSeconds = 300;
    cfg.game.tc.incrementSeconds = 0;

    cfg.replay.enabled = false;
    cfg.replay.pgnText.clear();
    cfg.replay.pgnFilename.clear();
    cfg.replay.pgnPath.clear();

    // Match your previous UX defaults: White human, Black bot (Lilia).
    cfg.white.kind = lilia::config::SideKind::Human;
    cfg.white.bot.reset();

    auto makeLockedInBot = [&]() -> lilia::config::BotConfig
    {
      // 1) Prefer builtin lilia
      auto bc = reg.makeDefaultBotConfig("lilia");
      if (!bc.engine.engineId.empty())
        return bc;

      // 2) Fallback: builtin stockfish
      bc = reg.makeDefaultBotConfig("stockfish");
      if (!bc.engine.engineId.empty())
        return bc;

      // 3) Fallback: first registered engine
      auto list = reg.list();
      if (!list.empty())
      {
        bc = reg.makeDefaultBotConfig(list.front().ref.engineId);
        if (!bc.engine.engineId.empty())
          return bc;
      }

      // 4) No engines at all (return empty)
      return {};
    };

    cfg.black.bot = makeLockedInBot();
    if (cfg.black.bot.has_value() && !cfg.black.bot->engine.engineId.empty())
    {
      cfg.black.kind = lilia::config::SideKind::Engine;
    }
    else
    {
      // Safe degrade: never claim "bot" if we don't have an engine.
      cfg.black.kind = lilia::config::SideKind::Human;
      cfg.black.bot.reset();
    }

    // Invariant helpers (prevents "engine side but nobody plays" + fixes icon fallback issues).
    auto setHumanSide = [&](lilia::config::SideConfig &side)
    {
      side.kind = lilia::config::SideKind::Human;
      side.bot.reset();
    };

    auto ensureBotSide = [&](lilia::config::SideConfig &side)
    {
      side.kind = lilia::config::SideKind::Engine;

      if (!side.bot.has_value() || side.bot->engine.engineId.empty())
        side.bot = makeLockedInBot();

      if (!side.bot.has_value() || side.bot->engine.engineId.empty())
      {
        // Degrade safely: never leave an Engine side without a valid engine.
        side.kind = lilia::config::SideKind::Human;
        side.bot.reset();
        return;
      }

      if (side.bot->uciValues.empty())
      {
        auto hydrated = reg.makeDefaultBotConfig(side.bot->engine.engineId);
        if (!hydrated.engine.engineId.empty())
        {
          hydrated.limits = side.bot->limits;
          side.bot = std::move(hydrated);
        }
      }
    };

    auto applyPickedEngine = [&](lilia::config::SideConfig &side, const lilia::config::EngineRef &picked)
    {
      // Preserve existing limits, but reset UCI values to the new engine's defaults.
      lilia::config::SearchLimits limits{};
      if (side.bot)
        limits = side.bot->limits;

      auto bc = reg.makeDefaultBotConfig(picked.engineId);
      bc.limits = limits;

      side.kind = lilia::config::SideKind::Engine;
      side.bot = std::move(bc);
    };

    const ui::Theme &theme = m_theme.uiTheme();

    // --- Time Control Picker (top-center; config only visible when ON) ---
    ui::TimeControlPicker timePicker(m_font, theme);
    timePicker.setValue({cfg.game.tc.enabled, cfg.game.tc.baseSeconds, cfg.game.tc.incrementSeconds});

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
    loadBtn.setText("Load Game", 16);

    // Track modals
    ui::PalettePickerModal *paletteModal = nullptr;
    BotCatalogModal *whiteCatalog = nullptr;
    BotCatalogModal *blackCatalog = nullptr;
    ui::GameSetupModal *setupModal = nullptr;

    std::optional<std::string> palettePicked{};

    auto openWhiteCatalog = [&]
    {
      const std::string cur = (cfg.white.bot ? cfg.white.bot->engine.engineId : std::string{});
      auto m = std::make_unique<BotCatalogModal>(m_font, theme, cur);
      whiteCatalog = m.get();
      modals.push(std::move(m));
      modals.layout(m_window.getSize());
    };

    auto openBlackCatalog = [&]
    {
      const std::string cur = (cfg.black.bot ? cfg.black.bot->engine.engineId : std::string{});
      auto m = std::make_unique<BotCatalogModal>(m_font, theme, cur);
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
      p.onPick = [&](const std::string &name) { palettePicked = name; };
      p.onClose = [&]() {};

      m->open(m_window.getSize(), std::move(p));
      modals.push(std::move(m));
      modals.layout(m_window.getSize()); });

    // Side selection: Human top, Bot bottom.
    whiteHuman.setOnClick([&]
                          { setHumanSide(cfg.white); });

    whiteBot.setOnClick([&]
                        {
      ensureBotSide(cfg.white);     // auto-select default engine immediately
      openWhiteCatalog(); });

    blackHuman.setOnClick([&]
                          { setHumanSide(cfg.black); });

    blackBot.setOnClick([&]
                        {
      ensureBotSide(cfg.black);     // auto-select default engine immediately
      openBlackCatalog(); });

    // Engine buttons remain as "change engine" affordance when Bot is active.
    whiteEngineBtn.setOnClick([&]
                              {
      ensureBotSide(cfg.white);
      openWhiteCatalog(); });
    blackEngineBtn.setOnClick([&]
                              {
      ensureBotSide(cfg.black);
      openBlackCatalog(); });

    loadBtn.setOnClick([&]
                       {
      auto m = std::make_unique<ui::GameSetupModal>(m_font, theme, focus);
      setupModal = m.get();

      setupModal->setOnRequestPgnUpload([&, modalPtr = setupModal]
      {
        std::optional<std::string> path = openPgnFileDialog();
        if (!path || path->empty())
        {
          const std::string clip = sf::Clipboard::getString().toAnsiString();
          if (!clip.empty())
            path = clip;
        }

        if (!path || path->empty())
          return;

        auto imported = ui::game_setup::import_pgn_file(*path);
        if (!imported)
          return;

        modalPtr->setPgnFilename(imported->filename);
        modalPtr->setPgnText(imported->pgn);
      });

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

      const float panelW = std::min(980.f, std::max(760.f, float(ws.x) - 90.f));
      const float panelH = std::min(620.f, std::max(520.f, float(ws.y) - 140.f));

      panel = ui::anchoredCenter(ws, {panelW, panelH});
      sf::FloatRect inner = ui::inset(panel, 24.f);

      paletteBtn.setBounds({20.f, float(ws.y) - 54.f, 140.f, 34.f});

      const float bottomPad = 26.f;
      const float startW = 300.f;
      const float startH = 56.f;
      const float loadH = 42.f;
      const float gap = 12.f;

      const float loadW = std::min(640.f, panel.width / 4);
      const float loadX = panel.left + (panel.width - loadW) * 0.5f;
      const float loadY = panel.top + panel.height - bottomPad - loadH;

      const float startX = panel.left + (panel.width - startW) * 0.5f;
      const float startY = loadY - gap - startH;

      startBtn.setBounds({startX, startY, startW, startH});
      loadBtn.setBounds({loadX, loadY, loadW, loadH});

      titlePos = ui::snap({panel.left + 24.f, panel.top + 18.f});
      subtitlePos = ui::snap({panel.left + 24.f, panel.top + 52.f});

      const float contentTop = panel.top + 92.f;
      const float contentBottom = startY - 18.f;
      const float contentH = std::max(0.f, contentBottom - contentTop);

      const float colGap = 22.f;
      const float desiredTimeW = 520.f;
      const float minSideW = 190.f;

      float availableW = inner.width;
      float sideW = (availableW - desiredTimeW - 2.f * colGap) * 0.5f;

      if (sideW < minSideW)
        sideW = minSideW;

      float timeW = availableW - 2.f * sideW - 2.f * colGap;
      if (timeW < 360.f)
      {
        float deficit = 360.f - timeW;
        float take = deficit * 0.5f;
        sideW = std::max(minSideW, sideW - take);
        timeW = availableW - 2.f * sideW - 2.f * colGap;
      }

      whiteCol = {inner.left, contentTop, sideW, contentH};
      timeCol = {whiteCol.left + whiteCol.width + colGap, contentTop, timeW, contentH};
      blackCol = {timeCol.left + timeCol.width + colGap, contentTop, sideW, contentH};

      whiteLabelPos = ui::snap({whiteCol.left, whiteCol.top});
      blackLabelPos = ui::snap({blackCol.left, blackCol.top});

      const float cardTopPad = 34.f;
      whiteCard = {whiteCol.left, whiteCol.top + cardTopPad, whiteCol.width, whiteCol.height - cardTopPad};
      blackCard = {blackCol.left, blackCol.top + cardTopPad, blackCol.width, blackCol.height - cardTopPad};
      timeCard = {timeCol.left, timeCol.top, timeCol.width, timeCol.height};

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

      sf::FloatRect timeBounds = {timeCard.left, timeCard.top, timeCard.width, 160.f};
      timePicker.setBounds(timeBounds);
    };

    layoutAll();

    while (m_window.isOpen() && !done)
    {
      float dt = frame.restart().asSeconds();
      (void)dt;

      sf::Event e{};
      while (m_window.pollEvent(e))
      {
        if (e.type == sf::Event::Closed)
          m_window.close();

        if (e.type == sf::Event::Resized)
        {
          sf::View v(sf::FloatRect(0.f, 0.f, float(e.size.width), float(e.size.height)));
          m_window.setView(v);

          layoutAll();
          modals.layout(m_window.getSize());
        }

        if (e.type == sf::Event::MouseMoved)
          mouse = {float(e.mouseMove.x), float(e.mouseMove.y)};

        modals.handleEvent(e, mouse);
        if (modals.hasOpenModal())
          continue;

        const bool whiteIsBot =
            (cfg.white.kind == lilia::config::SideKind::Engine) &&
            cfg.white.bot.has_value() &&
            !cfg.white.bot->engine.engineId.empty();

        const bool blackIsBot =
            (cfg.black.kind == lilia::config::SideKind::Engine) &&
            cfg.black.bot.has_value() &&
            !cfg.black.bot->engine.engineId.empty();

        paletteBtn.updateHover(mouse);

        whiteHuman.updateHover(mouse);
        whiteBot.updateHover(mouse);
        blackHuman.updateHover(mouse);
        blackBot.updateHover(mouse);

        if (whiteIsBot)
          whiteEngineBtn.updateHover(mouse);
        if (blackIsBot)
          blackEngineBtn.updateHover(mouse);

        startBtn.updateHover(mouse);
        loadBtn.updateHover(mouse);

        timePicker.updateHover(mouse);

        if (paletteBtn.handleEvent(e, mouse))
          continue;

        if (whiteHuman.handleEvent(e, mouse))
          continue;
        if (whiteBot.handleEvent(e, mouse))
          continue;

        if (whiteIsBot && whiteEngineBtn.handleEvent(e, mouse))
          continue;

        if (blackHuman.handleEvent(e, mouse))
          continue;
        if (blackBot.handleEvent(e, mouse))
          continue;

        if (blackIsBot && blackEngineBtn.handleEvent(e, mouse))
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
            applyPickedEngine(cfg.white, *picked);

          // Ensure invariants even if modal closed without a selection.
          if (cfg.white.kind == lilia::config::SideKind::Engine)
            ensureBotSide(cfg.white);

          whiteCatalog = nullptr;
        }

        if (blackCatalog && &modal == blackCatalog)
        {
          if (auto picked = blackCatalog->picked())
            applyPickedEngine(cfg.black, *picked);

          // Ensure invariants even if modal closed without a selection.
          if (cfg.black.kind == lilia::config::SideKind::Engine)
            ensureBotSide(cfg.black);

          blackCatalog = nullptr;
        }

        if (setupModal && &modal == setupModal)
        {
          if (auto fen = setupModal->resultFen())
            cfg.game.startFen = *fen;

          if (auto pgn = setupModal->resultPgn())
          {
            cfg.replay.enabled = true;
            cfg.replay.pgnText = *pgn;

            if (auto fn = setupModal->resultPgnFilename())
              cfg.replay.pgnFilename = *fn;
          }
          else
          {
            cfg.replay.enabled = false;
            cfg.replay.pgnText.clear();
            cfg.replay.pgnFilename.clear();
          }

          setupModal = nullptr;
        } });

      // Pull current time settings from picker into canonical config
      {
        auto tv = timePicker.value();
        cfg.game.tc.enabled = tv.enabled;
        cfg.game.tc.baseSeconds = tv.baseSeconds;
        cfg.game.tc.incrementSeconds = tv.incrementSeconds;
      }

      m_window.setView(m_window.getDefaultView());

      // Draw
      m_window.clear();
      ui::drawVerticalGradient(m_window, m_window.getSize(), theme.bgTop, theme.bgBottom);

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

      sf::Text wl("White", m_font, 20);
      wl.setFillColor(theme.text);
      wl.setPosition(whiteLabelPos);
      m_window.draw(wl);

      sf::Text bl("Black", m_font, 20);
      bl.setFillColor(theme.text);
      bl.setPosition(blackLabelPos);
      m_window.draw(bl);

      auto drawCard = [&](const sf::FloatRect &r)
      {
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

      const bool whiteIsBot = (cfg.white.kind == lilia::config::SideKind::Engine) && cfg.white.bot.has_value();
      const bool blackIsBot = (cfg.black.kind == lilia::config::SideKind::Engine) && cfg.black.bot.has_value();

      whiteHuman.setActive(!whiteIsBot);
      whiteBot.setActive(whiteIsBot);

      blackHuman.setActive(!blackIsBot);
      blackBot.setActive(blackIsBot);

      whiteEngineBtn.setEnabled(whiteIsBot);
      blackEngineBtn.setEnabled(blackIsBot);

      if (whiteIsBot)
        whiteEngineBtn.setText(engineLabel(cfg.white.bot->engine), 16);
      else
        whiteEngineBtn.setText("Select Bot...", 16);

      if (blackIsBot)
        blackEngineBtn.setText(engineLabel(cfg.black.bot->engine), 16);
      else
        blackEngineBtn.setText("Select Bot...", 16);

      paletteBtn.draw(m_window);

      whiteHuman.draw(m_window);
      whiteBot.draw(m_window);
      if (whiteIsBot)
        whiteEngineBtn.draw(m_window);

      blackHuman.draw(m_window);
      blackBot.draw(m_window);
      if (blackIsBot)
        blackEngineBtn.draw(m_window);

      timePicker.draw(m_window);

      startBtn.draw(m_window);
      loadBtn.draw(m_window);

      {
        sf::Text ver(std::string(core::SANDBOX_VERSION), m_font, 14);
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
      cfg.game.tc.enabled = tv.enabled;
      cfg.game.tc.baseSeconds = tv.baseSeconds;
      cfg.game.tc.incrementSeconds = tv.incrementSeconds;
    }

    // Hard guarantee: Engine side always has a bot config (prevents "nobody plays").
    if (cfg.white.kind == lilia::config::SideKind::Engine)
      ensureBotSide(cfg.white);
    if (cfg.black.kind == lilia::config::SideKind::Engine)
      ensureBotSide(cfg.black);

    return cfg;
  }

} // namespace lilia::view
