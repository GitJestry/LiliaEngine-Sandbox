#include <SFML/Window/Clipboard.hpp>
#include <SFML/Window/Keyboard.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <optional>
#include <string>

#include "lilia/constants.hpp"
#include "lilia/view/ui/render/render_constants.hpp"
#include "lilia/view/ui/render/texture_table.hpp"

#include "lilia/view/ui/style/style.hpp"
#include "lilia/view/ui/style/theme.hpp"
#include "lilia/view/ui/widgets/button.hpp"

#include "lilia/view/ui/style/modals/game_setup/position_builder.hpp"
#include "lilia/view/ui/style/modals/game_setup/position_builder_rules.hpp"

namespace lilia::view
{
  using pb::Board;
  using pb::FenMeta;

  struct PositionBuilder::Impl
  {
    // Persist last used FEN across opens (keeps prior behavior).
    static std::string s_lastFen;

    const ui::Theme *theme{nullptr};
    const sf::Font *font{nullptr};

    sf::FloatRect bounds{};
    sf::FloatRect boardRect{};
    sf::FloatRect leftRect{};
    sf::FloatRect rightRect{};
    sf::FloatRect bottomRect{};
    sf::FloatRect fenRect{};
    sf::FloatRect shortcutsRect{};
    sf::FloatRect fenBoxRect{};

    float sq{44.f};
    float pieceYOffset{0.f};

    Board board{};
    FenMeta meta{};

    int whiteK{0};
    int blackK{0};

    enum class ToolKind
    {
      Move,
      Piece
    };
    struct ToolSelection
    {
      ToolKind kind{ToolKind::Move};
      char piece{'.'};
      static ToolSelection move() { return {ToolKind::Move, '.'}; }
      static ToolSelection pieceSel(char p) { return {ToolKind::Piece, p}; }
    };

    ToolSelection selected = ToolSelection::move();
    bool placeWhite{true};
    char lastAddLower{'p'};

    // Drag
    bool dragging{false};
    char dragPiece{'.'};
    std::optional<std::pair<int, int>> dragFrom{};

    // Hover / input
    sf::Vector2f mouseGlobal{};
    sf::Vector2f offset{};
    std::optional<std::pair<int, int>> hoverSquare{};

    // EP selection mode
    bool epSelecting{false};

    // Toasts
    mutable float toastT{0.f};
    mutable float toastDur{0.9f};
    mutable std::string toastMsg{};
    mutable sf::Color toastColor{sf::Color(122, 205, 164)};

    // Error toast + shake (kept)
    mutable float errT{0.f};
    mutable float errDur{1.1f};
    mutable std::string errMsg{};
    mutable float shakeT{0.f};
    mutable float shakeDur{0.18f};
    mutable float shakePhase{0.f};

    mutable sf::Clock animClock{};

    // Textures
    mutable bool texReady{false};
    mutable const sf::Texture *texWhite{nullptr};
    mutable const sf::Texture *texBlack{nullptr};
    mutable std::array<const sf::Texture *, 12> pieceTex{};
    mutable sf::Sprite sqWhite{};
    mutable sf::Sprite sqBlack{};
    mutable std::array<sf::Sprite, 12> pieceTpl{};

    // UI buttons (reuse your existing Button)
    mutable ui::Button btnLeftWhite, btnLeftBlack;
    mutable ui::Button btnLeftAdd, btnLeftMove;
    mutable ui::Button btnLeftClear, btnLeftReset;

    mutable ui::Button btnTurnW, btnTurnB;
    mutable ui::Button btnCastleK, btnCastleQ, btnCastlek, btnCastleq;
    mutable ui::Button btnEp;
    mutable ui::Button btnCopyFen;
    mutable ui::Button btnShortcuts;

    struct PieceBtn
    {
      ui::Button bg;
      char pc{'.'};
      sf::FloatRect r{};
    };
    mutable std::array<PieceBtn, 12> pieceBtns{};

    bool showShortcuts{false};

    // ---------- construction / defaults ----------
    Impl()
    {
      pb::clearBoard(board);
      resetToStart(false);
      if (!s_lastFen.empty())
        setFromFen(s_lastFen, false);

      enterAddDefault();
    }

    void applyThemeFont()
    {
      auto apply = [&](ui::Button &b)
      {
        b.setTheme(theme);
        if (font)
          b.setFont(*font);
      };

      apply(btnLeftWhite);
      apply(btnLeftBlack);
      apply(btnLeftAdd);
      apply(btnLeftMove);
      apply(btnLeftClear);
      apply(btnLeftReset);

      apply(btnTurnW);
      apply(btnTurnB);
      apply(btnCastleK);
      apply(btnCastleQ);
      apply(btnCastlek);
      apply(btnCastleq);
      apply(btnEp);
      apply(btnCopyFen);
      apply(btnShortcuts);

      for (auto &pbx : pieceBtns)
        apply(pbx.bg);

      // Static texts (sizes can be tuned if you want)
      btnLeftWhite.setText("White", 12);
      btnLeftBlack.setText("Black", 12);
      btnLeftAdd.setText("Add", 12);
      btnLeftMove.setText("Move", 12);
      btnLeftClear.setText("Clear", 12);
      btnLeftReset.setText("Reset", 12);

      btnTurnW.setText("White", 12);
      btnTurnB.setText("Black", 12);

      btnCastleK.setText("K", 12);
      btnCastleQ.setText("Q", 12);
      btnCastlek.setText("k", 12);
      btnCastleq.setText("q", 12);

      btnCopyFen.setText("Copy", 12);
      btnShortcuts.setText("Shortcuts", 12);

      // piece backgrounds are icon-only
      for (auto &pbx : pieceBtns)
        pbx.bg.setText("", 12);
    }

