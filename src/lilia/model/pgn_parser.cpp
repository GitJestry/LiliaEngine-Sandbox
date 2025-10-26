#include "lilia/model/pgn_parser.hpp"

#include <algorithm>
#include <cctype>
#include <optional>
#include <sstream>

#include "lilia/chess_types.hpp"
#include "lilia/constants.hpp"
#include "lilia/model/chess_game.hpp"
#include "lilia/model/fen_validator.hpp"
#include "lilia/uci/uci_helper.hpp"

namespace lilia::model::pgn {

namespace {

using core::PieceType;
using core::Square;

bool is_result_token(const std::string& token) {
  return token == "1-0" || token == "0-1" || token == "1/2-1/2" || token == "*";
}

bool is_all_digits(const std::string& token) {
  return !token.empty() &&
         std::all_of(token.begin(), token.end(), [](unsigned char c) { return std::isdigit(c) != 0; });
}

bool is_nag(const std::string& token) {
  return !token.empty() && token.front() == '$';
}

void append_sanitized_token(std::string& out, const std::string& token) {
  if (!out.empty()) out.push_back(' ');
  out.append(token);
}

struct SanDescriptor {
  PieceType piece{PieceType::Pawn};
  bool castleKing{false};
  bool castleQueen{false};
  bool capture{false};
  std::optional<int> srcFile;
  std::optional<int> srcRank;
  int dstFile{-1};
  int dstRank{-1};
  PieceType promotion{PieceType::None};
  bool check{false};
  bool mate{false};
};

bool parse_san_descriptor(const std::string& token, SanDescriptor& desc, std::string& error) {
  std::string core = token;
  // Remove trailing annotations like +, #, !, ?
  while (!core.empty()) {
    char back = core.back();
    if (back == '+' || back == '#' || back == '!' || back == '?') {
      if (back == '+') desc.check = true;
      if (back == '#') desc.mate = true;
      core.pop_back();
    } else {
      break;
    }
  }

  if (core == "O-O" || core == "0-0") {
    desc.castleKing = true;
    desc.piece = PieceType::King;
    return true;
  }
  if (core == "O-O-O" || core == "0-0-0") {
    desc.castleQueen = true;
    desc.piece = PieceType::King;
    return true;
  }

  // Remove trailing "e.p." if present
  if (core.size() > 4) {
    std::string lower;
    lower.resize(core.size());
    std::transform(core.begin(), core.end(), lower.begin(), [](unsigned char c) {
      return static_cast<char>(std::tolower(c));
    });
    auto pos = lower.find("e.p.");
    if (pos != std::string::npos) core.erase(pos);
  }

  // Promotion handling
  auto eqPos = core.find('=');
  if (eqPos != std::string::npos && eqPos + 1 < core.size()) {
    char promo = core[eqPos + 1];
    switch (promo) {
      case 'q':
      case 'Q':
        desc.promotion = PieceType::Queen;
        break;
      case 'r':
      case 'R':
        desc.promotion = PieceType::Rook;
        break;
      case 'b':
      case 'B':
        desc.promotion = PieceType::Bishop;
        break;
      case 'n':
      case 'N':
        desc.promotion = PieceType::Knight;
        break;
      default:
        error = "Unsupported promotion piece in PGN.";
        return false;
    }
    core.erase(eqPos);
  }

  if (core.size() < 2) {
    error = "Malformed SAN token.";
    return false;
  }

  char fileChar = core[core.size() - 2];
  char rankChar = core[core.size() - 1];
  if (fileChar < 'a' || fileChar > 'h' || rankChar < '1' || rankChar > '8') {
    error = "Destination square missing in SAN token.";
    return false;
  }
  desc.dstFile = fileChar - 'a';
  desc.dstRank = rankChar - '1';
  core.erase(core.size() - 2, 2);

  desc.piece = PieceType::Pawn;
  std::size_t idx = 0;
  if (!core.empty()) {
    char lead = core[0];
    switch (lead) {
      case 'K':
        desc.piece = PieceType::King;
        idx = 1;
        break;
      case 'Q':
        desc.piece = PieceType::Queen;
        idx = 1;
        break;
      case 'R':
        desc.piece = PieceType::Rook;
        idx = 1;
        break;
      case 'B':
        desc.piece = PieceType::Bishop;
        idx = 1;
        break;
      case 'N':
        desc.piece = PieceType::Knight;
        idx = 1;
        break;
      default:
        desc.piece = PieceType::Pawn;
        break;
    }
  }

  for (; idx < core.size(); ++idx) {
    char c = core[idx];
    if (c == 'x') {
      desc.capture = true;
      continue;
    }
    if (c >= 'a' && c <= 'h') {
      desc.srcFile = c - 'a';
      continue;
    }
    if (c >= '1' && c <= '8') {
      desc.srcRank = c - '1';
      continue;
    }
    // Ignore anything else silently for robustness
  }

  return true;
}

int file_of(Square sq) { return static_cast<int>(sq) & 7; }
int rank_of(Square sq) { return static_cast<int>(sq) >> 3; }

bool analyze_move(const model::ChessGame& game, const model::Move& move, bool& givesCheck, bool& givesMate) {
  model::ChessGame copy = game;
  copy.doMove(move.from(), move.to(), move.promotion());
  auto side = copy.getGameState().sideToMove;
  givesCheck = copy.isKingInCheck(side);
  if (!givesCheck) {
    givesMate = false;
    return true;
  }
  const auto& legal = copy.generateLegalMoves();
  givesMate = legal.empty();
  return true;
}

bool move_matches_descriptor(const SanDescriptor& desc, const model::ChessGame& game,
                             const model::Move& move, bool enforceCheck, bool enforceMate) {
  const auto piece = game.getPiece(move.from());
  if (piece.type == PieceType::None) return false;

  if (desc.castleKing || desc.castleQueen) {
    if (!move.isCastle()) return false;
    if (desc.castleKing && move.castle() != model::CastleSide::KingSide) return false;
    if (desc.castleQueen && move.castle() != model::CastleSide::QueenSide) return false;
    if (enforceCheck || enforceMate) {
      bool givesCheck = false, givesMate = false;
      analyze_move(game, move, givesCheck, givesMate);
      if (enforceCheck && !givesCheck) return false;
      if (enforceMate && !givesMate) return false;
    }
    return true;
  }

  if (piece.type != desc.piece) return false;

  if (desc.dstFile != file_of(move.to()) || desc.dstRank != rank_of(move.to())) return false;

  if (desc.srcFile && *desc.srcFile != file_of(move.from())) return false;
  if (desc.srcRank && *desc.srcRank != rank_of(move.from())) return false;

  if (desc.capture && !(move.isCapture() || move.isEnPassant())) return false;
  if (!desc.capture && (move.isCapture() || move.isEnPassant())) return false;

  if (desc.promotion != PieceType::None && move.promotion() != desc.promotion) return false;
  if (desc.promotion == PieceType::None && move.promotion() != PieceType::None) return false;

  if (enforceCheck || enforceMate) {
    bool givesCheck = false, givesMate = false;
    analyze_move(game, move, givesCheck, givesMate);
    if (enforceCheck && !givesCheck) return false;
    if (enforceMate && !givesMate) return false;
  }

  return true;
}

std::vector<std::string> tokenize(const std::string& text) {
  std::vector<std::string> tokens;
  std::string current;
  bool inBraceComment = false;
  bool inLineComment = false;
  int parenDepth = 0;

  auto flush = [&]() {
    if (!current.empty()) {
      tokens.push_back(current);
      current.clear();
    }
  };

  const auto pushChar = [&](char c) { current.push_back(c); };

  for (std::size_t i = 0; i < text.size(); ++i) {
    char c = text[i];
    if (inBraceComment) {
      if (c == '}') inBraceComment = false;
      continue;
    }
    if (inLineComment) {
      if (c == '\n' || c == '\r') inLineComment = false;
      continue;
    }
    if (c == '{') {
      flush();
      inBraceComment = true;
      continue;
    }
    if (c == ';') {
      flush();
      inLineComment = true;
      continue;
    }
    if (c == '(') {
      ++parenDepth;
      flush();
      continue;
    }
    if (c == ')') {
      if (parenDepth > 0) --parenDepth;
      flush();
      continue;
    }
    if (parenDepth > 0) continue;
    if (std::isspace(static_cast<unsigned char>(c))) {
      flush();
      continue;
    }
    if (c == '.') {
      flush();
      continue;
    }
    if (c == '[') {
      // Tags should have been removed already; just skip until next ']'
      flush();
      while (i < text.size() && text[i] != ']') ++i;
      continue;
    }
    pushChar(c);
  }
  flush();

  // Filter tokens: remove move numbers, results, NAGs etc.
  std::vector<std::string> filtered;
  filtered.reserve(tokens.size());
  for (auto& tok : tokens) {
    if (tok.empty()) continue;
    if (is_result_token(tok)) continue;
    if (is_all_digits(tok)) continue;
    if (is_nag(tok)) continue;
    if (tok == "e.p.") continue;

    // Handle tokens like "2..." or "1...Nc6"
    if (tok.find('...') != std::string::npos) {
      auto pos = tok.find("...");
      if (pos == tok.size() - 3) continue;
      tok = tok.substr(pos + 3);
    }
    auto dotPos = tok.rfind('.');
    if (dotPos != std::string::npos) {
      if (dotPos == tok.size() - 1) continue;
      tok = tok.substr(dotPos + 1);
    }

    filtered.push_back(tok);
  }

  return filtered;
}

}  // namespace

std::optional<ParsedPgn> parse(const std::string& text, std::string* errorOut) {
  ParsedPgn parsed;
  parsed.startFen = core::START_FEN;
  parsed.finalFen = core::START_FEN;
  parsed.sanitized.clear();
  parsed.movesSan.clear();
  parsed.movesUci.clear();

  std::string error;

  std::string withoutTags;
  withoutTags.reserve(text.size());
  bool inTag = false;
  bool setUpTagOne = false;
  bool haveFenTag = false;
  std::string fenTagValue;

  for (std::size_t i = 0; i < text.size(); ++i) {
    char c = text[i];
    if (!inTag && c == '[') {
      inTag = true;
      std::size_t end = text.find(']', i + 1);
      if (end == std::string::npos) {
        if (errorOut) *errorOut = "Unterminated PGN tag.";
        return std::nullopt;
      }
      std::string tagContent = text.substr(i + 1, end - i - 1);
      std::istringstream tagStream(tagContent);
      std::string key;
      tagStream >> key;
      if (!key.empty()) {
        auto quotePos = tagContent.find('"');
        std::string value;
        if (quotePos != std::string::npos) {
          auto quoteEnd = tagContent.find('"', quotePos + 1);
          if (quoteEnd != std::string::npos)
            value = tagContent.substr(quotePos + 1, quoteEnd - quotePos - 1);
        }
        if (key == "SetUp") setUpTagOne = (value == "1");
        if (key == "FEN" && !value.empty()) {
          fenTagValue = value;
          haveFenTag = true;
        }
      }
      i = end;
      inTag = false;
      continue;
    }
    withoutTags.push_back(c);
  }

  if (haveFenTag) {
    if (!model::fen::is_basic_fen_valid(fenTagValue)) {
      if (errorOut) *errorOut = "FEN tag inside PGN is invalid.";
      return std::nullopt;
    }
    parsed.startFen = fenTagValue;
  }

  if (!haveFenTag && setUpTagOne) {
    if (errorOut) *errorOut = "PGN has SetUp=1 but missing FEN tag.";
    return std::nullopt;
  }

  auto tokens = tokenize(withoutTags);
  if (tokens.empty()) {
    if (errorOut) *errorOut = "PGN does not contain any moves.";
    return std::nullopt;
  }

  model::ChessGame game;
  game.setPosition(parsed.startFen);

  const auto appendToken = [&](const std::string& t) { append_sanitized_token(parsed.sanitized, t); };

  for (const auto& tok : tokens) {
    SanDescriptor desc;
    if (!parse_san_descriptor(tok, desc, error)) {
      if (errorOut) *errorOut = error;
      return std::nullopt;
    }

    const auto& legal = game.generateLegalMoves();
    model::Move matched;
    bool found = false;
    bool enforceCheck = desc.check;
    bool enforceMate = desc.mate;

    for (const auto& mv : legal) {
      if (move_matches_descriptor(desc, game, mv, enforceCheck, enforceMate)) {
        if (found) {
          if (errorOut) *errorOut = "Ambiguous SAN move in PGN.";
          return std::nullopt;
        }
        matched = mv;
        found = true;
      }
    }

    if (!found) {
      if (errorOut) *errorOut = "Illegal SAN move in PGN.";
      return std::nullopt;
    }

    parsed.movesSan.push_back(tok);
    parsed.movesUci.push_back(move_to_uci(matched));
    appendToken(tok);

    game.doMove(matched.from(), matched.to(), matched.promotion());
  }

  parsed.finalFen = game.getFen();
  return parsed;
}

bool is_valid(const std::string& text) {
  return parse(text, nullptr).has_value();
}

}  // namespace lilia::model::pgn

