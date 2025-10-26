#include "lilia/model/pgn_loader.hpp"

#include <algorithm>
#include <cctype>
#include <optional>
#include <sstream>
#include <string_view>

#include "lilia/constants.hpp"
#include "lilia/model/chess_game.hpp"

namespace lilia::model {

namespace {

bool isResultToken(std::string_view tok) {
  return tok == "1-0" || tok == "0-1" || tok == "1/2-1/2" || tok == "1/2" || tok == "*";
}

core::PieceType charToPiece(char c) {
  switch (c) {
    case 'K':
      return core::PieceType::King;
    case 'Q':
      return core::PieceType::Queen;
    case 'R':
      return core::PieceType::Rook;
    case 'B':
      return core::PieceType::Bishop;
    case 'N':
      return core::PieceType::Knight;
    case 'P':
      return core::PieceType::Pawn;
    default:
      return core::PieceType::None;
  }
}

bool isFileChar(char c) { return c >= 'a' && c <= 'h'; }

bool isRankChar(char c) { return c >= '1' && c <= '8'; }

std::string trimQuotes(const std::string &value) {
  if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
    return value.substr(1, value.size() - 2);
  }
  return value;
}

std::string removeComments(const std::string &text) {
  std::string out;
  out.reserve(text.size());
  int braceDepth = 0;
  int parenDepth = 0;
  bool semicolonComment = false;
  for (char ch : text) {
    if (semicolonComment) {
      if (ch == '\n') semicolonComment = false;
      continue;
    }
    if (ch == ';') {
      semicolonComment = true;
      continue;
    }
    if (ch == '{') {
      ++braceDepth;
      continue;
    }
    if (ch == '}') {
      if (braceDepth > 0) --braceDepth;
      continue;
    }
    if (ch == '(') {
      ++parenDepth;
      continue;
    }
    if (ch == ')') {
      if (parenDepth > 0) --parenDepth;
      continue;
    }
    if (braceDepth > 0 || parenDepth > 0) continue;
    out.push_back(ch);
  }
  return out;
}

std::string sanitizeToken(std::string token) {
  while (!token.empty()) {
    char back = token.back();
    if (back == '+') {
      token.pop_back();
    } else if (back == '#') {
      token.pop_back();
    } else if (back == '!' || back == '?') {
      token.pop_back();
    } else {
      break;
    }
  }
  return token;
}

core::Color sideFromFen(const std::string &fen) {
  auto firstSpace = fen.find(' ');
  if (firstSpace == std::string::npos) return core::Color::White;
  auto secondSpace = fen.find(' ', firstSpace + 1);
  if (secondSpace == std::string::npos) secondSpace = fen.size();
  std::string_view field = std::string_view(fen).substr(firstSpace + 1, secondSpace - firstSpace - 1);
  return (!field.empty() && (field.front() == 'b' || field.front() == 'B')) ? core::Color::Black
                                                                           : core::Color::White;
}

}  // namespace