    void hookCallbacks()
    {
      // Left panel
      btnLeftWhite.setOnClick([&]
                              {
        placeWhite = true;
        if (selected.kind == ToolKind::Piece)
          selected.piece = applyColorToPieceType(lastAddLower, true);
        rememberCurrent(); });

      btnLeftBlack.setOnClick([&]
                              {
        placeWhite = false;
        if (selected.kind == ToolKind::Piece)
          selected.piece = applyColorToPieceType(lastAddLower, false);
        rememberCurrent(); });

      btnLeftAdd.setOnClick([&]
                            { enterAddDefault(); rememberCurrent(); });
      btnLeftMove.setOnClick([&]
                             { selected = ToolSelection::move(); rememberCurrent(); });
      btnLeftClear.setOnClick([&]
                              { clear(true); });
      btnLeftReset.setOnClick([&]
                              { resetToStart(true); });

      // Turn
      btnTurnW.setOnClick([&]
                          { meta.sideToMove = 'w'; sanitizeMeta(); rememberCurrent(); });
      btnTurnB.setOnClick([&]
                          { meta.sideToMove = 'b'; sanitizeMeta(); rememberCurrent(); });

      // Castling toggles with structural validation (per your request).
      btnCastleK.setOnClick([&]
                            { toggleCastle(true, true); });
      btnCastleQ.setOnClick([&]
                            { toggleCastle(true, false); });
      btnCastlek.setOnClick([&]
                            { toggleCastle(false, true); });
      btnCastleq.setOnClick([&]
                            { toggleCastle(false, false); });

      // EP select tool:
      btnEp.setOnClick([&]
                       {
        // clicking again clears and exits
        if (epSelecting || meta.epTarget)
        {
          epSelecting = false;
          meta.epTarget.reset();
          rememberCurrent();
          return;
        }

        epSelecting = true;
        showToast("Select an en passant target square.", sf::Color(122, 205, 164)); });

      // Copy FEN
      btnCopyFen.setOnClick([&]
                            {
        if (!pb::kingsOk(board))
        {
          invalidAction("Cannot copy FEN.\nPosition must contain exactly one king per side.");
          return;
        }
        sanitizeMeta();
        sf::Clipboard::setString(fen());
        showToast("FEN copied to clipboard.", sf::Color(122, 205, 164)); });

      // Shortcuts
      btnShortcuts.setOnClick([&]
                              {
        showShortcuts = !showShortcuts;
        rebuildGeometry(); });

      // Piece buttons
      for (int i = 0; i < 12; ++i)
      {
        pieceBtns[i].pc = pieceCharFromIndex(i);
        pieceBtns[i].bg.setOnClick([&, i]
                                   {
          const char chosen = pieceBtns[i].pc;
          placeWhite = std::isupper(static_cast<unsigned char>(chosen));
          lastAddLower = char(std::tolower(static_cast<unsigned char>(chosen)));
          selected = ToolSelection::pieceSel(chosen);
          rememberCurrent(); });
      }
    }

    // ---------- state helpers ----------
    static char applyColorToPieceType(char lowerPiece, bool white)
    {
      const char l = char(std::tolower(static_cast<unsigned char>(lowerPiece)));
      return white ? char(std::toupper(static_cast<unsigned char>(l))) : l;
    }

    static char pieceCharFromIndex(int idx)
    {
      static constexpr std::array<char, 12> pieces{
          'P', 'B', 'N', 'R', 'Q', 'K',
          'p', 'b', 'n', 'r', 'q', 'k'};
      return pieces[idx];
    }

    void enterAddDefault()
    {
      lastAddLower = 'p';
      selected = ToolSelection::pieceSel(applyColorToPieceType('p', placeWhite));
    }

    void refreshKings()
    {
      pb::countKings(board, whiteK, blackK);
    }

    void sanitizeMeta()
    {
      pb::sanitizeMeta(board, meta);
      // EP tool should never be "selecting" if EP already set (single source of truth).
      if (meta.epTarget && epSelecting)
        epSelecting = false;
    }

    void rememberCurrent()
    {
      sanitizeMeta();
      s_lastFen = fen();
    }

    // ---------- FEN ----------
    std::string fen() const
    {
      // (const) call through pb::fen, but meta may need sanitize in mutable contexts; callers generally call rememberCurrent().
      return pb::fen(board, meta);
    }

    std::string fenForUse() const
    {
      if (!pb::kingsOk(board))
        return {};
      return fen();
    }

    // ---------- error / toast ----------
    void invalidAction(const std::string &msg) const
    {
      errMsg = msg;
      errT = errDur;
      shakeT = shakeDur;
      shakePhase = 0.f;
    }

    void showToast(std::string msg, sf::Color c) const
    {
      toastMsg = std::move(msg);
      toastColor = c;
      toastT = toastDur;
    }

    static float clampDt(float dt)
    {
      if (dt < 0.f)
        return 0.f;
      if (dt > 0.05f)
        return 0.05f;
      return dt;
    }

    void animate(float dt) const
    {
      if (shakeT > 0.f)
      {
        shakeT = std::max(0.f, shakeT - dt);
        shakePhase += dt * 55.f;
      }
      if (errT > 0.f)
        errT = std::max(0.f, errT - dt);

      if (toastT > 0.f)
        toastT = std::max(0.f, toastT - dt);
    }

    // ---------- board access ----------
    char at(int x, int y) const { return board[y][x]; }
    void set(int x, int y, char p) { board[y][x] = p; }

    bool wouldViolateKingUniqueness(int x, int y, char newP) const
    {
      if (newP != 'K' && newP != 'k')
        return false;
      const char old = at(x, y);
      if (old == newP)
        return false;

      int count = 0;
      for (int yy = 0; yy < 8; ++yy)
        for (int xx = 0; xx < 8; ++xx)
        {
          if (xx == x && yy == y)
            continue;
          if (at(xx, yy) == newP)
            ++count;
        }
      return count >= 1;
    }

    bool trySet(int x, int y, char p, bool remember)
    {
      if ((p == 'K' || p == 'k') && wouldViolateKingUniqueness(x, y, p))
        return false;

      set(x, y, p);
      refreshKings();
      sanitizeMeta();

      if (remember)
        rememberCurrent();

      return true;
    }

    // ---------- EP selection ----------
    std::optional<std::pair<int, int>> squareFromMouse(sf::Vector2f local) const
    {
      if (!boardRect.contains(local))
        return std::nullopt;
      const int x = int((local.x - boardRect.left) / sq);
      const int y = int((local.y - boardRect.top) / sq);
      if (!pb::inBounds(x, y))
        return std::nullopt;
      return std::make_pair(x, y);
    }

