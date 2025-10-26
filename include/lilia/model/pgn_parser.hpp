#pragma once

#include <optional>
#include <string>
#include <vector>

namespace lilia::model::pgn {

struct ParsedPgn {
  std::string sanitized;              // PGN text stripped from comments/variations
  std::string startFen;               // Starting position used for replay
  std::string finalFen;               // Final position after all moves
  std::vector<std::string> movesSan;  // SAN tokens (sanitized)
  std::vector<std::string> movesUci;  // Corresponding UCI moves
};

// Parses the given PGN string. On success, returns a populated ParsedPgn
// containing the sanitized move list and derived positions. On failure,
// returns std::nullopt and, if errorOut is provided, writes a short
// explanation into it.
std::optional<ParsedPgn> parse(const std::string& text, std::string* errorOut = nullptr);

// Convenience helper that simply returns whether the provided PGN can be
// parsed successfully.
bool is_valid(const std::string& text);

}  // namespace lilia::model::pgn

