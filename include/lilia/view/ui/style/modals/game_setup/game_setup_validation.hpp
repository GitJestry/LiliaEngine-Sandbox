#pragma once

#include <optional>
#include <string>
#include <vector>

namespace lilia::view::ui::game_setup
{
  // ---------- small helpers ----------
  std::string trim_copy(std::string s);
  std::vector<std::string> split_ws(const std::string &s);
  void strip_crlf(std::string &s);

  // Normalizes spacing and ensures FEN has 6 fields when possible (adds "0 1" if missing).
  std::string normalize_fen(std::string fen);

  // Basic structural validation (fast UI validation).
  std::optional<std::string> validate_fen_basic(const std::string &fenRaw);

  // NEW: normalize + validate + sanitize for playability using PositionBuilder rules.
  // Returns {} if invalid; otherwise returns a normalized + meta-sanitized FEN.
  std::string sanitize_fen_playable(const std::string &fenRaw);

  // Extracts [FEN "..."] tag if present.
  std::optional<std::string> extract_fen_tag(const std::string &pgn);

  struct PgnStatus
  {
    enum class Kind
    {
      Empty,
      OkNoFen,
      OkFen,
      Error
    } kind{Kind::Empty};

    std::optional<std::string> fenFromTag; // sanitized + normalized
  };

  PgnStatus validate_pgn_basic(const std::string &pgnRaw);

  bool looks_like_fen(const std::string &s);
  bool looks_like_pgn(const std::string &s);

} // namespace lilia::view::ui::game_setup