    // ---------- castling toggle ----------
    void toggleCastle(bool white, bool kingSide)
    {
      bool &flag =
          white
              ? (kingSide ? meta.castleK : meta.castleQ)
              : (kingSide ? meta.castlek : meta.castleq);

      if (flag)
      {
        flag = false;
        rememberCurrent();
        return;
      }

      if (!pb::hasCastleStructure(board, white, kingSide))
      {
        invalidAction("Castling right is not valid.\nKing/Rook must be on start squares.");
        return;
      }

      flag = true;
      rememberCurrent();
    }

    // ---------- clear/reset ----------
    void clear(bool remember)
    {
      pb::clearBoard(board);

      dragging = false;
      dragPiece = '.';
      dragFrom.reset();

      meta.sideToMove = 'w';
      meta.castleK = meta.castleQ = meta.castlek = meta.castleq = false;
      meta.epTarget.reset();
      meta.halfmove = 0;
      meta.fullmove = 1;

      epSelecting = false;

      refreshKings();
      if (remember)
        rememberCurrent();
    }

    void resetToStart(bool remember)
    {
      clear(false);
      setFromFen(core::START_FEN, false);

      // Startpos defaults:
      meta.sideToMove = 'w';
      meta.castleK = meta.castleQ = meta.castlek = meta.castleq = true;
      meta.epTarget.reset();
      meta.halfmove = 0;
      meta.fullmove = 1;

      epSelecting = false;

      sanitizeMeta();
      if (remember)
        rememberCurrent();
    }

    void setFromFen(const std::string &fenStr, bool remember)
    {
      pb::setFromFen(board, meta, fenStr);
      epSelecting = false;
      refreshKings();
      sanitizeMeta();
      if (remember)
        rememberCurrent();
    }

    // ---------- layout ----------
    static sf::FloatRect insetRect(sf::FloatRect r, float p)
    {
      r.left += p;
      r.top += p;
      r.width = std::max(0.f, r.width - 2.f * p);
      r.height = std::max(0.f, r.height - 2.f * p);
      return r;
    }

    static sf::FloatRect consumeRow(sf::FloatRect &r, float h, float gapAfter)
    {
      sf::FloatRect out = r;
      out.height = std::min(h, r.height);
      r.top += out.height + gapAfter;
      r.height = std::max(0.f, r.height - out.height - gapAfter);
      return out;
    }

    void rebuildGeometry()
    {
      if (bounds.width <= 0.f || bounds.height <= 0.f)
        return;

      const float pad = 14.f;
      const float gap = 14.f;
      const float topInset = 16.f;

      const float fenH = 84.f;
      const float shortcutsGap = 10.f;
      const float shortcutsH = 88.f;
      const float bottomH = fenH + (showShortcuts ? (shortcutsGap + shortcutsH) : 0.f);

      const float sideW = std::clamp(bounds.width * 0.20f, 160.f, 240.f);

      float availW = bounds.width - pad * 2.f - sideW * 2.f - gap * 2.f;
      float availHTotal = bounds.height - pad * 2.f - topInset;

      float boardMaxH = std::max(240.f, availHTotal - bottomH);
      float boardSize = std::min(availW, boardMaxH);
      boardSize = std::max(240.f, boardSize);

      sq = boardSize / 8.f;
      pieceYOffset = sq * 0.03f;

      const float blockH = boardSize + bottomH;
      const float blockTop = bounds.top + pad + topInset + (availHTotal - blockH) * 0.5f;

      const float midLeft = bounds.left + pad + sideW + gap;
      const float midRight = bounds.left + bounds.width - pad - sideW - gap;
      const float midW = (midRight - midLeft);

      const float boardLeft = midLeft + (midW - boardSize) * 0.5f;
      const float boardTop = blockTop;

      boardRect = {boardLeft, boardTop, boardSize, boardSize};
      leftRect = {bounds.left + pad, boardTop, sideW, boardSize};
      rightRect = {bounds.left + bounds.width - pad - sideW, boardTop, sideW, boardSize};

      bottomRect = {bounds.left + pad,
                    boardRect.top + boardRect.height + gap,
                    bounds.width - pad * 2.f,
                    bottomH};

      fenRect = {bottomRect.left, bottomRect.top, bottomRect.width, fenH};

      if (showShortcuts)
      {
        shortcutsRect = {bottomRect.left,
                         fenRect.top + fenRect.height + shortcutsGap,
                         bottomRect.width,
                         shortcutsH};
      }
      else
      {
        shortcutsRect = {};
      }

      // Left panel button layout
      {
        const float x = leftRect.left + 10.f;
        const float w = leftRect.width - 20.f;
        const float h = 34.f;
        const float g = 10.f;

        float y = leftRect.top + 52.f;
        const float half = std::floor(w * 0.5f);

        btnLeftWhite.setBounds({x, y, half, h});
        btnLeftBlack.setBounds({x + half, y, w - half, h});
        y += h + g;

        btnLeftAdd.setBounds({x, y, half, h});
        btnLeftMove.setBounds({x + half, y, w - half, h});
        y += h + g;

        y += 6.f;
        btnLeftClear.setBounds({x, y, w, h});
        y += h + g;
        btnLeftReset.setBounds({x, y, w, h});
      }

      // Right panel piece grid layout
      {
        const float padR = 10.f;
        const float left = rightRect.left + padR;
        const float w = rightRect.width - padR * 2.f;

        const float titleZone = 54.f;
        const float top = rightRect.top + titleZone;

        const float cellGap = 10.f;
        const float sep = 18.f;
        const float cell = std::clamp(std::floor((w - cellGap * 2.f) / 3.f), 44.f, 78.f);

        auto rectAt = [&](int col, int row, float baseY) -> sf::FloatRect
        {
          return {left + col * (cell + cellGap), baseY + row * (cell + cellGap), cell, cell};
        };

        float y0 = top;
        pieceBtns[0].r = rectAt(0, 0, y0);
        pieceBtns[1].r = rectAt(1, 0, y0);
        pieceBtns[2].r = rectAt(2, 0, y0);
        pieceBtns[3].r = rectAt(0, 1, y0);
        pieceBtns[4].r = rectAt(1, 1, y0);
        pieceBtns[5].r = rectAt(2, 1, y0);

        float y1 = y0 + 2.f * cell + cellGap + sep;
        pieceBtns[6].r = rectAt(0, 0, y1);
        pieceBtns[7].r = rectAt(1, 0, y1);
        pieceBtns[8].r = rectAt(2, 0, y1);
        pieceBtns[9].r = rectAt(0, 1, y1);
        pieceBtns[10].r = rectAt(1, 1, y1);
        pieceBtns[11].r = rectAt(2, 1, y1);

        for (auto &pbx : pieceBtns)
          pbx.bg.setBounds(pbx.r);
      }

      // FEN panel layout
      {
        sf::FloatRect inner = insetRect(fenRect, 10.f);
        sf::FloatRect row1 = consumeRow(inner, 30.f, 8.f);
        sf::FloatRect row2 = consumeRow(inner, 32.f, 0.f);

        const float btnH = 28.f;
        const float y = row1.top + 1.f;
        float x = row1.left;

        // Turn segmented
        const float turnW = 78.f;
        btnTurnW.setBounds({x, y, turnW, btnH});
        btnTurnB.setBounds({x + turnW, y, turnW, btnH});
        x += (turnW * 2.f + 12.f);

        // Castling toggles
        const float small = 30.f;
        btnCastleK.setBounds({x, y, small, btnH});
        btnCastleQ.setBounds({x + (small + 6.f), y, small, btnH});
        btnCastlek.setBounds({x + 2.f * (small + 6.f), y, small, btnH});
        btnCastleq.setBounds({x + 3.f * (small + 6.f), y, small, btnH});
        x += (4.f * small + 3.f * 6.f + 12.f);

        // Right aligned shortcuts
        const float rightBtnW = 120.f;
        btnShortcuts.setBounds({row1.left + row1.width - rightBtnW, y, rightBtnW, btnH});

        // EP button takes remaining space
        const float rightLimit = btnShortcuts.bounds().left - 12.f;
        const float epW = std::max(120.f, rightLimit - x);
        btnEp.setBounds({x, y, epW, btnH});

        // Row2: fen box + copy
        const float copyW = 92.f;
        btnCopyFen.setBounds({row2.left + row2.width - copyW, row2.top + 1.f, copyW, 30.f});
        fenBoxRect = {row2.left, row2.top + 1.f, row2.width - copyW - 10.f, 30.f};
      }

      texReady = false;
      refreshKings();
      sanitizeMeta();
    }

