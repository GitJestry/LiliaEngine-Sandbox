#include "lilia/model/chess_game.hpp"

#include <cctype>
#include <optional>
#include <string_view>

#include "lilia/model/move_helper.hpp"

namespace lilia::model {

namespace {

// Fast ASCII helpers
inline char tolower_ascii(char c) noexcept {
  return (c >= 'A' && c <= 'Z') ? static_cast<char>(c | 32) : c;
}

inline int squareFromUCI(const char* sq) noexcept {
  // expects "e2", returns 0..63 or -1
  if (!sq) return -1;
  const int file = sq[0] - 'a';
  const int rank = sq[1] - '1';
  return (static_cast<unsigned>(file) < 8u && static_cast<unsigned>(rank) < 8u) ? (rank * 8 + file)
                                                                                : -1;
}

inline core::Square stringToSquare(std::string_view sv) noexcept {
  if (sv.size() < 2) return core::NO_SQUARE;
  const char f = sv[0];
  const char r = sv[1];
  if (f < 'a' || f > 'h' || r < '1' || r > '8') return core::NO_SQUARE;
  const uint8_t file = static_cast<uint8_t>(f - 'a');
  const uint8_t rank = static_cast<uint8_t>(r - '1');
  return static_cast<core::Square>(file + rank * 8);
}

inline int parseInt(std::string_view sv) noexcept {
  int val = 0;
  for (char c : sv) {
    if (c < '0' || c > '9') break;
    val = val * 10 + (c - '0');
  }
  return val;
}

}  // namespace

// ---------------- Public API ----------------

ChessGame::ChessGame() {
  m_pseudo_moves.reserve(256);
  m_legal_moves.reserve(256);
}

bool ChessGame::doMoveUCI(const std::string& uciMove) {
  const size_t len = uciMove.size();
  if (len < 4) return false;

  const char* ptr = uciMove.c_str();
  const int from = squareFromUCI(ptr + 0);
  const int to = squareFromUCI(ptr + 2);
  if (from < 0 || to < 0) return false;

  core::PieceType promo = core::PieceType::None;
  if (len >= 5) {
    switch (tolower_ascii(uciMove[4])) {
      case 'q':
        promo = core::PieceType::Queen;
        break;
      case 'r':
        promo = core::PieceType::Rook;
        break;
      case 'b':
        promo = core::PieceType::Bishop;
        break;
      case 'n':
        promo = core::PieceType::Knight;
        break;
      default:
        promo = core::PieceType::None;
        break;
    }
  }
  return doMove(static_cast<core::Square>(from), static_cast<core::Square>(to), promo);
}

std::optional<Move> ChessGame::getMove(core::Square from, core::Square to) {
  const auto& moves = generateLegalMoves();
  for (const auto& m : moves) {
    if (m.from() == from && m.to() == to) return m;
  }
  return std::nullopt;
}

void ChessGame::setPosition(const std::string& fen) {
  // full reset
  m_position = Position{};
  m_result = core::GameResult::ONGOING;
  m_pseudo_moves.clear();
  m_legal_moves.clear();

  // Split FEN into 6 fields manually (faster than stringstream)
  std::string_view sv{fen};
  std::string_view fields[6]{};
  for (int i = 0; i < 6; ++i) {
    const size_t sp = sv.find(' ');
    if (sp == std::string_view::npos) {
      fields[i] = sv;
      for (++i; i < 6; ++i) fields[i] = {};
      break;
    }
    fields[i] = sv.substr(0, sp);
    sv.remove_prefix(sp + 1);
  }

  const std::string_view board = fields[0];
  const std::string_view activeColor = fields[1];
  const std::string_view castling = fields[2];
  const std::string_view enPassant = fields[3];
  const std::string_view halfmoveClock = fields[4];
  const std::string_view fullmoveNumber = fields[5];

  // Board placement
  uint8_t rank = 7, file = 0;
  for (char ch : board) {
    if (ch == '/') {
      if (rank == 0) { /* ignore overflows */
      }
      if (file > 8) { /* ignore malformed */
      }
      file = 0;
      if (rank > 0) --rank;
      continue;
    }
    if (std::isdigit(static_cast<unsigned char>(ch))) {
      file = static_cast<uint8_t>(file + (ch - '0'));
      continue;
    }
    if (file > 7) continue;  // guard malformed FEN

    const char lo = tolower_ascii(ch);
    core::PieceType type;
    switch (lo) {
      case 'k':
        type = core::PieceType::King;
        break;
      case 'q':
        type = core::PieceType::Queen;
        break;
      case 'r':
        type = core::PieceType::Rook;
        break;
      case 'b':
        type = core::PieceType::Bishop;
        break;
      case 'n':
        type = core::PieceType::Knight;
        break;
      case 'p':
        type = core::PieceType::Pawn;
        break;
      default:
        type = core::PieceType::None;
        break;
    }
    if (type != core::PieceType::None) {
      const core::Square sq = static_cast<core::Square>(file + rank * 8);
      const core::Color col = (ch == lo) ? core::Color::Black : core::Color::White;
      m_position.getBoard().setPiece(sq, {type, col});
      ++file;
    }
  }

  // Active color
  m_position.getState().sideToMove =
      (!activeColor.empty() && activeColor[0] == 'w') ? core::Color::White : core::Color::Black;

  // Castling rights
  uint8_t rights = 0;
  for (char c : castling) {
    switch (c) {
      case 'K':
        rights |= bb::Castling::WK;
        break;
      case 'Q':
        rights |= bb::Castling::WQ;
        break;
      case 'k':
        rights |= bb::Castling::BK;
        break;
      case 'q':
        rights |= bb::Castling::BQ;
        break;
      case '-':
      default:
        break;
    }
  }
  m_position.getState().castlingRights = rights;

  // En passant
  if (enPassant.size() == 2) {
    m_position.getState().enPassantSquare = stringToSquare(enPassant);
  } else {
    m_position.getState().enPassantSquare = core::NO_SQUARE;
  }

  // Clocks (robust parse)
  int hm = 0, fm = 1;
  if (!halfmoveClock.empty()) {
    hm = parseInt(halfmoveClock);
  }
  if (!fullmoveNumber.empty()) {
    fm = parseInt(fullmoveNumber);
    if (fm == 0) fm = 1;
  }
  m_position.getState().halfmoveClock = hm;
  m_position.getState().fullmoveNumber = fm;

  // Rebuild hashes/accumulators
  m_position.buildHash();
  m_position.rebuildEvalAcc();
}

void ChessGame::buildHash() {
  m_position.buildHash();
}

const std::vector<Move>& ChessGame::generateLegalMoves() {
  m_pseudo_moves.clear();
  m_legal_moves.clear();

  m_move_gen.generatePseudoLegalMoves(m_position.getBoard(), m_position.getState(), m_pseudo_moves);

  // Filter legality by make/unmake (fast because Position has fast quiet paths)
  for (const auto& m : m_pseudo_moves) {
    if (m_position.doMove(m)) {
      m_position.undoMove();
      m_legal_moves.push_back(m);
    }
  }
  return m_legal_moves;
}

const GameState& ChessGame::getGameState() {
  return m_position.getState();
}

core::Square ChessGame::getRookSquareFromCastleside(CastleSide castleSide, core::Color side) {
  if (castleSide == CastleSide::KingSide)
    return (side == core::Color::White) ? static_cast<core::Square>(7)
                                        : static_cast<core::Square>(63);
  if (castleSide == CastleSide::QueenSide)
    return (side == core::Color::White) ? static_cast<core::Square>(0)
                                        : static_cast<core::Square>(56);
  return core::NO_SQUARE;
}

core::Square ChessGame::getKingSquare(core::Color color) {
  const bb::Bitboard kbb = m_position.getBoard().getPieces(color, core::PieceType::King);
  if (!kbb) return core::NO_SQUARE;
  return static_cast<core::Square>(bb::ctz64(kbb));
}

void ChessGame::checkGameResult() {
  if (generateLegalMoves().empty()) {
    if (isKingInCheck(m_position.getState().sideToMove))
      m_result = core::GameResult::CHECKMATE;
    else
      m_result = core::GameResult::STALEMATE;
  }
  if (m_position.checkInsufficientMaterial()) m_result = core::GameResult::INSUFFICIENT;
  if (m_position.checkMoveRule()) m_result = core::GameResult::MOVERULE;
  if (m_position.checkRepetition()) m_result = core::GameResult::REPETITION;
}

core::GameResult ChessGame::getResult() {
  return m_result;
}

void ChessGame::setResult(core::GameResult res) {
  m_result = res;
}

bb::Piece ChessGame::getPiece(core::Square sq) {
  auto opt = m_position.getBoard().getPiece(sq);
  return opt.value_or(bb::Piece{core::PieceType::None, core::Color::White});
}

bool ChessGame::doMove(core::Square from, core::Square to, core::PieceType promotion) {
  const auto& moves = generateLegalMoves();
  for (const auto& m : moves) {
    if (m.from() == from && m.to() == to && m.promotion() == promotion) {
      if (m_position.doMove(m)) {
        return true;
      }
      break;  // execute once
    }
  }
  return false;
}

bool ChessGame::isKingInCheck(core::Color from) const {
  const bb::Bitboard kbb = m_position.getBoard().getPieces(from, core::PieceType::King);
  if (!kbb) return false;
  const core::Square ksq = static_cast<core::Square>(bb::ctz64(kbb));
  return attackedBy(m_position.getBoard(), ksq, ~from, m_position.getBoard().getAllPieces());
}

Position& ChessGame::getPositionRefForBot() {
  return m_position;
}

std::string ChessGame::getFen() const {
  std::string fen;
  fen.reserve(100);
  const auto& board = m_position.getBoard();

  for (int rank = 7; rank >= 0; --rank) {
    int empty = 0;
    for (int file = 0; file < 8; ++file) {
      const core::Square sq = static_cast<core::Square>(rank * 8 + file);
      const auto piece = board.getPiece(sq);
      if (piece.has_value()) {
        if (empty) {
          fen.push_back(static_cast<char>('0' + empty));
          empty = 0;
        }
        char ch;
        switch (piece->type) {
          case core::PieceType::King:
            ch = 'k';
            break;
          case core::PieceType::Queen:
            ch = 'q';
            break;
          case core::PieceType::Rook:
            ch = 'r';
            break;
          case core::PieceType::Bishop:
            ch = 'b';
            break;
          case core::PieceType::Knight:
            ch = 'n';
            break;
          case core::PieceType::Pawn:
            ch = 'p';
            break;
          default:
            ch = '?';
            break;
        }
        if (piece->color == core::Color::White) ch = static_cast<char>(std::toupper(ch));
        fen.push_back(ch);
      } else {
        ++empty;
      }
    }
    if (empty) fen.push_back(static_cast<char>('0' + empty));
    if (rank) fen.push_back('/');
  }

  const auto& st = m_position.getState();
  fen.push_back(' ');
  fen.push_back(st.sideToMove == core::Color::White ? 'w' : 'b');
  fen.push_back(' ');

  if (st.castlingRights) {
    if (st.castlingRights & bb::Castling::WK) fen.push_back('K');
    if (st.castlingRights & bb::Castling::WQ) fen.push_back('Q');
    if (st.castlingRights & bb::Castling::BK) fen.push_back('k');
    if (st.castlingRights & bb::Castling::BQ) fen.push_back('q');
  } else {
    fen.push_back('-');
  }
  fen.push_back(' ');

  if (st.enPassantSquare == core::NO_SQUARE) {
    fen.push_back('-');
  } else {
    const int sq = static_cast<int>(st.enPassantSquare);
    fen.push_back(static_cast<char>('a' + (sq & 7)));
    fen.push_back(static_cast<char>('1' + (sq >> 3)));
  }
  fen.push_back(' ');
  fen.append(std::to_string(st.halfmoveClock));
  fen.push_back(' ');
  fen.append(std::to_string(st.fullmoveNumber));

  return fen;
}

}  // namespace lilia::model
