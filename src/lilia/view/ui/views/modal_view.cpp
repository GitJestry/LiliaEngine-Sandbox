#include "lilia/view/ui/views/modal_view.hpp"

namespace lilia::view
{

  bool ModalView::loadFont(const std::string &path)
  {
    m_fontLoaded = m_font.loadFromFile(path);
    return m_fontLoaded;
  }

  void ModalView::onResize(sf::Vector2u ws, sf::Vector2f boardCenter)
  {
    m_ws = ws;
    m_boardCenter = boardCenter;

    // Keep open modals anchored when the board moves.
    // ConfirmResign is window-centered and only needs relayout.
    if (m_resign && m_resign->isOpen())
      m_resign->layout(ws);

    // GameOver is anchored to board center.
    if (m_gameOver && m_gameOver->isOpen())
      m_gameOver->setAnchor(boardCenter);

    m_stack.layout(ws);
  }

  void ModalView::showResign(sf::Vector2u ws, sf::Vector2f /*anchorCenter*/)
  {
    // Idempotent: if already open, just relayout.
    if (m_resign && m_resign->isOpen())
    {
      onResize(ws, m_boardCenter);
      return;
    }

    auto modal = std::make_unique<ui::ConfirmResignModal>();
    m_resign = modal.get();

    ui::ConfirmResignModal::Params p;
    p.theme = &m_theme.uiTheme();
    p.font = m_fontLoaded ? &m_font : nullptr;

    p.onYes = [this]()
    { m_pending = ui::ModalAction::ResignYes; };
    p.onNo = [this]()
    { m_pending = ui::ModalAction::ResignNo; };
    p.onClose = [this]()
    { m_pending = ui::ModalAction::Close; };

    modal->open(ws, std::move(p));
    m_stack.push(std::move(modal));
  }

  void ModalView::hideResign()
  {
    if (m_resign)
      m_resign->close();
  }

  bool ModalView::isResignOpen() const
  {
    return m_resign && m_resign->isOpen();
  }

  void ModalView::showGameOver(sf::Vector2u ws, const std::string &msg, bool won, sf::Vector2f anchorCenter)
  {
    // Robustness: in case showGameOver is called before the first onResize().
    if (m_ws.x == 0 || m_ws.y == 0)
      m_ws = sf::Vector2u{800u, 600u};

    // Idempotent refresh: update content/anchor without pushing a second modal.
    if (m_gameOver && m_gameOver->isOpen())
    {
      ui::GameOverModal::Params p;
      p.theme = &m_theme.uiTheme();
      p.font = m_fontLoaded ? &m_font : nullptr;

      p.onNewBot = [this]()
      { m_pending = ui::ModalAction::NewBot; };
      p.onRematch = [this]()
      { m_pending = ui::ModalAction::Rematch; };
      p.onClose = [this]()
      { m_pending = ui::ModalAction::Close; };

      m_gameOver->open(m_ws, anchorCenter, msg, won, std::move(p));
      return;
    }

    auto modal = std::make_unique<ui::GameOverModal>();
    m_gameOver = modal.get();

    ui::GameOverModal::Params p;
    p.theme = &m_theme.uiTheme();
    p.font = m_fontLoaded ? &m_font : nullptr;

    p.onNewBot = [this]()
    { m_pending = ui::ModalAction::NewBot; };
    p.onRematch = [this]()
    { m_pending = ui::ModalAction::Rematch; };
    p.onClose = [this]()
    { m_pending = ui::ModalAction::Close; };

    modal->open(m_ws, anchorCenter, msg, won, std::move(p));
    m_stack.push(std::move(modal));
  }

  void ModalView::hideGameOver()
  {
    if (m_gameOver)
      m_gameOver->close();
  }

  bool ModalView::isGameOverOpen() const
  {
    return m_gameOver && m_gameOver->isOpen();
  }

  bool ModalView::handleEvent(const sf::Event &e, sf::Vector2f mouse)
  {
    if (m_stack.empty())
      return false;
    return m_stack.handleEvent(e, mouse);
  }

  void ModalView::update(float dt, sf::Vector2f mouse)
  {
    m_stack.update(dt, mouse, [this](ui::Modal &m)
                   { trackDismissed(m); });
  }

  void ModalView::drawOverlay(sf::RenderWindow &win) const
  {
    m_stack.drawOverlay(win);
  }

  void ModalView::drawPanel(sf::RenderWindow &win) const
  {
    m_stack.drawPanel(win);
  }

  ui::ModalAction ModalView::consumeAction()
  {
    if (!m_pending)
      return ui::ModalAction::None;

    const ui::ModalAction out = *m_pending;
    m_pending.reset();
    return out;
  }

  void ModalView::trackDismissed(ui::Modal &m)
  {
    if (&m == m_resign)
      m_resign = nullptr;
    if (&m == m_gameOver)
      m_gameOver = nullptr;
  }

} // namespace lilia::view