    // ---------- textures ----------
    static int typeIndexFromLower(char lower)
    {
      switch (lower)
      {
      case 'p':
        return 0;
      case 'n':
        return 1;
      case 'b':
        return 2;
      case 'r':
        return 3;
      case 'q':
        return 4;
      case 'k':
        return 5;
      default:
        return -1;
      }
    }

    static std::string pieceFilenameFromChar(char p)
    {
      const bool white = std::isupper(static_cast<unsigned char>(p));
      const char lower = char(std::tolower(static_cast<unsigned char>(p)));
      const int t = typeIndexFromLower(lower);
      if (t < 0)
        return {};

      const int colorIdx = white ? 0 : 1;
      const int idx = t + 6 * colorIdx;
      return std::string{constant::path::PIECES_DIR} + "/piece_" + std::to_string(idx) + ".png";
    }

    static int pieceSlotFromChar(char p)
    {
      const bool white = std::isupper(static_cast<unsigned char>(p));
      const char lower = char(std::tolower(static_cast<unsigned char>(p)));
      const int t = typeIndexFromLower(lower);
      if (t < 0)
        return -1;
      return t + (white ? 0 : 6);
    }

    void ensureTextures() const
    {
      if (texReady)
        return;

      texWhite = &TextureTable::getInstance().get(std::string{constant::tex::WHITE});
      texBlack = &TextureTable::getInstance().get(std::string{constant::tex::BLACK});

      if (texWhite && texWhite->getSize().x > 0)
      {
        sqWhite.setTexture(*texWhite, true);
        const auto sz = texWhite->getSize();
        sqWhite.setScale(sq / float(sz.x), sq / float(sz.y));
      }
      if (texBlack && texBlack->getSize().x > 0)
      {
        sqBlack.setTexture(*texBlack, true);
        const auto sz = texBlack->getSize();
        sqBlack.setScale(sq / float(sz.x), sq / float(sz.y));
      }

      pieceTex.fill(nullptr);
      for (int i = 0; i < 12; ++i)
        pieceTpl[i] = sf::Sprite{};

      auto load = [&](char p)
      {
        const int slot = pieceSlotFromChar(p);
        if (slot < 0 || slot >= 12)
          return;

        const std::string fn = pieceFilenameFromChar(p);
        if (fn.empty())
          return;

        const sf::Texture &t = TextureTable::getInstance().get(fn);
        pieceTex[slot] = &t;

        sf::Sprite spr;
        spr.setTexture(t, true);

        const auto ts = t.getSize();
        spr.setOrigin(float(ts.x) * 0.5f, float(ts.y) * 0.5f);

        const float target = sq * 0.92f;
        const float scale = (ts.y > 0) ? (target / float(ts.y)) : 1.f;
        spr.setScale(scale, scale);

        pieceTpl[slot] = spr;
      };

      load('P');
      load('B');
      load('N');
      load('R');
      load('Q');
      load('K');
      load('p');
      load('b');
      load('n');
      load('r');
      load('q');
      load('k');

      texReady = true;
    }

    sf::Sprite spriteForPiece(char p) const
    {
      ensureTextures();
      const int slot = pieceSlotFromChar(p);
      if (slot < 0 || slot >= 12 || !pieceTex[slot])
        return sf::Sprite{};
      return pieceTpl[slot];
    }

    // ---------- input ----------
    void updateHover(sf::Vector2f mouse, sf::Vector2f off)
    {
      mouseGlobal = mouse;
      offset = off;

      const sf::Vector2f local{mouse.x - off.x, mouse.y - off.y};
      hoverSquare = squareFromMouse(local);

      // Buttons hover
      auto hov = [&](ui::Button &b)
      { b.updateHover(mouse, off); };
      hov(btnLeftWhite);
      hov(btnLeftBlack);
      hov(btnLeftAdd);
      hov(btnLeftMove);
      hov(btnLeftClear);
      hov(btnLeftReset);

      hov(btnTurnW);
      hov(btnTurnB);
      hov(btnCastleK);
      hov(btnCastleQ);
      hov(btnCastlek);
      hov(btnCastleq);
      hov(btnEp);
      hov(btnCopyFen);
      hov(btnShortcuts);

      for (auto &pbx : pieceBtns)
        pbx.bg.updateHover(mouse, off);
    }

