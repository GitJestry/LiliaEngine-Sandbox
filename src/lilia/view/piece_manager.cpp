#include "lilia/view/piece_manager.hpp"

#include <iostream>
#include <string>
#include <utility>

#include "lilia/view/animation/chess_animator.hpp"
#include "lilia/view/color_palette_manager.hpp"
#include "lilia/view/render_constants.hpp"
#include "lilia/view/texture_table.hpp"

namespace lilia::view {

PieceManager::PieceManager(const BoardView &boardRef)
    : m_board_view_ref(boardRef), m_pieces() {
  m_paletteListener =
      ColorPaletteManager::get().addListener([this]() { onPaletteChanged(); });
}

PieceManager::~PieceManager() {
  ColorPaletteManager::get().removeListener(m_paletteListener);
}

/* -------------------- FEN -------------------- */
void PieceManager::initFromFen(const std::string &fen) {
  std::string boardPart = fen.substr(0, fen.find(' '));
  int rank = 7;
  int file = 0;
  for (char ch : boardPart) {
    if (ch == '/') {
      rank--;
      file = 0;
    } else if (std::isdigit(static_cast<unsigned char>(ch))) {
      file += ch - '0';
    } else {
      int pos = file + rank * constant::BOARD_SIZE;
      core::PieceType type;
      switch (std::tolower(static_cast<unsigned char>(ch))) {
        case 'k':
          type = core::PieceType::King;
          break;
        case 'p':
          type = core::PieceType::Pawn;
          break;
        case 'n':
          type = core::PieceType::Knight;
          break;
        case 'b':
          type = core::PieceType::Bishop;
          break;
        case 'r':
          type = core::PieceType::Rook;
          break;
        default:
          type = core::PieceType::Queen;
          break;
      }

      addPiece(
          type,
          (std::isupper(static_cast<unsigned char>(ch)) ? core::Color::White : core::Color::Black),
          static_cast<core::Square>(pos));
      file++;
    }
  }
}

/* -------------------- Query helpers -------------------- */
[[nodiscard]] Entity::ID_type PieceManager::getPieceID(core::Square pos) const {
  if (pos == core::NO_SQUARE) return 0;
  auto ghost = m_premove_pieces.find(pos);
  if (ghost != m_premove_pieces.end()) return ghost->second.getId();
  if (m_hidden_squares.count(pos) > 0) return 0;
  auto it = m_pieces.find(pos);
  return it != m_pieces.end() ? it->second.getId() : 0;
}

[[nodiscard]] bool PieceManager::isSameColor(core::Square sq1, core::Square sq2) const {
  auto getPiece = [this](core::Square sq) -> const Piece * {
    auto ghost = m_premove_pieces.find(sq);
    if (ghost != m_premove_pieces.end()) return &ghost->second;
    if (m_hidden_squares.count(sq) > 0) return nullptr;
    auto it = m_pieces.find(sq);
    return it != m_pieces.end() ? &it->second : nullptr;
  };
  const Piece *p1 = getPiece(sq1);
  const Piece *p2 = getPiece(sq2);
  if (!p1 || !p2) return false;
  return p1->getColor() == p2->getColor();
}

/* -------------------- Placement -------------------- */
Entity::Position PieceManager::createPiecePositon(core::Square pos) {
  return m_board_view_ref.getSquareScreenPos(pos) +
         Entity::Position{0.f, constant::SQUARE_PX_SIZE * 0.02f};
}

void PieceManager::addPiece(core::PieceType type, core::Color color, core::Square pos) {
  std::uint8_t numTypes = 6;
  std::string filename = constant::ASSET_PIECES_FILE_PATH + std::string("/piece_") +
                         std::to_string(static_cast<std::uint8_t>(type) +
                                        numTypes * static_cast<std::uint8_t>(color)) +
                         ".png";

  const sf::Texture &texture = TextureTable::getInstance().get(filename);

  Piece newpiece(color, type, texture);
  newpiece.setScale(constant::ASSET_PIECE_SCALE, constant::ASSET_PIECE_SCALE);
  m_pieces[pos] = std::move(newpiece);
  m_pieces[pos].setPosition(createPiecePositon(pos));
}

void PieceManager::movePiece(core::Square from, core::Square to, core::PieceType promotion) {
  Piece movingPiece;

  // The piece might have been stashed in m_captured_backup if a premove
  // ghost temporarily "captured" it. Restore it if necessary so the real
  // move can proceed.
  auto fromIt = m_pieces.find(from);
  if (fromIt != m_pieces.end()) {
    movingPiece = std::move(fromIt->second);
    m_pieces.erase(fromIt);
  } else {
    auto backupIt = m_captured_backup.find(from);
    if (backupIt != m_captured_backup.end()) {
      movingPiece = std::move(backupIt->second);
      m_captured_backup.erase(backupIt);
      m_hidden_squares.erase(from);
    } else {
      // No piece to move – likely an out-of-sync premove scenario.
      return;
    }
  }

  // If the destination square holds a stashed piece, drop it.
  removePiece(to);

  if (promotion != core::PieceType::None) {
    addPiece(promotion, movingPiece.getColor(), to);
  } else {
    m_pieces[to] = std::move(movingPiece);
  }

  // The piece now occupies 'to', so ensure it's not hidden and reveal the origin.
  m_hidden_squares.erase(from);
  m_hidden_squares.erase(to);
}

void PieceManager::removePiece(core::Square pos) {
  m_pieces.erase(pos);
  m_captured_backup.erase(pos);
  m_hidden_squares.erase(pos);
}

void PieceManager::removeAll() {
  m_pieces.clear();
}

/* -------------------- Piece info -------------------- */
core::PieceType PieceManager::getPieceType(core::Square pos) const {
  auto ghost = m_premove_pieces.find(pos);
  if (ghost != m_premove_pieces.end()) return ghost->second.getType();

  // Hidden squares occur when a premove ghost temporarily removes a piece
  // from the board. Instead of reporting a default value, surface the real
  // piece from the primary piece map or the captured-piece backup so callers
  // see the true board state.
  if (m_hidden_squares.count(pos) > 0) {
    if (auto it = m_pieces.find(pos); it != m_pieces.end())
      return it->second.getType();
    if (auto it = m_captured_backup.find(pos); it != m_captured_backup.end())
      return it->second.getType();
    return core::PieceType::None;
  }

  if (auto it = m_pieces.find(pos); it != m_pieces.end())
    return it->second.getType();
  if (auto it = m_captured_backup.find(pos); it != m_captured_backup.end())
    return it->second.getType();
  return core::PieceType::None;
}

core::Color PieceManager::getPieceColor(core::Square pos) const {
  auto ghost = m_premove_pieces.find(pos);
  if (ghost != m_premove_pieces.end()) return ghost->second.getColor();

  if (m_hidden_squares.count(pos) > 0) {
    if (auto it = m_pieces.find(pos); it != m_pieces.end())
      return it->second.getColor();
    if (auto it = m_captured_backup.find(pos); it != m_captured_backup.end())
      return it->second.getColor();
    return core::Color::White;
  }

  if (auto it = m_pieces.find(pos); it != m_pieces.end())
    return it->second.getColor();
  if (auto it = m_captured_backup.find(pos); it != m_captured_backup.end())
    return it->second.getColor();
  return core::Color::White;
}

[[nodiscard]] bool PieceManager::hasPieceOnSquare(core::Square pos) const {
  if (m_premove_pieces.find(pos) != m_premove_pieces.end()) return true;
  if (m_hidden_squares.count(pos) > 0) return false;
  return m_pieces.find(pos) != m_pieces.end();
}

Entity::Position PieceManager::getPieceSize(core::Square pos) const {
  auto ghost = m_premove_pieces.find(pos);
  if (ghost != m_premove_pieces.end()) return ghost->second.getCurrentSize();
  if (m_hidden_squares.count(pos) > 0) return {0.f, 0.f};
  auto it = m_pieces.find(pos);
  if (it == m_pieces.end()) return {0.f, 0.f};
  return it->second.getCurrentSize();
}

/* -------------------- Movement helpers -------------------- */
[[nodiscard]] inline Entity::Position mouseToEntityPos(core::MousePos mousePos) {
  return static_cast<Entity::Position>(mousePos);
}

static auto findGhostByOrigin(std::unordered_map<core::Square, core::Square> &origin,
                              std::unordered_map<core::Square, Piece> &ghosts, core::Square from)
    -> decltype(ghosts.find(from)) {
  // If a ghost is currently sitting on 'from' (i.e., previous to), this hits:
  auto it = ghosts.find(from);
  if (it != ghosts.end()) return it;
  // Otherwise, look up any ghost whose origin == from, then find its 'to'
  for (auto &kv : origin) {  // kv: to -> from
    if (kv.second == from) return ghosts.find(kv.first);
  }
  return ghosts.end();
}

void PieceManager::setPieceToSquareScreenPos(core::Square from, core::Square to) {
  if (auto git = findGhostByOrigin(m_premove_origin, m_premove_pieces, from);
      git != m_premove_pieces.end()) {
    git->second.setPosition(createPiecePositon(to));
    return;
  }
  auto it = m_pieces.find(from);
  if (it != m_pieces.end() && m_hidden_squares.count(from) == 0) {
    it->second.setPosition(createPiecePositon(to));
  }
}

void PieceManager::setPieceToScreenPos(core::Square pos, core::MousePos mousePos) {
  if (auto git = findGhostByOrigin(m_premove_origin, m_premove_pieces, pos);
      git != m_premove_pieces.end()) {
    git->second.setPosition(mouseToEntityPos(mousePos));
    return;
  }
  auto it = m_pieces.find(pos);
  if (it != m_pieces.end() && m_hidden_squares.count(pos) == 0) {
    it->second.setPosition(mouseToEntityPos(mousePos));
  }
}

void PieceManager::setPieceToScreenPos(core::Square pos, Entity::Position entityPos) {
  if (auto git = findGhostByOrigin(m_premove_origin, m_premove_pieces, pos);
      git != m_premove_pieces.end()) {
    git->second.setPosition(entityPos);
    return;
  }
  auto it = m_pieces.find(pos);
  if (it != m_pieces.end() && m_hidden_squares.count(pos) == 0) {
    it->second.setPosition(entityPos);
  }
}

/* -------------------- Rendering -------------------- */
void PieceManager::renderPieces(sf::RenderWindow &window,
                                const animation::ChessAnimator &chessAnimRef) {
  for (auto &pair : m_pieces) {
    const auto pos = pair.first;
    auto &piece = pair.second;
    if (m_hidden_squares.count(pos) > 0) continue;
    if (m_premove_pieces.count(pos) > 0) continue;  // ghost on top, skip base
    if (!chessAnimRef.isAnimating(piece.getId())) {
      piece.setPosition(createPiecePositon(pos));
      piece.draw(window);
    }
  }
}

// draw ghosts after animator so they always win the z-order.
void PieceManager::renderPremoveGhosts(sf::RenderWindow &window,
                                       const animation::ChessAnimator &chessAnimRef) {
  for (auto &pair : m_premove_pieces) {
    if (!chessAnimRef.isAnimating(pair.second.getId())) {
      pair.second.setPosition(createPiecePositon(pair.first));
    }
    pair.second.draw(window);
  }
}

void PieceManager::renderPiece(core::Square pos, sf::RenderWindow &window) {
  if (m_hidden_squares.count(pos) > 0) return;
  auto it = m_pieces.find(pos);
  if (it != m_pieces.end()) {
    it->second.draw(window);
  }
}

// Build a fresh, independent piece (new ID) for ghosts.
static Piece makeGhost(core::PieceType type, core::Color color) {
  constexpr std::uint8_t numTypes = 6;
  std::string filename = constant::ASSET_PIECES_FILE_PATH + std::string("/piece_") +
                         std::to_string(static_cast<std::uint8_t>(type) +
                                        numTypes * static_cast<std::uint8_t>(color)) +
                         ".png";
  const sf::Texture &texture = TextureTable::getInstance().get(filename);
  Piece p(color, type, texture);
  p.setScale(constant::ASSET_PIECE_SCALE, constant::ASSET_PIECE_SCALE);
  return p;
}

void PieceManager::setPremovePiece(core::Square from, core::Square to, core::PieceType promotion) {
  Piece ghost;
  core::Square origin = from;  // track the true origin across retargeting

  // Move existing ghost forward in a chain?
  if (auto existing = m_premove_pieces.find(from); existing != m_premove_pieces.end()) {
    ghost = std::move(existing->second);
    m_premove_pieces.erase(existing);

    // Preserve the original starting square if this ghost was already moved
    if (auto itO = m_premove_origin.find(from); itO != m_premove_origin.end()) {
      origin = itO->second;
      m_premove_origin.erase(itO);
    }

    // If this square held a stashed capture from an earlier step, keep the
    // backup so cancelling the premove restores everything.
    if (promotion != core::PieceType::None) {
      ghost = makeGhost(promotion, ghost.getColor());
    }
  } else {
    auto it = m_pieces.find(from);
    if (it == m_pieces.end()) return;
    core::PieceType gType = (promotion != core::PieceType::None) ? promotion : it->second.getType();
    ghost = makeGhost(gType, it->second.getColor());
    m_hidden_squares.insert(from);
  }

  // If 'to' already has a ghost, treat it as captured: remove the ghost and
  // keep its origin hidden so the piece doesn't pop back to its start square.
  if (auto prevGhost = m_premove_pieces.find(to); prevGhost != m_premove_pieces.end()) {
    if (auto itO = m_premove_origin.find(to); itO != m_premove_origin.end()) {
      m_premove_origin.erase(itO);
    }
    m_premove_pieces.erase(prevGhost);
  }

  // If 'to' has a real piece, stash it so cancel restores it
  if (auto captured = m_pieces.find(to); captured != m_pieces.end()) {
    m_captured_backup[to] = std::move(captured->second);
    m_pieces.erase(captured);
  }

  ghost.setPosition(createPiecePositon(to));
  m_premove_pieces[to] = std::move(ghost);
  m_premove_origin[to] = origin;
}

void PieceManager::consumePremoveGhost(core::Square from, core::Square to) {
  auto it = m_premove_origin.find(to);
  if (it == m_premove_origin.end() || it->second != from) return;

  // Remove ghost + mapping
  m_premove_origin.erase(it);
  m_premove_pieces.erase(to);

  // Reveal the real piece again, no real move happened
  m_hidden_squares.erase(from);

  // If we had temporarily stashed a piece that was sitting on 'to',
  // put it back so the board matches reality.
  if (auto bak = m_captured_backup.find(to); bak != m_captured_backup.end()) {
    m_pieces[to] = std::move(bak->second);
    m_pieces[to].setPosition(createPiecePositon(to));
    m_captured_backup.erase(to);
  }
}

void PieceManager::applyPremoveInstant(core::Square from, core::Square to,
                                       core::PieceType promotion) {
  if (auto it = m_premove_origin.find(to); it != m_premove_origin.end() && it->second == from) {
    m_premove_origin.erase(it);
  }
  if (auto git = m_premove_pieces.find(to); git != m_premove_pieces.end()) {
    m_premove_pieces.erase(git);
  }
  movePiece(from, to, promotion);  // moves the real piece & reveals it at 'to'
  m_hidden_squares.erase(from);
  m_hidden_squares.erase(to);
  m_captured_backup.erase(to);
}

void PieceManager::clearPremovePieces(bool restore) {
  if (restore) {
    // Put back anything we had visually 'captured' by ghosts
    for (auto &pair : m_captured_backup) {
      m_pieces[pair.first] = std::move(pair.second);
      m_pieces[pair.first].setPosition(createPiecePositon(pair.first));
    }
    m_captured_backup.clear();  // only clear when we actually restored
    m_hidden_squares.clear();   // safe to clear — nothing is hidden anymore
  } else {
    // We’re just hiding ghosts temporarily
    // Keep backups so we can reconstitute the preview later.
    m_hidden_squares.clear();  // show the true board while in history view
    // m_captured_backup stays intact on purpose
  }

  m_premove_pieces.clear();
  m_premove_origin.clear();
}

void PieceManager::onPaletteChanged() {
  auto reload = [](Piece& p) {
    std::uint8_t numTypes = 6;
    std::string filename = constant::ASSET_PIECES_FILE_PATH +
                           std::string("/piece_") +
                           std::to_string(static_cast<std::uint8_t>(p.getType()) +
                                          numTypes * static_cast<std::uint8_t>(p.getColor())) +
                           ".png";
    p.setTexture(TextureTable::getInstance().get(filename));
    p.setScale(constant::ASSET_PIECE_SCALE, constant::ASSET_PIECE_SCALE);
  };

  for (auto& [sq, piece] : m_pieces) reload(piece);
  for (auto& [sq, piece] : m_premove_pieces) reload(piece);
  for (auto& [sq, piece] : m_captured_backup) reload(piece);
}

}  // namespace lilia::view
