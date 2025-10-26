#pragma once

#include <string>

namespace lilia::model::fen {

// Performs a basic structural validation of the provided FEN string.
// The function checks the standard six fields and validates board layout,
// side to move, castling rights, en-passant square as well as halfmove and
// fullmove counters. The check matches the lightweight logic that was
// previously embedded inside the start screen UI.
bool is_basic_fen_valid(const std::string& fen);

}  // namespace lilia::model::fen