    bool handleEvent(const sf::Event &e, sf::Vector2f mouse, sf::Vector2f off)
    {
      if (!theme || !font)
        return false;

      mouseGlobal = mouse;
      offset = off;

      const sf::Vector2f local{mouse.x - off.x, mouse.y - off.y};

      // Keyboard
      if (e.type == sf::Event::KeyPressed)
      {
        const bool shift = e.key.shift;

        if (e.key.code == sf::Keyboard::Tab)
        {
          placeWhite = !placeWhite;
          if (selected.kind == ToolKind::Piece)
            selected.piece = applyColorToPieceType(lastAddLower, placeWhite);
          rememberCurrent();
          return true;
        }

        if (e.key.code == sf::Keyboard::Space)
        {
          meta.sideToMove = (meta.sideToMove == 'w') ? 'b' : 'w';
          sanitizeMeta();
          rememberCurrent();
          return true;
        }

        if (e.key.code == sf::Keyboard::H)
        {
          showShortcuts = !showShortcuts;
          rebuildGeometry();
          return true;
        }

        if (e.key.code == sf::Keyboard::A)
        {
          enterAddDefault();
          return true;
        }

        if (e.key.code == sf::Keyboard::M)
        {
          selected = ToolSelection::move();
          return true;
        }

        if (e.key.code == sf::Keyboard::C)
        {
          clear(true);
          return true;
        }

        if (e.key.code == sf::Keyboard::R)
        {
          resetToStart(true);
          return true;
        }

        char placed = '.';
        if (e.key.code == sf::Keyboard::Num1)
          placed = 'p';
        if (e.key.code == sf::Keyboard::Num2)
          placed = 'b';
        if (e.key.code == sf::Keyboard::Num3)
          placed = 'n';
        if (e.key.code == sf::Keyboard::Num4)
          placed = 'r';
        if (e.key.code == sf::Keyboard::Num5)
          placed = 'q';
        if (e.key.code == sf::Keyboard::Num6)
          placed = 'k';

        if (placed != '.')
        {
          lastAddLower = placed;
          const bool white = shift ? false : placeWhite;
          selected = ToolSelection::pieceSel(applyColorToPieceType(placed, white));
          rememberCurrent();
          return true;
        }
      }

      // RMB deletes square (board only)
      if (e.type == sf::Event::MouseButtonPressed && e.mouseButton.button == sf::Mouse::Right)
      {
        auto sqr = squareFromMouse(local);
        if (!sqr)
          return false;

        auto [x, y] = *sqr;
        trySet(x, y, '.', true);
        return true;
      }

      // Route mouse events to buttons first (prevents fighting with board)
      auto routeButtons = [&]() -> bool
      {
        auto h = [&](ui::Button &b) -> bool
        { return b.handleEvent(e, mouse, off); };

        if (h(btnCopyFen))
          return true;
        if (h(btnShortcuts))
          return true;

        if (h(btnTurnW) || h(btnTurnB))
          return true;
        if (h(btnCastleK) || h(btnCastleQ) || h(btnCastlek) || h(btnCastleq))
          return true;
        if (h(btnEp))
          return true;

        if (h(btnLeftWhite) || h(btnLeftBlack))
          return true;
        if (h(btnLeftAdd) || h(btnLeftMove))
          return true;
        if (h(btnLeftClear) || h(btnLeftReset))
          return true;

        for (auto &pbx : pieceBtns)
          if (pbx.bg.handleEvent(e, mouse, off))
            return true;

        return false;
      };

      if (routeButtons())
        return true;

      // Board LMB interactions
      if (e.type == sf::Event::MouseButtonPressed && e.mouseButton.button == sf::Mouse::Left)
      {
        auto sqr = squareFromMouse(local);
        if (!sqr)
          return false;

        auto [x, y] = *sqr;

        // EP selection tool consumes the next board click
        if (epSelecting)
        {
          if (!pb::isValidEnPassantTarget(board, x, y, meta.sideToMove))
          {
            invalidAction("Invalid en passant square.\nChoose a valid EP target for the side to move.");
            return true;
          }
          meta.epTarget = std::make_pair(x, y);
          epSelecting = false;
          rememberCurrent();
          return true;
        }

        if (selected.kind == ToolKind::Piece)
        {
          if (!trySet(x, y, selected.piece, true))
            invalidAction("Kings must be unique per color.\nUse Move to reposition an existing king.");
          return true;
        }

        // Move tool: pick up piece
        const char p = at(x, y);
        if (p != '.')
        {
          dragging = true;
          dragPiece = p;
          dragFrom = std::make_pair(x, y);

          set(x, y, '.');
          refreshKings();
          sanitizeMeta();
          return true;
        }

        return false;
      }

      if (e.type == sf::Event::MouseButtonReleased && e.mouseButton.button == sf::Mouse::Left)
      {
        if (!dragging)
          return false;

        dragging = false;

        auto sqr = squareFromMouse(local);
        if (sqr)
        {
          auto [tx, ty] = *sqr;
          if (!trySet(tx, ty, dragPiece, true))
          {
            invalidAction("Invalid drop.\nKings must be unique per color.");
            if (dragFrom)
            {
              auto [ox, oy] = *dragFrom;
              set(ox, oy, dragPiece);
            }
          }
        }
        else if (dragFrom)
        {
          auto [ox, oy] = *dragFrom;
          set(ox, oy, dragPiece);
        }

        dragPiece = '.';
        dragFrom.reset();
        refreshKings();
        sanitizeMeta();
        rememberCurrent();
        return true;
      }

      return false;
    }

    // ---------- draw ----------
    void draw(sf::RenderTarget &rt, sf::Vector2f off) const
    {
      if (!theme || !font)
        return;

      ensureTextures();

      const float dt = clampDt(animClock.restart().asSeconds());
      animate(dt);

      sf::RectangleShape bg({bounds.width, bounds.height});
      bg.setPosition(ui::snap({bounds.left + off.x, bounds.top + off.y}));
      bg.setFillColor(theme->panel);
      rt.draw(bg);

      sf::Vector2f shake{0.f, 0.f};
      if (shakeT > 0.f)
      {
        const float a = (shakeT / shakeDur);
        shake.x = std::sin(shakePhase) * (6.f * a);
      }

      drawSidePanels(rt, off, shake);
      drawBoard(rt, off, shake);
      drawFenPanel(rt, off);

      if (showShortcuts)
        drawShortcutsPanel(rt, off);

      if (toastT > 0.f)
        drawToast(rt, off);

      if (errT > 0.f)
        drawError(rt, off);
    }

