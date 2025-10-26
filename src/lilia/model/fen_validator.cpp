#include "lilia/model/fen_validator.hpp"

#include <cctype>
#include <sstream>

namespace lilia::model::fen {

namespace {

bool is_non_negative_int(const std::string& s) {
  if (s.empty()) return false;
  for (char c : s) {
    if (!std::isdigit(static_cast<unsigned char>(c))) return false;
  }
  return true;
}

}  // namespace

bool is_basic_fen_valid(const std::string& fen) {
  std::istringstream ss(fen);
  std::string fields[6];
  for (int i = 0; i < 6; ++i) {
    if (!(ss >> fields[i])) return false;
  }

  std::string extra;
  if (ss >> extra) return false;

  // Board layout
  int rankCount = 0;
  int i = 0;
  const std::string& board = fields[0];
  while (i < static_cast<int>(board.size())) {
    int fileSum = 0;
    while (i < static_cast<int>(board.size()) && board[i] != '/') {
      char c = board[i++];
      if (std::isdigit(static_cast<unsigned char>(c))) {
        int n = c - '0';
        if (n <= 0 || n > 8) return false;
        fileSum += n;
      } else {
        switch (c) {
          case 'p':
          case 'r':
          case 'n':
          case 'b':
          case 'q':
          case 'k':
          case 'P':
          case 'R':
          case 'N':
          case 'B':
          case 'Q':
          case 'K':
            fileSum += 1;
            break;
          default:
            return false;
        }
      }
      if (fileSum > 8) return false;
    }
    if (fileSum != 8) return false;
    ++rankCount;
    if (i < static_cast<int>(board.size()) && board[i] == '/') ++i;
  }
  if (rankCount != 8) return false;

  // Side to move
  if (!(fields[1] == "w" || fields[1] == "b")) return false;

  // Castling rights
  if (!(fields[2] == "-" || fields[2].find_first_not_of("KQkq") == std::string::npos))
    return false;

  // En-passant square
  if (!(fields[3] == "-")) {
    if (fields[3].size() != 2) return false;
    if (fields[3][0] < 'a' || fields[3][0] > 'h') return false;
    if (!(fields[3][1] == '3' || fields[3][1] == '6')) return false;
  }

  // Halfmove and fullmove counters
  if (!is_non_negative_int(fields[4])) return false;
  if (!is_non_negative_int(fields[5])) return false;
  if (std::stoi(fields[5]) <= 0) return false;

  return true;
}

}  // namespace lilia::model::fen

