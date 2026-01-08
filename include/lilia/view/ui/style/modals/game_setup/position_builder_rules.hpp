#pragma once

#include <array>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace lilia::view::pb
{
  using Board = std::array<std::array<char, 8>, 8>;

  struct FenMeta
  {
    char sideToMove{'w'}; // 'w' or 'b'

    // Castling rights (builder constrains these to "structurally possible": king/rook on start squares).
    bool castleK{true};
    bool castleQ{true};
    bool castlek{true};
    bool castleq{true};

    // En-passant target square (FEN field 4). Empty = '-'.
    std::optional<std::pair<int, int>> epTarget{};

    int halfmove{0};
    int fullmove{1};
  };

  // --- basic helpers ---
  bool inBounds(int x, int y);
  std::string squareName(int x, int y); // x,y -> "e4"
  bool parseSquareName(const std::string &s, int &x, int &y);

  // --- king counting / constraints ---
  void countKings(const Board &b, int &whiteKings, int &blackKings);
  bool kingsOk(const Board &b);

  // --- castling / en-passant validation ---
  bool hasCastleStructure(const Board &b, bool white, bool kingSide);
  bool isValidEnPassantTarget(const Board &b, int x, int y, char sideToMove);

  // Sanitizes meta to ensure it is consistent with the board (as requested).
  void sanitizeMeta(const Board &b, FenMeta &m);

  // --- FEN encode/decode ---
  std::string placementToFen(const Board &b);
  std::string castlingString(const FenMeta &m);
  std::string epString(const FenMeta &m);
  std::string fen(const Board &b, const FenMeta &m);

  // Robust parse; clears board first; leaves invalid meta in safe state; then sanitizes.
  void setFromFen(Board &b, FenMeta &m, const std::string &fenStr);

  // Convenience: fill board with '.'.
  void clearBoard(Board &b);

} // namespace lilia::view::pb