    void drawPanel(sf::RenderTarget &rt, sf::Vector2f off, const sf::FloatRect &r, const char *title) const
    {
      sf::RectangleShape box({r.width, r.height});
      box.setPosition(ui::snap({r.left + off.x, r.top + off.y}));
      box.setFillColor(ui::darken(theme->panel, 4));
      box.setOutlineThickness(1.f);
      box.setOutlineColor(ui::darken(theme->panel, 18));
      rt.draw(box);

      sf::Text t(title, *font, 14);
      t.setFillColor(theme->text);
      t.setPosition(ui::snap({r.left + off.x + 10.f, r.top + off.y + 10.f}));
      rt.draw(t);
    }

    void drawSidePanels(sf::RenderTarget &rt, sf::Vector2f off, sf::Vector2f shake) const
    {
      (void)shake;

      drawPanel(rt, off, leftRect, "Tools");
      drawPanel(rt, off, rightRect, "Pieces");

      // Left active states
      btnLeftWhite.setActive(placeWhite);
      btnLeftBlack.setActive(!placeWhite);

      btnLeftAdd.setActive(selected.kind == ToolKind::Piece);
      btnLeftMove.setActive(selected.kind == ToolKind::Move);

      // Draw left buttons
      btnLeftWhite.draw(rt, off);
      btnLeftBlack.draw(rt, off);
      btnLeftAdd.draw(rt, off);
      btnLeftMove.draw(rt, off);

      btnLeftClear.setAccent(true);
      btnLeftClear.draw(rt, off);
      btnLeftClear.setAccent(false);

      btnLeftReset.draw(rt, off);

      // Piece labels
      sf::Text labelW("White", *font, 12);
      labelW.setFillColor(theme->subtle);
      labelW.setPosition(ui::snap({rightRect.left + off.x + 10.f, rightRect.top + off.y + 32.f}));
      rt.draw(labelW);

      sf::Text labelB("Black", *font, 12);
      labelB.setFillColor(theme->subtle);
      labelB.setPosition(ui::snap({pieceBtns[6].r.left + off.x, pieceBtns[6].r.top + off.y - 18.f}));
      rt.draw(labelB);

      // Draw piece buttons + icons
      for (int i = 0; i < 12; ++i)
      {
        const char pc = pieceBtns[i].pc;
        const bool active = (selected.kind == ToolKind::Piece && selected.piece == pc);

        pieceBtns[i].bg.setActive(active);
        pieceBtns[i].bg.draw(rt, off);

        sf::Sprite spr = spriteForPiece(pc);
        if (spr.getTexture())
        {
          const auto r = pieceBtns[i].r;
          spr.setPosition(ui::snap({r.left + off.x + r.width * 0.5f,
                                    r.top + off.y + r.height * 0.5f + pieceYOffset * 0.25f}));
          rt.draw(spr);
        }
      }
    }

    void drawFenPanel(sf::RenderTarget &rt, sf::Vector2f off) const
    {
      // Panel
      sf::RectangleShape bg({fenRect.width, fenRect.height});
      bg.setPosition(ui::snap({fenRect.left + off.x, fenRect.top + off.y}));
      bg.setFillColor(ui::darken(theme->panel, 2));
      bg.setOutlineThickness(1.f);
      bg.setOutlineColor(ui::darken(theme->panel, 18));
      rt.draw(bg);

      // Update dynamic button labels/states
      btnTurnW.setActive(meta.sideToMove == 'w');
      btnTurnB.setActive(meta.sideToMove == 'b');

      // Castling: enabled if it’s already active OR structurally possible to enable.
      btnCastleK.setEnabled(meta.castleK || pb::hasCastleStructure(board, true, true));
      btnCastleQ.setEnabled(meta.castleQ || pb::hasCastleStructure(board, true, false));
      btnCastlek.setEnabled(meta.castlek || pb::hasCastleStructure(board, false, true));
      btnCastleq.setEnabled(meta.castleq || pb::hasCastleStructure(board, false, false));

      btnCastleK.setActive(meta.castleK);
      btnCastleQ.setActive(meta.castleQ);
      btnCastlek.setActive(meta.castlek);
      btnCastleq.setActive(meta.castleq);

      // EP label
      std::string epLbl;
      if (epSelecting)
        epLbl = "EP: select...";
      else if (meta.epTarget)
        epLbl = "EP: " + pb::epString(meta);
      else
        epLbl = "EP: -";
      btnEp.setText(epLbl, 12);
      btnEp.setActive(epSelecting || meta.epTarget.has_value());

      // Draw buttons
      btnTurnW.draw(rt, off);
      btnTurnB.draw(rt, off);

      btnCastleK.draw(rt, off);
      btnCastleQ.draw(rt, off);
      btnCastlek.draw(rt, off);
      btnCastleq.draw(rt, off);

      btnEp.draw(rt, off);

      btnShortcuts.setActive(showShortcuts);
      btnShortcuts.draw(rt, off);

      // FEN box
      const bool ok = pb::kingsOk(board);

      sf::RectangleShape fenBox({fenBoxRect.width, fenBoxRect.height});
      fenBox.setPosition(ui::snap({fenBoxRect.left + off.x, fenBoxRect.top + off.y}));
      fenBox.setFillColor(ui::darken(theme->panel, 6));
      fenBox.setOutlineThickness(2.f);
      fenBox.setOutlineColor(ok ? sf::Color(122, 205, 164, 220) : sf::Color(220, 70, 70, 220));
      rt.draw(fenBox);

      // Ellipsize cleanly (uses helper added to ui/style.hpp)
      const float maxW = fenBoxRect.width - 16.f;
      const unsigned fs = 13;
      std::string fenStr = fen();
      fenStr = ui::ellipsizeMiddle(*font, fs, fenStr, maxW);

      sf::Text fenText(fenStr, *font, fs);
      fenText.setFillColor(theme->text);
      auto b = fenText.getLocalBounds();
      fenText.setPosition(ui::snap({fenBoxRect.left + off.x + 8.f,
                                    fenBoxRect.top + off.y + (fenBoxRect.height - b.height) * 0.5f - b.top}));
      rt.draw(fenText);

      // Copy
      btnCopyFen.setEnabled(ok);
      btnCopyFen.setAccent(true);
      btnCopyFen.draw(rt, off);
      btnCopyFen.setAccent(false);
    }

