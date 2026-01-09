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
    sf::FloatRect fenBoxRect{};

    // New: explicit layout zones in left panel for clean/symmetric alignment
    sf::FloatRect toolSegRect{};     // segmented tool selector container
    sf::FloatRect addColorRowRect{}; // "Add color" row baseline
    sf::FloatRect hotkeysRect{};     // always-visible hotkeys field

    float sq{50.f};
    float pieceYOffset{0.f};

    Board board{};
    FenMeta meta{};

    int whiteK{0};
    int blackK{0};

    enum class ToolKind
    {
      Move,
      Add,
      Delete
    };

    struct ToolSelection
    {
      ToolKind kind{ToolKind::Move};
      char piece{'.'};

      static ToolSelection addPiece(char p) { return {ToolKind::Add, p}; }
      static ToolSelection move() { return {ToolKind::Move, '.'}; }
      static ToolSelection del() { return {ToolKind::Delete, '.'}; }
    };

    // Start state must be Move.
    ToolSelection selected = ToolSelection::move();
    bool placeWhite{true};
    char lastAddLower{'p'};

    // Drag
    bool dragging{false};
    bool dragMouseDown{false}; // true while tied to pressed LMB; false = "piece in hand" carry mode
    char dragPiece{'.'};
    std::optional<std::pair<int, int>> dragFrom{};

    // Hover / input
    sf::Vector2f mouseGlobal{};
    sf::Vector2f offset{};
    std::optional<std::pair<int, int>> hoverSquare{};

    // Paint (hold LMB in Add/Delete and sweep across board)
    bool paintDown{false};
    std::optional<std::pair<int, int>> lastPaintSq{};

    // Palette: click/drag/long-press behavior
    bool palettePress{false};
    int paletteIdx{-1};
    sf::Vector2f palettePressLocal{};
    sf::Clock paletteClock{};
    bool paletteDragStarted{false};
    bool paletteOneShot{false};        // one-off placement from palette while in Move
    ToolSelection paletteReturnTool{}; // tool to restore after one-shot placement
    static constexpr float kPaletteLongPressS = 0.28f;
    static constexpr float kPaletteDragStartPx = 7.f;

    // Tool selector animation (very noticeable, sliding state highlight)
    mutable float toolSelPos{0.f}; // 0=Move, 1=Add, 2=Delete (animated)

    // EP selection mode
    bool epSelecting{false};

    struct EpHoldState
    {
      ToolSelection selected{};
      bool placeWhite{true};
      char lastAddLower{'p'};

      bool dragging{false};
      char dragPiece{'.'};
      std::optional<std::pair<int, int>> dragFrom{};

      std::optional<std::pair<int, int>> epBefore{};
    };

    std::optional<EpHoldState> epHold{};

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
    mutable ui::Button btnLeftMove, btnLeftAdd, btnLeftDelete;
    mutable ui::Button btnLeftClear, btnLeftReset;

    mutable ui::Button btnTurnW, btnTurnB;
    mutable ui::Button btnCastleK, btnCastleQ, btnCastlek, btnCastleq;
    mutable ui::Button btnEp;
    mutable ui::Button btnCopyFen;

    struct PieceBtn
    {
      ui::Button bg;
      char pc{'.'};
      sf::FloatRect r{};
    };
    mutable std::array<PieceBtn, 12> pieceBtns{};

    // ---------- construction / defaults ----------
    Impl()
    {
      pb::clearBoard(board);
      resetToStart(false);
      if (!s_lastFen.empty())
        setFromFen(s_lastFen, false);

      // Start on Move (do not auto-enter Add)
      selected = ToolSelection::move();
      lastAddLower = 'p';
      toolSelPos = 0.f;
    }

    static float toolIndex(ToolKind k)
    {
      switch (k)
      {
      case ToolKind::Move:
        return 0.f;
      case ToolKind::Add:
        return 1.f;
      case ToolKind::Delete:
        return 2.f;
      }
      return 0.f;
    }

    void applyThemeFont()
    {
      auto apply = [&](ui::Button &b)
      {
        b.setTheme(theme);
        if (font)
          b.setFont(*font);
      };

      apply(btnLeftMove);
      apply(btnLeftAdd);
      apply(btnLeftDelete);
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

      for (auto &pbx : pieceBtns)
        apply(pbx.bg);

      // Tool selector: make it look like a distinct "state control" (uppercase + slightly larger)
      btnLeftMove.setText("MOVE", 10);
      btnLeftAdd.setText("ADD", 10);
      btnLeftDelete.setText("DELETE", 10);

      btnLeftClear.setText("Clear", 12);
      btnLeftReset.setText("Reset", 12);

      btnTurnW.setText("White", 12);
      btnTurnB.setText("Black", 12);

      btnCastleK.setText("K", 12);
      btnCastleQ.setText("Q", 12);
      btnCastlek.setText("k", 12);
      btnCastleq.setText("q", 12);

      btnCopyFen.setText("Copy", 12);

      // piece backgrounds are icon-only
      for (auto &pbx : pieceBtns)
        pbx.bg.setText("", 12);
    }

    void hookCallbacks()
    {
      // Left panel tool states
      btnLeftMove.setOnClick([&]
                             {
        cancelDragToOrigin(false);
        selected = ToolSelection::move();
        rememberCurrentIfStable(); });

      btnLeftAdd.setOnClick([&]
                            {
        cancelDragToOrigin(false);
        if (selected.kind != ToolKind::Add)
        {
          selected.kind = ToolKind::Add;
          if (selected.piece == '.')
            selected.piece = applyColorToPieceType(lastAddLower, placeWhite);
        }
        rememberCurrentIfStable(); });

      btnLeftDelete.setOnClick([&]
                               {
        cancelDragToOrigin(false);
        selected = ToolSelection::del();
        rememberCurrentIfStable(); });

      btnLeftClear.setOnClick([&]
                              { clear(true); });
      btnLeftReset.setOnClick([&]
                              { resetToStart(true); });

      // Turn
      btnTurnW.setOnClick([&]
                          { meta.sideToMove = 'w'; sanitizeMeta(); rememberCurrentIfStable(); });
      btnTurnB.setOnClick([&]
                          { meta.sideToMove = 'b'; sanitizeMeta(); rememberCurrentIfStable(); });

      // Castling toggles with structural validation.
      btnCastleK.setOnClick([&]
                            { toggleCastle(true, true); });
      btnCastleQ.setOnClick([&]
                            { toggleCastle(true, false); });
      btnCastlek.setOnClick([&]
                            { toggleCastle(false, true); });
      btnCastleq.setOnClick([&]
                            { toggleCastle(false, false); });

      // EP select tool: modal "quick event" that holds state (including a piece in hand)
      btnEp.setOnClick([&]
                       {
        if (epSelecting)
        {
          cancelEpSelection();
          return;
        }
        beginEpSelection(); });

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

      // Piece buttons are now handled with custom interactions (quick click, drag, long press).
      // Keep their onClick empty to avoid conflicting behavior.
      for (int i = 0; i < 12; ++i)
      {
        pieceBtns[i].pc = pieceCharFromIndex(i);
        pieceBtns[i].bg.setOnClick([&] {});
      }
    }

    // ---------- EP selection lifecycle ----------
    void beginEpSelection()
    {
      EpHoldState h;
      h.selected = selected;
      h.placeWhite = placeWhite;
      h.lastAddLower = lastAddLower;

      h.dragging = dragging;
      h.dragPiece = dragPiece;
      h.dragFrom = dragFrom;

      h.epBefore = meta.epTarget;

      epHold = h;
      epSelecting = true;

      selected = ToolSelection::move();

      dragging = false;
      dragMouseDown = false;

      showToast("Select an en passant target square.\nClick anywhere else to cancel.", sf::Color(122, 205, 164));
    }

    void restoreHeldAfterEpCancelOrCommit()
    {
      if (!epHold)
        return;

      selected = epHold->selected;
      placeWhite = epHold->placeWhite;
      lastAddLower = epHold->lastAddLower;

      if (epHold->dragging && epHold->dragPiece != '.' && epHold->dragFrom)
      {
        dragging = true;
        dragMouseDown = false; // carry mode
        dragPiece = epHold->dragPiece;
        dragFrom = epHold->dragFrom;

        auto [ox, oy] = *dragFrom;
        if (pb::inBounds(ox, oy) && board[oy][ox] == dragPiece)
          board[oy][ox] = '.';
      }
      else
      {
        dragging = false;
        dragMouseDown = false;
        dragPiece = '.';
        dragFrom.reset();
      }

      epHold.reset();
      refreshKings();
      sanitizeMeta();
    }

    void cancelEpSelection()
    {
      if (epHold)
        meta.epTarget = epHold->epBefore;

      epSelecting = false;
      restoreHeldAfterEpCancelOrCommit();
    }

    void commitEpSelection(int x, int y)
    {
      meta.epTarget = std::make_pair(x, y);
      epSelecting = false;
      restoreHeldAfterEpCancelOrCommit();
      rememberCurrentIfStable();
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

    void cycleTool()
    {
      cancelDragToOrigin(false);

      if (selected.kind == ToolKind::Move)
      {
        selected.kind = ToolKind::Add;
        selected.piece = applyColorToPieceType(lastAddLower, placeWhite);
      }
      else if (selected.kind == ToolKind::Add)
      {
        selected = ToolSelection::del();
      }
      else
      {
        selected = ToolSelection::move();
      }
    }

    void togglePlaceColor()
    {
      placeWhite = !placeWhite;
      if (selected.kind == ToolKind::Add)
        selected.piece = applyColorToPieceType(lastAddLower, placeWhite);

      showToast(placeWhite ? "Add color: White" : "Add color: Black", sf::Color(122, 205, 164));
    }

    void cancelDragToOrigin(bool remember)
    {
      if (!dragging)
        return;

      dragging = false;
      dragMouseDown = false;

      if (dragFrom && dragPiece != '.')
      {
        auto [ox, oy] = *dragFrom;
        board[oy][ox] = dragPiece;
      }

      dragPiece = '.';
      dragFrom.reset();

      refreshKings();
      sanitizeMeta();
      if (remember)
        rememberCurrentIfStable();
    }

    void cancelPaletteCarryOrDrag()
    {
      if (!dragging)
        return;

      dragging = false;
      dragMouseDown = false;
      dragPiece = '.';
      dragFrom.reset();

      // If this was a one-shot palette action, restore the tool immediately.
      if (paletteOneShot)
        selected = paletteReturnTool;

      paletteOneShot = false;
    }

    void refreshKings()
    {
      pb::countKings(board, whiteK, blackK);
    }

    void sanitizeMeta()
    {
      pb::sanitizeMeta(board, meta);
    }

    void rememberCurrent()
    {
      sanitizeMeta();
      s_lastFen = fen();
    }

    void rememberCurrentIfStable()
    {
      if (dragging)
        return;
      rememberCurrent();
    }

    // ---------- FEN ----------
    std::string fen() const
    {
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
        rememberCurrentIfStable();

      return true;
    }

    // ---------- EP selection helpers ----------
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
        rememberCurrentIfStable();
        return;
      }

      if (!pb::hasCastleStructure(board, white, kingSide))
      {
        invalidAction("Castling right is not valid.\nKing/Rook must be on start squares.");
        return;
      }

      flag = true;
      rememberCurrentIfStable();
    }

    // ---------- clear/reset ----------
    void clear(bool remember)
    {
      pb::clearBoard(board);

      dragging = false;
      dragMouseDown = false;
      dragPiece = '.';
      dragFrom.reset();

      paintDown = false;
      lastPaintSq.reset();

      palettePress = false;
      paletteIdx = -1;
      paletteDragStarted = false;
      paletteOneShot = false;

      meta.sideToMove = 'w';
      meta.castleK = meta.castleQ = meta.castlek = meta.castleq = false;
      meta.epTarget.reset();
      meta.halfmove = 0;
      meta.fullmove = 1;

      epSelecting = false;
      epHold.reset();

      refreshKings();
      if (remember)
        rememberCurrentIfStable();
    }

    void resetToStart(bool remember)
    {
      clear(false);
      setFromFen(core::START_FEN, false);

      meta.sideToMove = 'w';
      meta.castleK = meta.castleQ = meta.castlek = meta.castleq = true;
      meta.epTarget.reset();
      meta.halfmove = 0;
      meta.fullmove = 1;

      epSelecting = false;
      epHold.reset();

      sanitizeMeta();
      if (remember)
        rememberCurrentIfStable();
    }

    void setFromFen(const std::string &fenStr, bool remember)
    {
      pb::setFromFen(board, meta, fenStr);

      epSelecting = false;
      epHold.reset();

      dragging = false;
      dragMouseDown = false;
      dragPiece = '.';
      dragFrom.reset();

      paintDown = false;
      lastPaintSq.reset();

      palettePress = false;
      paletteIdx = -1;
      paletteDragStarted = false;
      paletteOneShot = false;

      refreshKings();
      sanitizeMeta();
      if (remember)
        rememberCurrentIfStable();
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

      // Increased overall usable area and board presence:
      // tighter padding/gaps + slightly narrower side panels + higher minimum board size.
      const float pad = 12.f;
      const float gap = 12.f;
      const float topInset = 12.f;

      const float fenH = 92.f;
      const float bottomH = fenH;

      const float sideW = std::clamp(bounds.width * 0.18f, 150.f, 220.f);

      float availW = bounds.width - pad * 2.f - sideW * 2.f - gap * 2.f;
      float availHTotal = bounds.height - pad * 2.f - topInset;

      float boardMaxH = std::max(320.f, availHTotal - bottomH);
      float boardSize = std::min(availW, boardMaxH);
      boardSize = std::max(320.f, boardSize);

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

      // Left panel: Segmented tool selector at the top (highly visible)
      {
        const float x = leftRect.left + 10.f;
        const float w = leftRect.width - 20.f;
        const float h = 36.f;

        const float y = leftRect.top + 52.f;

        toolSegRect = {x, y, w, h};

        const float segGap = 6.f;
        const float segW = std::floor((w - segGap * 2.f) / 3.f);

        btnLeftMove.setBounds({x + 0.f * (segW + segGap), y, segW, h});
        btnLeftAdd.setBounds({x + 1.f * (segW + segGap), y, segW, h});
        btnLeftDelete.setBounds({x + 2.f * (segW + segGap), y, segW, h});

        // Add color row + always-visible hotkeys field below
        const float yColor = y + h + 16.f;
        addColorRowRect = {x, yColor, w, 18.f};

        const float hkY = yColor + 22.f;
        const float hkH = 98.f;
        hotkeysRect = {x, hkY, w, hkH};

        // Clear / Reset anchored at the bottom of the left panel (kept symmetrical)
        const float btnH = 34.f;
        const float g = 10.f;
        const float bottomPad = 10.f;
        const float block = btnH * 2.f + g;
        const float yBottom = leftRect.top + leftRect.height - bottomPad - block;

        btnLeftClear.setBounds({x, yBottom, w, btnH});
        btnLeftReset.setBounds({x, yBottom + btnH + g, w, btnH});
      }

      // Right panel: piece grid layout (no hotkeys button anymore; grid is vertically centered cleaner)
      {
        const float padR = 10.f;
        const float left = rightRect.left + padR;
        const float w = rightRect.width - padR * 2.f;

        const float titleZone = 54.f;
        const float top = rightRect.top + titleZone;

        const float cellGap = 10.f;
        const float sep = 18.f;
        const float cell = std::clamp(std::floor((w - cellGap * 2.f) / 3.f), 46.f, 84.f);

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
        sf::FloatRect row1 = consumeRow(inner, 32.f, 8.f);
        sf::FloatRect row2 = consumeRow(inner, 34.f, 0.f);

        const float btnH = 30.f;
        const float y = row1.top + 1.f;
        float x = row1.left;

        // Turn segmented
        const float turnW = 84.f;
        btnTurnW.setBounds({x, y, turnW, btnH});
        btnTurnB.setBounds({x + turnW, y, turnW, btnH});
        x += (turnW * 2.f + 12.f);

        // Castling toggles
        const float small = 32.f;
        btnCastleK.setBounds({x, y, small, btnH});
        btnCastleQ.setBounds({x + (small + 6.f), y, small, btnH});
        btnCastlek.setBounds({x + 2.f * (small + 6.f), y, small, btnH});
        btnCastleq.setBounds({x + 3.f * (small + 6.f), y, small, btnH});
        x += (4.f * small + 3.f * 6.f + 12.f);

        // EP fixed size
        const float epW = 150.f;
        btnEp.setBounds({x, y, epW, btnH});

        // Row2: fen box + copy
        const float copyW = 110.f;
        btnCopyFen.setBounds({row2.left + row2.width - copyW, row2.top + 1.f, copyW, 32.f});
        fenBoxRect = {row2.left, row2.top + 1.f, row2.width - copyW - 10.f, 32.f};
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

      auto hov = [&](ui::Button &b)
      { b.updateHover(mouse, off); };

      hov(btnLeftMove);
      hov(btnLeftAdd);
      hov(btnLeftDelete);
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

      for (auto &pbx : pieceBtns)
        pbx.bg.updateHover(mouse, off);
    }

    std::optional<int> paletteIndexAt(sf::Vector2f local) const
    {
      for (int i = 0; i < 12; ++i)
        if (pieceBtns[i].r.contains(local))
          return i;
      return std::nullopt;
    }

    void enterAddModeForPiece(char chosen)
    {
      placeWhite = std::isupper(static_cast<unsigned char>(chosen));
      lastAddLower = char(std::tolower(static_cast<unsigned char>(chosen)));
      selected = ToolSelection::addPiece(chosen);
      rememberCurrentIfStable();
    }

    void beginOneShotCarryFromPalette(char chosen)
    {
      // One-shot add: pick up a piece (no tool switch), place once, then return to Move.
      cancelDragToOrigin(false);

      paletteOneShot = true;
      paletteReturnTool = ToolSelection::move();

      selected = ToolSelection::move();

      dragging = true;
      dragMouseDown = false; // carry mode (click to place)
      dragPiece = chosen;
      dragFrom.reset();

      showToast("Place piece: click a square (Right-click cancels).", sf::Color(122, 205, 164));
    }

    void beginPaletteDragToBoard(char chosen)
    {
      cancelDragToOrigin(false);

      paletteOneShot = true;
      paletteReturnTool = ToolSelection::move();
      selected = ToolSelection::move();

      dragging = true;
      dragMouseDown = true; // tied to LMB
      dragPiece = chosen;
      dragFrom.reset();
    }

    bool handlePaletteInteraction(const sf::Event &e, sf::Vector2f mouse, sf::Vector2f off)
    {
      const sf::Vector2f local{mouse.x - off.x, mouse.y - off.y};

      // Cancel one-shot carry quickly via RMB (fluid UX)
      if (e.type == sf::Event::MouseButtonPressed &&
          e.mouseButton.button == sf::Mouse::Right &&
          dragging && !dragMouseDown && paletteOneShot)
      {
        cancelPaletteCarryOrDrag();
        return true;
      }

      if (e.type == sf::Event::MouseButtonPressed && e.mouseButton.button == sf::Mouse::Left)
      {
        auto idx = paletteIndexAt(local);
        if (!idx)
          return false;

        palettePress = true;
        paletteIdx = *idx;
        palettePressLocal = local;
        paletteClock.restart();
        paletteDragStarted = false;

        // Consume so the board does not start a Move pick-up beneath.
        return true;
      }

      if (e.type == sf::Event::MouseMoved && palettePress && paletteIdx >= 0 && paletteIdx < 12)
      {
        const float t = paletteClock.getElapsedTime().asSeconds();
        const sf::Vector2f d = local - palettePressLocal;
        const float dist = std::sqrt(d.x * d.x + d.y * d.y);

        const char chosen = pieceBtns[paletteIdx].pc;

        // Long press (without drag): switch to Add mode for that piece.
        if (!paletteDragStarted && t >= kPaletteLongPressS && dist < kPaletteDragStartPx)
        {
          palettePress = false;
          paletteIdx = -1;
          paletteDragStarted = false;

          enterAddModeForPiece(chosen);
          showToast("Add mode: selected piece.", sf::Color(122, 205, 164));
          return true;
        }

        // Drag start: immediate palette-to-board placement flow (Move-state one-shot)
        if (!paletteDragStarted && dist >= kPaletteDragStartPx)
        {
          paletteDragStarted = true;

          // This interaction is explicitly requested for Move: drag from palette => place once => return to Move.
          // If user is not in Move, we still allow drag placement but do not force tool changes.
          beginPaletteDragToBoard(chosen);
          return true;
        }

        return true;
      }

      if (e.type == sf::Event::MouseButtonReleased && e.mouseButton.button == sf::Mouse::Left &&
          palettePress && paletteIdx >= 0 && paletteIdx < 12)
      {
        const float t = paletteClock.getElapsedTime().asSeconds();
        const char chosen = pieceBtns[paletteIdx].pc;

        // Release after palette drag: perform drop now.
        if (paletteDragStarted && dragging && dragMouseDown && dragPiece == chosen)
        {
          palettePress = false;
          paletteIdx = -1;
          paletteDragStarted = false;

          dragging = false;
          dragMouseDown = false;

          auto sqr = squareFromMouse(local);
          if (sqr)
          {
            auto [tx, ty] = *sqr;
            if (!trySet(tx, ty, chosen, true))
            {
              invalidAction("Invalid drop.\nKings must be unique per color.");
            }
          }

          dragPiece = '.';
          dragFrom.reset();

          // Always return to Move after a palette drag placement (per requirement).
          selected = ToolSelection::move();
          paletteOneShot = false;

          refreshKings();
          sanitizeMeta();
          return true;
        }

        // Short click (no drag): in Move => one-shot carry, in non-Move => normal Add selection
        palettePress = false;
        paletteIdx = -1;
        paletteDragStarted = false;

        if (t < kPaletteLongPressS)
        {
          if (selected.kind == ToolKind::Move)
          {
            beginOneShotCarryFromPalette(chosen);
            return true;
          }

          // If not in Move, keep existing intuition: choose piece and go to Add mode.
          enterAddModeForPiece(chosen);
          return true;
        }

        // If it was a "hold" but didn't satisfy the long-press condition earlier, still treat as long press.
        enterAddModeForPiece(chosen);
        showToast("Add mode: selected piece.", sf::Color(122, 205, 164));
        return true;
      }

      // If palette press is active and user releases outside: reset the press state.
      if (e.type == sf::Event::MouseButtonReleased && e.mouseButton.button == sf::Mouse::Left &&
          palettePress)
      {
        palettePress = false;
        paletteIdx = -1;
        paletteDragStarted = false;
        return false;
      }

      return false;
    }

    bool handleEvent(const sf::Event &e, sf::Vector2f mouse, sf::Vector2f off)
    {
      if (!theme || !font)
        return false;

      mouseGlobal = mouse;
      offset = off;

      const sf::Vector2f local{mouse.x - off.x, mouse.y - off.y};

      // EP selection is modal: it consumes the next click anywhere.
      if (epSelecting)
      {
        if (e.type == sf::Event::KeyPressed && e.key.code == sf::Keyboard::Escape)
        {
          cancelEpSelection();
          return true;
        }

        if (e.type == sf::Event::MouseButtonPressed &&
            (e.mouseButton.button == sf::Mouse::Left || e.mouseButton.button == sf::Mouse::Right))
        {
          if (e.mouseButton.button == sf::Mouse::Right)
          {
            cancelEpSelection();
            return true;
          }

          auto sqr = squareFromMouse(local);
          if (sqr)
          {
            auto [x, y] = *sqr;
            if (pb::isValidEnPassantTarget(board, x, y, meta.sideToMove))
            {
              commitEpSelection(x, y);
              return true;
            }
          }

          cancelEpSelection();
          return true;
        }

        return true;
      }

      // Keyboard
      if (e.type == sf::Event::KeyPressed)
      {
        if (e.key.code == sf::Keyboard::Tab)
        {
          cycleTool();
          return true;
        }

        if (e.key.code == sf::Keyboard::T)
        {
          togglePlaceColor();
          rememberCurrentIfStable();
          return true;
        }

        if (e.key.code == sf::Keyboard::Space)
        {
          meta.sideToMove = (meta.sideToMove == 'w') ? 'b' : 'w';
          sanitizeMeta();
          rememberCurrentIfStable();
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
          selected = ToolSelection::addPiece(applyColorToPieceType(placed, placeWhite));
          rememberCurrentIfStable();
          return true;
        }
      }

      // Palette interaction (before buttons/board so it feels responsive and never conflicts)
      if (handlePaletteInteraction(e, mouse, off))
        return true;

      // Route mouse events to buttons first (excluding piece palette)
      auto routeButtons = [&]() -> bool
      {
        auto h = [&](ui::Button &b) -> bool
        { return b.handleEvent(e, mouse, off); };

        if (h(btnCopyFen))
          return true;

        if (h(btnTurnW) || h(btnTurnB))
          return true;
        if (h(btnCastleK) || h(btnCastleQ) || h(btnCastlek) || h(btnCastleq))
          return true;
        if (h(btnEp))
          return true;

        if (h(btnLeftMove) || h(btnLeftAdd) || h(btnLeftDelete))
          return true;
        if (h(btnLeftClear) || h(btnLeftReset))
          return true;

        return false;
      };

      if (routeButtons())
      {
        // If a button consumes the mouse release while a drag is in progress,
        // convert it into "piece in hand" carry mode.
        if (e.type == sf::Event::MouseButtonReleased &&
            e.mouseButton.button == sf::Mouse::Left &&
            dragging && dragMouseDown)
        {
          dragMouseDown = false;
        }

        // Stop painting if UI consumed the release.
        if (e.type == sf::Event::MouseButtonReleased && e.mouseButton.button == sf::Mouse::Left)
        {
          paintDown = false;
          lastPaintSq.reset();
        }

        return true;
      }

      // Paint sweep in Add/Delete: apply on move while LMB held
      if (e.type == sf::Event::MouseMoved && paintDown && !dragging &&
          (selected.kind == ToolKind::Add || selected.kind == ToolKind::Delete))
      {
        auto sqr = squareFromMouse(local);
        if (!sqr)
          return true;

        if (!lastPaintSq || *lastPaintSq != *sqr)
        {
          auto [x, y] = *sqr;

          if (selected.kind == ToolKind::Delete)
          {
            if (at(x, y) != '.')
              trySet(x, y, '.', true);
          }
          else
          {
            if (!trySet(x, y, selected.piece, true))
              invalidAction("Kings must be unique per color.\nUse Move to reposition an existing king.");
          }

          lastPaintSq = sqr;
        }

        return true;
      }

      // If we are carrying a piece (dragging==true but mouse is not held), then a click places it.
      if (dragging && !dragMouseDown &&
          e.type == sf::Event::MouseButtonPressed &&
          e.mouseButton.button == sf::Mouse::Left)
      {
        auto sqr = squareFromMouse(local);
        if (sqr)
        {
          dragging = false;
          auto [tx, ty] = *sqr;

          if (!trySet(tx, ty, dragPiece, true))
          {
            invalidAction("Invalid drop.\nKings must be unique per color.");
            if (dragFrom)
            {
              auto [ox, oy] = *dragFrom;
              set(ox, oy, dragPiece);
              rememberCurrentIfStable();
            }
          }

          dragPiece = '.';
          dragFrom.reset();
          refreshKings();
          sanitizeMeta();

          // One-shot palette placement: always return to Move.
          if (paletteOneShot)
          {
            selected = paletteReturnTool;
            paletteOneShot = false;
          }

          return true;
        }

        // Click outside board: cancel carry (back to origin if any; otherwise discard)
        if (paletteOneShot)
        {
          cancelPaletteCarryOrDrag();
          return true;
        }

        cancelDragToOrigin(true);
        return true;
      }

      // Board LMB interactions
      if (e.type == sf::Event::MouseButtonPressed && e.mouseButton.button == sf::Mouse::Left)
      {
        auto sqr = squareFromMouse(local);
        if (!sqr)
          return false;

        auto [x, y] = *sqr;

        // Delete tool (press starts paint)
        if (selected.kind == ToolKind::Delete)
        {
          paintDown = true;
          lastPaintSq = sqr;

          if (at(x, y) != '.')
            trySet(x, y, '.', true);
          return true;
        }

        // Add tool (press starts paint)
        if (selected.kind == ToolKind::Add)
        {
          paintDown = true;
          lastPaintSq = sqr;

          if (!trySet(x, y, selected.piece, true))
            invalidAction("Kings must be unique per color.\nUse Move to reposition an existing king.");
          return true;
        }

        // Move tool: pick up piece
        const char p = at(x, y);
        if (p != '.')
        {
          dragging = true;
          dragMouseDown = true;
          dragPiece = p;
          dragFrom = std::make_pair(x, y);

          set(x, y, '.');
          refreshKings();
          sanitizeMeta();
          return true;
        }

        return true;
      }

      if (e.type == sf::Event::MouseButtonReleased && e.mouseButton.button == sf::Mouse::Left)
      {
        paintDown = false;
        lastPaintSq.reset();

        // Only finalize a drag on release if this drag was tied to a held mouse press.
        if (!dragging || !dragMouseDown)
          return false;

        dragging = false;
        dragMouseDown = false;

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
              rememberCurrentIfStable();
            }
          }
        }
        else if (dragFrom)
        {
          auto [ox, oy] = *dragFrom;
          set(ox, oy, dragPiece);
          rememberCurrentIfStable();
        }

        dragPiece = '.';
        dragFrom.reset();
        refreshKings();
        sanitizeMeta();
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

      // Smooth animated tool highlight position (very noticeable)
      {
        const float target = toolIndex(selected.kind);
        const float k = std::min(1.f, dt * 14.f);
        toolSelPos = toolSelPos + (target - toolSelPos) * k;
      }

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

      if (epSelecting)
        drawEpSelectionOverlay(rt, off, shake);

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

    void drawToolSegmentedControl(sf::RenderTarget &rt, sf::Vector2f off) const
    {
      // Background track
      sf::RectangleShape track({toolSegRect.width, toolSegRect.height});
      track.setPosition(ui::snap({toolSegRect.left + off.x, toolSegRect.top + off.y}));
      track.setFillColor(ui::darken(theme->panel, 8));
      track.setOutlineThickness(1.f);
      track.setOutlineColor(ui::darken(theme->panel, 20));
      rt.draw(track);

      // Sliding active indicator (the "state" look)
      const float segGap = 6.f;
      const float segW = std::floor((toolSegRect.width - segGap * 2.f) / 3.f);
      const float x0 = toolSegRect.left;
      const float y0 = toolSegRect.top;

      const float indicatorX = x0 + toolSelPos * (segW + segGap);
      sf::RectangleShape active({segW, toolSegRect.height});
      active.setPosition(ui::snap({indicatorX + off.x, y0 + off.y}));
      active.setFillColor(sf::Color(theme->accent.r, theme->accent.g, theme->accent.b, 46));
      active.setOutlineThickness(2.f);
      active.setOutlineColor(sf::Color(theme->accent.r, theme->accent.g, theme->accent.b, 200));
      rt.draw(active);

      // Subtle state dots (extra visual affordance)
      auto dotAt = [&](int i)
      {
        const float cx = x0 + i * (segW + segGap) + segW * 0.5f;
        const float cy = y0 + toolSegRect.height - 7.f;

        sf::CircleShape c(3.2f);
        c.setPosition(ui::snap({cx + off.x - 3.2f, cy + off.y - 3.2f}));
        if (int(std::round(toolIndex(selected.kind))) == i)
          c.setFillColor(sf::Color(theme->accent.r, theme->accent.g, theme->accent.b, 220));
        else
          c.setFillColor(sf::Color(255, 255, 255, 70));
        rt.draw(c);
      };

      dotAt(0);
      dotAt(1);
      dotAt(2);
    }

    void drawHotkeysField(sf::RenderTarget &rt, sf::Vector2f off) const
    {
      sf::RectangleShape box({hotkeysRect.width, hotkeysRect.height});
      box.setPosition(ui::snap({hotkeysRect.left + off.x, hotkeysRect.top + off.y}));
      box.setFillColor(ui::darken(theme->panel, 7));
      box.setOutlineThickness(1.f);
      box.setOutlineColor(ui::darken(theme->panel, 20));
      rt.draw(box);

      sf::Text t0("Hotkeys", *font, 12);
      t0.setFillColor(theme->subtle);
      t0.setPosition(ui::snap({hotkeysRect.left + off.x + 10.f, hotkeysRect.top + off.y + 8.f}));
      rt.draw(t0);

      // Always-visible hotkeys + the new interaction hints (so players notice immediately).
      const unsigned fs = 12;
      const float x = hotkeysRect.left + off.x + 10.f;
      float y = hotkeysRect.top + off.y + 28.f;

      const float maxW = hotkeysRect.width - 20.f;

      auto line = [&](const std::string &s)
      {
        std::string out = ui::ellipsizeMiddle(*font, fs, s, maxW);
        sf::Text tt(out, *font, fs);
        tt.setFillColor(theme->text);
        tt.setPosition(ui::snap({x, y}));
        rt.draw(tt);
        y += 16.f;
      };

      line("Tab: mode");
      line("T: color");
      line("1-6: piece");
    }

    void drawSidePanels(sf::RenderTarget &rt, sf::Vector2f off, sf::Vector2f shake) const
    {
      (void)shake;

      drawPanel(rt, off, leftRect, "Tools");
      drawPanel(rt, off, rightRect, "Pieces");

      // Tool selector label
      {
        sf::Text lbl("Mode", *font, 12);
        lbl.setFillColor(theme->subtle);
        lbl.setPosition(ui::snap({toolSegRect.left + off.x, toolSegRect.top + off.y - 18.f}));
        rt.draw(lbl);
      }

      // Segmented state control visuals (makes state change unmistakable)
      drawToolSegmentedControl(rt, off);

      // Active states
      btnLeftMove.setActive(selected.kind == ToolKind::Move);
      btnLeftAdd.setActive(selected.kind == ToolKind::Add);
      btnLeftDelete.setActive(selected.kind == ToolKind::Delete);

      // Draw tool buttons on top of the segmented visuals
      btnLeftMove.draw(rt, off);
      btnLeftAdd.draw(rt, off);
      btnLeftDelete.draw(rt, off);

      // Placement color indicator (and always-visible hotkeys field below it)
      {
        float x = addColorRowRect.left + off.x;
        float y = addColorRowRect.top + off.y;

        sf::Text lbl("Add color:", *font, 12);
        lbl.setFillColor(theme->subtle);
        lbl.setPosition(ui::snap({x, y}));
        rt.draw(lbl);

        sf::Text val(placeWhite ? "White (T)" : "Black (T)", *font, 12);
        val.setFillColor(theme->text);
        val.setPosition(ui::snap({x + 74.f, y}));
        rt.draw(val);
      }

      drawHotkeysField(rt, off);

      // Clear / Reset at bottom
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

        // Active highlight:
        // - Add mode highlights selected piece
        // - Move + one-shot carry highlights the carried palette piece
        bool active = (selected.kind == ToolKind::Add && selected.piece == pc);
        if (!active && paletteOneShot && dragging && dragPiece == pc)
          active = true;

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
      sf::RectangleShape bg({fenRect.width, fenRect.height});
      bg.setPosition(ui::snap({fenRect.left + off.x, fenRect.top + off.y}));
      bg.setFillColor(ui::darken(theme->panel, 2));
      bg.setOutlineThickness(1.f);
      bg.setOutlineColor(ui::darken(theme->panel, 18));
      rt.draw(bg);

      {
        sf::Text t("Turn", *font, 12);
        t.setFillColor(theme->subtle);
        t.setPosition(ui::snap({btnTurnW.bounds().left + off.x, btnTurnW.bounds().top + off.y - 16.f}));
        rt.draw(t);
      }

      btnTurnW.setActive(meta.sideToMove == 'w');
      btnTurnB.setActive(meta.sideToMove == 'b');

      btnCastleK.setEnabled(meta.castleK || pb::hasCastleStructure(board, true, true));
      btnCastleQ.setEnabled(meta.castleQ || pb::hasCastleStructure(board, true, false));
      btnCastlek.setEnabled(meta.castlek || pb::hasCastleStructure(board, false, true));
      btnCastleq.setEnabled(meta.castleq || pb::hasCastleStructure(board, false, false));

      btnCastleK.setActive(meta.castleK);
      btnCastleQ.setActive(meta.castleQ);
      btnCastlek.setActive(meta.castlek);
      btnCastleq.setActive(meta.castleq);

      std::string epLbl;
      if (epSelecting)
        epLbl = "EP: select";
      else if (meta.epTarget)
        epLbl = "EP: " + pb::epString(meta);
      else
        epLbl = "EP: -";
      btnEp.setText(epLbl, 12);
      btnEp.setActive(epSelecting || meta.epTarget.has_value());

      btnTurnW.draw(rt, off);
      btnTurnB.draw(rt, off);

      btnCastleK.draw(rt, off);
      btnCastleQ.draw(rt, off);
      btnCastlek.draw(rt, off);
      btnCastleq.draw(rt, off);

      btnEp.draw(rt, off);

      const bool ok = pb::kingsOk(board);

      sf::RectangleShape fenBox({fenBoxRect.width, fenBoxRect.height});
      fenBox.setPosition(ui::snap({fenBoxRect.left + off.x, fenBoxRect.top + off.y}));
      fenBox.setFillColor(ui::darken(theme->panel, 6));
      fenBox.setOutlineThickness(2.f);
      fenBox.setOutlineColor(ok ? sf::Color(122, 205, 164, 220) : sf::Color(220, 70, 70, 220));
      rt.draw(fenBox);

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

      const bool previewingAdd = (!dragging && selected.kind == ToolKind::Add && hoverSquare.has_value());

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
            if (previewingAdd && hoverSquare)
            {
              auto [hx, hy] = *hoverSquare;
              dimUnder = (hx == x && hy == y);
            }
            drawPiece(rt, off, shake, x, y, p, dimUnder);
          }
        }
      }

      // EP selection: show valid targets (DO NOT dim the board itself).
      if (epSelecting)
      {
        const int y = (meta.sideToMove == 'w') ? 2 : 5;
        for (int x = 0; x < 8; ++x)
        {
          if (!pb::isValidEnPassantTarget(board, x, y, meta.sideToMove))
            continue;

          sf::RectangleShape fill({sq, sq});
          fill.setPosition(ui::snap({boardRect.left + off.x + shake.x + x * sq,
                                     boardRect.top + off.y + y * sq}));
          fill.setFillColor(sf::Color(theme->accent.r, theme->accent.g, theme->accent.b, 40));
          rt.draw(fill);

          sf::RectangleShape o({sq, sq});
          o.setPosition(ui::snap({boardRect.left + off.x + shake.x + x * sq,
                                  boardRect.top + off.y + y * sq}));
          o.setFillColor(sf::Color(0, 0, 0, 0));
          o.setOutlineThickness(4.f);
          o.setOutlineColor(sf::Color(theme->accent.r, theme->accent.g, theme->accent.b, 210));
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

      // Hover square + tool-specific overlays
      if (hoverSquare)
      {
        auto [hx, hy] = *hoverSquare;

        const sf::Vector2f sqPos = ui::snap({boardRect.left + off.x + shake.x + hx * sq,
                                             boardRect.top + off.y + hy * sq});

        if (!dragging && selected.kind == ToolKind::Delete)
        {
          const char p = at(hx, hy);
          if (p != '.')
          {
            sf::RectangleShape fill({sq, sq});
            fill.setPosition(sqPos);
            fill.setFillColor(sf::Color(220, 70, 70, 70));
            rt.draw(fill);

            sf::RectangleShape border({sq, sq});
            border.setPosition(sqPos);
            border.setFillColor(sf::Color::Transparent);
            border.setOutlineThickness(3.f);
            border.setOutlineColor(sf::Color(220, 70, 70, 220));
            rt.draw(border);

            sf::VertexArray xmark(sf::Lines, 4);
            xmark[0].position = {sqPos.x + 6.f, sqPos.y + 6.f};
            xmark[1].position = {sqPos.x + sq - 6.f, sqPos.y + sq - 6.f};
            xmark[2].position = {sqPos.x + sq - 6.f, sqPos.y + 6.f};
            xmark[3].position = {sqPos.x + 6.f, sqPos.y + sq - 6.f};
            xmark[0].color = xmark[1].color = xmark[2].color = xmark[3].color = sf::Color(255, 255, 255, 170);
            rt.draw(xmark);
          }
          else
          {
            sf::RectangleShape h({sq, sq});
            h.setPosition(sqPos);
            h.setFillColor(sf::Color(255, 255, 255, 0));
            h.setOutlineThickness(2.f);
            h.setOutlineColor(sf::Color(255, 255, 255, 70));
            rt.draw(h);
          }
        }
        else
        {
          sf::RectangleShape h({sq, sq});
          h.setPosition(sqPos);
          h.setFillColor(sf::Color(255, 255, 255, 0));
          h.setOutlineThickness(2.f);
          h.setOutlineColor(sf::Color(255, 255, 255, 90));
          rt.draw(h);
        }

        // Ghost preview in add mode
        if (!dragging && selected.kind == ToolKind::Add)
        {
          sf::RectangleShape tint({sq, sq});
          tint.setPosition(sqPos);
          tint.setFillColor(sf::Color(theme->accent.r, theme->accent.g, theme->accent.b, 36));
          rt.draw(tint);

          sf::RectangleShape ring({sq, sq});
          ring.setPosition(sqPos);
          ring.setFillColor(sf::Color::Transparent);
          ring.setOutlineThickness(3.f);
          ring.setOutlineColor(sf::Color(theme->accent.r, theme->accent.g, theme->accent.b, 150));
          rt.draw(ring);

          sf::Sprite ghost = spriteForPiece(selected.piece);
          if (ghost.getTexture())
          {
            const bool illegal = wouldViolateKingUniqueness(hx, hy, selected.piece);

            const sf::Vector2f center = ui::snap({sqPos.x + sq * 0.5f,
                                                  sqPos.y + sq * 0.5f + pieceYOffset});

            auto drawOutlinedGhost = [&](sf::Color outline, sf::Color fill)
            {
              sf::Sprite o = ghost;
              o.setColor(outline);

              static const std::array<sf::Vector2f, 4> offs{
                  sf::Vector2f{-1.f, 0.f},
                  sf::Vector2f{1.f, 0.f},
                  sf::Vector2f{0.f, -1.f},
                  sf::Vector2f{0.f, 1.f},
              };

              for (auto d : offs)
              {
                o.setPosition(ui::snap({center.x + d.x, center.y + d.y}));
                rt.draw(o);
              }

              ghost.setColor(fill);
              ghost.setPosition(center);
              rt.draw(ghost);
            };

            sf::Sprite shadow = ghost;
            shadow.setColor(sf::Color(0, 0, 0, 130));
            shadow.setPosition(ui::snap({center.x + 2.f, center.y + 3.f}));
            rt.draw(shadow);

            if (illegal)
              drawOutlinedGhost(sf::Color(60, 0, 0, 180), sf::Color(255, 120, 120, 235));
            else
              drawOutlinedGhost(sf::Color(0, 0, 0, 170), sf::Color(255, 255, 255, 240));
          }
        }
      }

      // Drag/carry sprite (includes palette one-shot and palette drag)
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

    void drawEpSelectionOverlay(sf::RenderTarget &rt, sf::Vector2f off, sf::Vector2f shake) const
    {
      const sf::Color dimC(0, 0, 0, 120);

      const float bL = boardRect.left + off.x + shake.x;
      const float bT = boardRect.top + off.y;
      const float bR = bL + boardRect.width;
      const float bB = bT + boardRect.height;

      const float oL = bounds.left + off.x;
      const float oT = bounds.top + off.y;
      const float oR = oL + bounds.width;
      const float oB = oT + bounds.height;

      auto drawRect = [&](float l, float t, float w, float h)
      {
        if (w <= 0.f || h <= 0.f)
          return;
        sf::RectangleShape r({w, h});
        r.setPosition(ui::snap({l, t}));
        r.setFillColor(dimC);
        rt.draw(r);
      };

      drawRect(oL, oT, bounds.width, bT - oT);
      drawRect(oL, bB, bounds.width, oB - bB);
      drawRect(oL, bT, bL - oL, boardRect.height);
      drawRect(bR, bT, oR - bR, boardRect.height);

      {
        const auto r = btnEp.bounds();
        sf::RectangleShape hl({r.width, r.height});
        hl.setPosition(ui::snap({r.left + off.x, r.top + off.y}));
        hl.setFillColor(sf::Color(theme->accent.r, theme->accent.g, theme->accent.b, 26));
        hl.setOutlineThickness(4.f);
        hl.setOutlineColor(sf::Color(theme->accent.r, theme->accent.g, theme->accent.b, 220));
        rt.draw(hl);
      }

      sf::Text t("Select en passant target square.", *font, 13);
      t.setFillColor(sf::Color(255, 255, 255, 235));
      t.setPosition(ui::snap({boardRect.left + off.x + shake.x + 8.f, boardRect.top + off.y - 22.f}));
      rt.draw(t);
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
        spr.setColor(sf::Color(255, 255, 255, 45));

      rt.draw(spr);
    }

    void drawToast(sf::RenderTarget &rt, sf::Vector2f off) const
    {
      if (toastT <= 0.f || toastMsg.empty())
        return;

      const float a = std::clamp(toastT / toastDur, 0.f, 1.f);
      const float w = std::min(560.f, bottomRect.width * 0.78f);
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
      const float w = std::min(560.f, boardRect.width * 0.92f);
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

    // Start on Move (per requirement)
    m->selected = Impl::ToolSelection::move();
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