std::optional<PgnGame> parsePgn(const std::string &pgnText) {
  if (pgnText.empty()) return std::nullopt;

  std::string normalized;
  normalized.reserve(pgnText.size());
  for (char ch : pgnText) {
    if (ch != '\r') normalized.push_back(ch);
  }

  std::istringstream input(normalized);
  std::string line;
  std::string movetext;
  std::string resultTag;
  std::string startFen = core::START_FEN;

  while (std::getline(input, line)) {
    if (!line.empty() && line.front() == '[') {
      auto close = line.find(']');
      if (close == std::string::npos) continue;
      auto inner = line.substr(1, close - 1);
      auto space = inner.find(' ');
      if (space == std::string::npos) continue;
      std::string tag = inner.substr(0, space);
      std::string value = inner.substr(space + 1);
      value = trimQuotes(value);
      if (tag == "FEN") {
        startFen = value;
      } else if (tag == "Result") {
        resultTag = value;
      }
    } else {
      movetext.append(line);
      movetext.push_back('\n');
    }
  }

  movetext = removeComments(movetext);

  PgnGame out;
  out.startFen = startFen;
  out.fenHistory.push_back(startFen);
  if (!resultTag.empty()) out.result = resultTag;

  ChessGame game;
  game.setPosition(startFen);

  std::istringstream movesStream(movetext);
  std::string rawToken;
  while (movesStream >> rawToken) {
    if (rawToken.empty()) continue;

    if (isResultToken(rawToken)) {
      out.result = rawToken;
      break;
    }

    // Strip move numbers like 12. or 12...e4
    std::size_t idx = 0;
    while (idx < rawToken.size() && std::isdigit(static_cast<unsigned char>(rawToken[idx]))) {
      ++idx;
    }
    if (idx < rawToken.size() && rawToken[idx] == '.') {
      while (idx < rawToken.size() && rawToken[idx] == '.') ++idx;
      rawToken.erase(0, idx);
      if (rawToken.empty()) continue;
    }

    if (rawToken.empty()) continue;
    if (rawToken.front() == '$') continue;  // NAGs

    std::string stripped = sanitizeToken(rawToken);
    if (stripped.empty()) continue;

    std::string upperStripped = stripped;
    std::transform(upperStripped.begin(), upperStripped.end(), upperStripped.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });

    const bool castleKing = (upperStripped == "O-O" || upperStripped == "0-0");
    const bool castleQueen = (upperStripped == "O-O-O" || upperStripped == "0-0-0");

    std::string displaySan = stripped;
    std::string work = stripped;

    core::PieceType promotion = core::PieceType::None;
    auto eqPos = work.find('=');
    if (eqPos != std::string::npos) {
      if (eqPos + 1 < work.size()) promotion = charToPiece(static_cast<char>(std::toupper(work[eqPos + 1])));
      work.erase(eqPos);
    }

    core::PieceType piece = core::PieceType::Pawn;
    std::size_t pos = 0;
    if (!work.empty() && std::isupper(static_cast<unsigned char>(work[0])) && work[0] != 'O') {
      piece = charToPiece(work[0]);
      ++pos;
    }

    std::string remainder = work.substr(pos);
    bool capture = false;
    auto capPos = remainder.find('x');
    if (capPos != std::string::npos) {
      capture = true;
      remainder.erase(capPos, 1);
    }

    if (castleKing || castleQueen) {
      piece = core::PieceType::King;
    } else {
      if (remainder.size() < 2) return std::nullopt;
    }

    int toFile = -1;
    int toRank = -1;
    if (!castleKing && !castleQueen) {
      std::string target = remainder.substr(remainder.size() - 2);
      if (!isFileChar(target[0]) || !isRankChar(target[1])) return std::nullopt;
      toFile = target[0] - 'a';
      toRank = target[1] - '1';
      remainder.erase(remainder.size() - 2);
    }

    std::optional<int> fromFile;
    std::optional<int> fromRank;
    for (char c : remainder) {
      if (isFileChar(c)) fromFile = c - 'a';
      else if (isRankChar(c)) fromRank = c - '1';
    }

    const auto &legal = game.generateLegalMoves();
    std::optional<Move> matched;
    for (const auto &mv : legal) {
      if (castleKing) {
        if (mv.castle() == CastleSide::KingSide) {
          matched = mv;
          break;
        }
        continue;
      }
      if (castleQueen) {
        if (mv.castle() == CastleSide::QueenSide) {
          matched = mv;
          break;
        }
        continue;
      }
      const auto pieceOnFrom = game.getPiece(mv.from());
      if (pieceOnFrom.type != piece) continue;
      if (promotion != mv.promotion()) continue;
      const int mvFile = static_cast<int>(mv.to()) & 7;
      const int mvRank = static_cast<int>(mv.to()) >> 3;
      if (mvFile != toFile || mvRank != toRank) continue;
      if (capture && !mv.isCapture()) continue;
      if (!capture && mv.isCapture()) continue;
      if (fromFile && ((*fromFile) != (static_cast<int>(mv.from()) & 7))) continue;
      if (fromRank && ((*fromRank) != (static_cast<int>(mv.from()) >> 3))) continue;
      matched = mv;
      break;
    }

    if (!matched.has_value()) return std::nullopt;

    const core::Color mover = game.getGameState().sideToMove;
    core::PieceType capturedType = core::PieceType::None;
    if (matched->isCapture()) {
      core::Square captureSq = matched->to();
      if (matched->isEnPassant()) {
        captureSq = (mover == core::Color::White) ? static_cast<core::Square>(captureSq - 8)
                                                  : static_cast<core::Square>(captureSq + 8);
      }
      capturedType = game.getPiece(captureSq).type;
    }

    game.doMove(matched->from(), matched->to(), matched->promotion());

    bool givesCheck = game.isKingInCheck(game.getGameState().sideToMove);
    bool givesMate = false;
    if (givesCheck) {
      const auto &nextLegal = game.generateLegalMoves();
      if (nextLegal.empty()) givesMate = true;
    }

    PgnMove pgnMove;
    pgnMove.move = *matched;
    pgnMove.san = displaySan;
    if (givesMate)
      pgnMove.san.push_back('#');
    else if (givesCheck)
      pgnMove.san.push_back('+');
    pgnMove.mover = mover;
    pgnMove.captured = capturedType;
    pgnMove.gaveCheck = givesCheck;
    pgnMove.gaveMate = givesMate;

    out.moves.push_back(pgnMove);
    out.fenHistory.push_back(game.getFen());
  }

  if (out.fenHistory.empty()) return std::nullopt;
  if (out.fenHistory.size() != out.moves.size() + 1) return std::nullopt;

  return out;
}

}  // namespace lilia::model