    void drawBoard(sf::RenderTarget &rt, sf::Vector2f off, sf::Vector2f shake) const
    {
      sf::RectangleShape frame({boardRect.width, boardRect.height});
      frame.setPosition(ui::snap({boardRect.left + off.x + shake.x, boardRect.top + off.y}));
      frame.setFillColor(sf::Color::Transparent);
      frame.setOutlineThickness(1.f);
      frame.setOutlineColor(ui::darken(theme->panel, 18));
      rt.draw(frame);

      const bool previewing = (!dragging && selected.kind == ToolKind::Piece && hoverSquare.has_value());

      for (int y = 0; y < 8; ++y)
      {
        for (int x = 0; x < 8; ++x)
        {
          const bool dark = ((x + y) % 2) == 1;
          sf::Sprite sqSpr = dark ? sqBlack : sqWhite;
          sqSpr.setPosition(ui::snap({boardRect.left + off.x + shake.x + x * sq,
                                      boardRect.top + off.y + y * sq}));
          rt.draw(sqSpr);

          const char p = at(x, y);
          if (p != '.')
          {
            bool dimUnder = false;
            if (previewing && hoverSquare)
            {
              auto [hx, hy] = *hoverSquare;
              dimUnder = (hx == x && hy == y);
            }
            drawPiece(rt, off, shake, x, y, p, dimUnder);
          }
        }
      }

      // EP selection: show valid targets
      if (epSelecting)
      {
        const int y = (meta.sideToMove == 'w') ? 2 : 5;
        for (int x = 0; x < 8; ++x)
        {
          if (!pb::isValidEnPassantTarget(board, x, y, meta.sideToMove))
            continue;

          sf::RectangleShape o({sq, sq});
          o.setPosition(ui::snap({boardRect.left + off.x + shake.x + x * sq,
                                  boardRect.top + off.y + y * sq}));
          o.setFillColor(sf::Color(0, 0, 0, 0));
          o.setOutlineThickness(3.f);
          o.setOutlineColor(sf::Color(theme->accent.r, theme->accent.g, theme->accent.b, 175));
          rt.draw(o);
        }
      }

      // EP active square: highlight
      if (meta.epTarget)
      {
        auto [ex, ey] = *meta.epTarget;
        sf::RectangleShape o({sq, sq});
        o.setPosition(ui::snap({boardRect.left + off.x + shake.x + ex * sq,
                                boardRect.top + off.y + ey * sq}));
        o.setFillColor(sf::Color(0, 0, 0, 0));
        o.setOutlineThickness(3.f);
        o.setOutlineColor(sf::Color(122, 205, 164, 210));
        rt.draw(o);
      }

      // Hover square
      if (hoverSquare)
      {
        auto [hx, hy] = *hoverSquare;
        sf::RectangleShape h({sq, sq});
        h.setPosition(ui::snap({boardRect.left + off.x + shake.x + hx * sq,
                                boardRect.top + off.y + hy * sq}));
        h.setFillColor(sf::Color(255, 255, 255, 0));
        h.setOutlineThickness(2.f);
        h.setOutlineColor(sf::Color(255, 255, 255, 90));
        rt.draw(h);

        // Ghost preview in add mode
        if (!dragging && selected.kind == ToolKind::Piece)
        {
          sf::Sprite ghost = spriteForPiece(selected.piece);
          if (ghost.getTexture())
          {
            const bool illegal = wouldViolateKingUniqueness(hx, hy, selected.piece);

            sf::Sprite shadow = ghost;
            shadow.setColor(sf::Color(0, 0, 0, 120));
            shadow.setPosition(ui::snap({boardRect.left + off.x + shake.x + hx * sq + sq * 0.5f + 2.f,
                                         boardRect.top + off.y + hy * sq + sq * 0.5f + pieceYOffset + 3.f}));
            rt.draw(shadow);

            ghost.setColor(illegal ? sf::Color(255, 120, 120, 200) : sf::Color(255, 255, 255, 220));
            ghost.setPosition(ui::snap({boardRect.left + off.x + shake.x + hx * sq + sq * 0.5f,
                                        boardRect.top + off.y + hy * sq + sq * 0.5f + pieceYOffset}));
            rt.draw(ghost);
          }
        }
      }

      // Drag sprite
      if (dragging && dragPiece != '.')
      {
        sf::Sprite ghost = spriteForPiece(dragPiece);
        if (ghost.getTexture())
        {
          sf::Sprite shadow = ghost;
          shadow.setColor(sf::Color(0, 0, 0, 130));
          shadow.setPosition(ui::snap({mouseGlobal.x + 2.f, mouseGlobal.y + pieceYOffset + 3.f}));
          rt.draw(shadow);

          ghost.setColor(sf::Color(255, 255, 255, 230));
          ghost.setPosition(ui::snap({mouseGlobal.x, mouseGlobal.y + pieceYOffset}));
          rt.draw(ghost);
        }
      }
    }

    void drawPiece(sf::RenderTarget &rt, sf::Vector2f off, sf::Vector2f shake,
                   int x, int y, char p, bool dimUnder) const
    {
      sf::Sprite spr = spriteForPiece(p);
      if (!spr.getTexture())
        return;

      spr.setPosition(ui::snap({boardRect.left + off.x + shake.x + x * sq + sq * 0.5f,
                                boardRect.top + off.y + y * sq + sq * 0.5f + pieceYOffset}));

      if (dimUnder)
        spr.setColor(sf::Color(255, 255, 255, 85));

      rt.draw(spr);
    }

