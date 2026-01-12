#pragma once

#include <optional>
#include <string>
#include <vector>

namespace lilia::view::ui::game_setup
{
  struct PgnStatus
  {
    enum class Kind
    {
      Empty,
      OkFen,
      OkNoFen,
      Error
    };

    Kind kind{Kind::Empty};
    std::optional<std::string> fenFromTag{};
  };

  struct ImportedPgnFile
  {
    std::string filename;
    std::string pgn;
  };

  std::optional<ImportedPgnFile> import_pgn_file(const std::string &path);

  std::string trim_copy(std::string s);
  std::vector<std::string> split_ws(const std::string &s);
  void strip_crlf(std::string &s);

  std::string normalize_fen(std::string fen);
  std::optional<std::string> validate_fen_basic(const std::string &fenRaw);
  std::string sanitize_fen_playable(const std::string &fenRaw);

  std::optional<std::string> extract_fen_tag(const std::string &pgn);
  PgnStatus validate_pgn_basic(const std::string &pgnRaw);

  bool looks_like_fen(const std::string &s);
  bool looks_like_pgn(const std::string &s);

} // namespace lilia::view::ui::game_setup