    void drawToast(sf::RenderTarget &rt, sf::Vector2f off) const
    {
      if (toastT <= 0.f || toastMsg.empty())
        return;

      const float a = std::clamp(toastT / toastDur, 0.f, 1.f);
      const float w = std::min(520.f, bottomRect.width * 0.75f);
      const float h = 36.f;

      sf::FloatRect r{
          fenRect.left + (fenRect.width - w) * 0.5f,
          fenRect.top - 44.f,
          w, h};

      sf::RectangleShape box({r.width, r.height});
      box.setPosition(ui::snap({r.left + off.x, r.top + off.y}));
      box.setFillColor(sf::Color(toastColor.r, toastColor.g, toastColor.b, sf::Uint8(160 * a)));
      box.setOutlineThickness(1.f);
      box.setOutlineColor(sf::Color(0, 0, 0, sf::Uint8(70 * a)));
      rt.draw(box);

      sf::Text t(toastMsg, *font, 12);
      t.setFillColor(sf::Color(255, 255, 255, sf::Uint8(255 * a)));
      t.setPosition(ui::snap({r.left + off.x + 10.f, r.top + off.y + 8.f}));
      rt.draw(t);
    }

    void drawError(sf::RenderTarget &rt, sf::Vector2f off) const
    {
      const float a = std::clamp(errT / errDur, 0.f, 1.f);
      const float w = std::min(520.f, boardRect.width * 0.92f);
      const float h = 44.f;

      sf::FloatRect r{boardRect.left + (boardRect.width - w) * 0.5f,
                      boardRect.top - 54.f,
                      w, h};

      sf::RectangleShape box({r.width, r.height});
      box.setPosition(ui::snap({r.left + off.x, r.top + off.y}));
      box.setFillColor(sf::Color(200, 70, 70, sf::Uint8(180 * a)));
      box.setOutlineThickness(1.f);
      box.setOutlineColor(sf::Color(0, 0, 0, sf::Uint8(80 * a)));
      rt.draw(box);

      sf::Text t(errMsg, *font, 12);
      t.setFillColor(sf::Color(255, 255, 255, sf::Uint8(255 * a)));
      t.setPosition(ui::snap({r.left + off.x + 10.f, r.top + off.y + 6.f}));
      rt.draw(t);
    }

    void drawShortcutsPanel(sf::RenderTarget &rt, sf::Vector2f off) const
    {
      if (shortcutsRect.width <= 0.f || shortcutsRect.height <= 0.f)
        return;

      sf::RectangleShape bg({shortcutsRect.width, shortcutsRect.height});
      bg.setPosition(ui::snap({shortcutsRect.left + off.x, shortcutsRect.top + off.y}));
      bg.setFillColor(ui::darken(theme->panel, 6));
      bg.setOutlineThickness(1.f);
      bg.setOutlineColor(ui::darken(theme->panel, 18));
      rt.draw(bg);

      sf::Text title("Shortcuts", *font, 12);
      title.setFillColor(theme->subtle);
      title.setPosition(ui::snap({shortcutsRect.left + off.x + 10.f, shortcutsRect.top + off.y + 8.f}));
      rt.draw(title);

      struct Item
      {
        const char *k;
        const char *v;
      };
      static constexpr std::array<Item, 10> items{{
          {"A", "Add"},
          {"M", "Move"},
          {"Tab", "Toggle add color"},
          {"1-6", "Pieces"},
          {"Shift+1-6", "Force black"},
          {"Space", "Toggle turn"},
          {"RMB", "Delete square"},
          {"H", "Toggle shortcuts"},
          {"C", "Clear"},
          {"R", "Reset"},
      }};

      float x = shortcutsRect.left + off.x + 10.f;
      float y = shortcutsRect.top + off.y + 28.f;

      const float lineH = 18.f;
      const float maxW = shortcutsRect.width - 20.f;

      for (const auto &it : items)
      {
        sf::Text k(it.k, *font, 11);
        sf::Text v(it.v, *font, 11);
        k.setFillColor(theme->text);
        v.setFillColor(theme->subtle);

        auto kb = k.getLocalBounds();
        auto vb = v.getLocalBounds();

        const float itemW = (kb.width + 18.f + vb.width + 18.f);
        if (x - (shortcutsRect.left + off.x + 10.f) + itemW > maxW)
        {
          x = shortcutsRect.left + off.x + 10.f;
          y += lineH;
        }

        k.setPosition(ui::snap({x, y}));
        rt.draw(k);
        v.setPosition(ui::snap({x + kb.width + 18.f, y}));
        rt.draw(v);

        x += itemW;
        if (y > shortcutsRect.top + off.y + shortcutsRect.height - 24.f)
          break;
      }
    }
  };

  std::string PositionBuilder::Impl::s_lastFen{};

  // ---------------- PositionBuilder public wrapper ----------------

  PositionBuilder::PositionBuilder()
      : m(std::make_unique<Impl>())
  {
    m->applyThemeFont();
    m->hookCallbacks();
  }

  PositionBuilder::~PositionBuilder() = default;

  void PositionBuilder::onOpen()
  {
    if (!Impl::s_lastFen.empty())
      m->setFromFen(Impl::s_lastFen, false);
    else
      m->resetToStart(false);

    m->enterAddDefault();
  }

  void PositionBuilder::setTheme(const ui::Theme *t)
  {
    m->theme = t;
    m->texReady = false;
    m->applyThemeFont();
    m->rebuildGeometry();
  }

  void PositionBuilder::setFont(const sf::Font *f)
  {
    m->font = f;
    m->applyThemeFont();
    m->rebuildGeometry();
  }

  void PositionBuilder::setBounds(sf::FloatRect r)
  {
    m->bounds = r;
    m->rebuildGeometry();
  }

  void PositionBuilder::clear(bool remember) { m->clear(remember); }
  void PositionBuilder::resetToStart(bool remember) { m->resetToStart(remember); }

  std::string PositionBuilder::fen() const { return m->fen(); }
  std::string PositionBuilder::fenForUse() const { return m->fenForUse(); }

  bool PositionBuilder::kingsOk() const { return pb::kingsOk(m->board); }
  int PositionBuilder::whiteKings() const { return m->whiteK; }
  int PositionBuilder::blackKings() const { return m->blackK; }

  void PositionBuilder::updateHover(sf::Vector2f mouse, sf::Vector2f off) { m->updateHover(mouse, off); }

  bool PositionBuilder::handleEvent(const sf::Event &e, sf::Vector2f mouse, sf::Vector2f off)
  {
    return m->handleEvent(e, mouse, off);
  }

  void PositionBuilder::draw(sf::RenderTarget &rt, sf::Vector2f off) const
  {
    m->draw(rt, off);
  }

} // namespace lilia::view
