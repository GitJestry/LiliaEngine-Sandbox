#include "lilia/view/ui/style/modals/game_setup/game_setup_validation.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>

// Reuse your PositionBuilder rule engine as the single source of truth.
#include "lilia/view/ui/style/modals/game_setup/position_builder_rules.hpp"

namespace lilia::view::ui::game_setup
{
  std::string trim_copy(std::string s)
  {
    auto notSpace = [](unsigned char c)
    { return !std::isspace(c); };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), notSpace));
    s.erase(std::find_if(s.rbegin(), s.rend(), notSpace).base(), s.end());
    return s;
  }

  std::vector<std::string> split_ws(const std::string &s)
  {
    std::vector<std::string> out;
    std::istringstream iss(s);
    std::string tok;
    while (iss >> tok)
      out.push_back(tok);
    return out;
  }

  void strip_crlf(std::string &s)
  {
    s.erase(std::remove(s.begin(), s.end(), '\r'), s.end());
    s.erase(std::remove(s.begin(), s.end(), '\n'), s.end());
  }

  static bool is_piece_placement_char(char c)
  {
    switch (c)
    {
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
      return true;
    default:
      return false;
    }
  }

  std::string normalize_fen(std::string fen)
  {
    fen = trim_copy(fen);
    if (fen.empty())
      return fen;

    strip_crlf(fen);

    auto parts = split_ws(fen);
    if (parts.size() == 4)
    {
      parts.push_back("0");
      parts.push_back("1");
    }
    else if (parts.size() == 5)
    {
      parts.push_back("1");
    }

    std::ostringstream ss;
    for (size_t i = 0; i < parts.size(); ++i)
    {
      if (i)
        ss << ' ';
      ss << parts[i];
    }
    return ss.str();
  }

  std::optional<std::string> validate_fen_basic(const std::string &fenRaw)
  {
    const std::string fen = normalize_fen(fenRaw);
    auto parts = split_ws(fen);
    if (parts.size() != 6)
      return std::string("needs 6 fields");

    const std::string &placement = parts[0];
    int ranks = 0;
    int fileCount = 0;

    for (char c : placement)
    {
      if (c == '/')
      {
        if (fileCount != 8)
          return std::string("rank not 8");
        ranks += 1;
        fileCount = 0;
        continue;
      }
      if (c >= '1' && c <= '8')
      {
        fileCount += (c - '0');
        continue;
      }
      if (!is_piece_placement_char(c))
        return std::string("bad char");
      fileCount += 1;
      if (fileCount > 8)
        return std::string("rank overflow");
    }

    if (fileCount != 8)
      return std::string("last rank not 8");
    if (ranks != 7)
      return std::string("not 8 ranks");

    if (!(parts[1] == "w" || parts[1] == "b"))
      return std::string("turn not w/b");

    const std::string &cs = parts[2];
    if (cs != "-")
    {
      for (char c : cs)
        if (!(c == 'K' || c == 'Q' || c == 'k' || c == 'q'))
          return std::string("castling invalid");
    }

    const std::string &ep = parts[3];
    if (ep != "-")
    {
      if (ep.size() != 2)
        return std::string("ep invalid");
      if (!(ep[0] >= 'a' && ep[0] <= 'h'))
        return std::string("ep file invalid");

      // Stricter + correct FEN convention: EP target must be on rank 3 or 6.
      if (!(ep[1] == '3' || ep[1] == '6'))
        return std::string("ep rank invalid");
    }

    // halfmove/fullmove: permissive
    return std::nullopt;
  }

  std::string sanitize_fen_playable(const std::string &fenRaw)
  {
    const std::string norm = normalize_fen(fenRaw);
    if (trim_copy(norm).empty())
      return {};

    if (validate_fen_basic(norm).has_value())
      return {};

    pb::Board b{};
    pb::FenMeta m{};
    pb::setFromFen(b, m, norm); // parses + pb::sanitizeMeta(b,m)
    return pb::fen(b, m);       // canonical output (incl. EP/castling corrected)
  }

  std::optional<std::string> extract_fen_tag(const std::string &pgn)
  {
    const std::string key = "[FEN \"";
    const size_t pos = pgn.find(key);
    if (pos == std::string::npos)
      return std::nullopt;

    const size_t start = pos + key.size();
    const size_t end = pgn.find("\"]", start);
    if (end == std::string::npos)
      return std::nullopt;

    return pgn.substr(start, end - start);
  }

  PgnStatus validate_pgn_basic(const std::string &pgnRaw)
  {
    PgnStatus st{};
    const std::string pgn = trim_copy(pgnRaw);

    if (pgn.empty())
    {
      st.kind = PgnStatus::Kind::Empty;
      return st;
    }

    if (auto fen = extract_fen_tag(pgn))
    {
      const std::string sanitized = sanitize_fen_playable(*fen);
      if (sanitized.empty())
      {
        st.kind = PgnStatus::Kind::Error;
        return st;
      }
      st.kind = PgnStatus::Kind::OkFen;
      st.fenFromTag = sanitized;
      return st;
    }

    const bool looksLikeMoves =
        (pgn.find("1.") != std::string::npos) || (pgn.find("...") != std::string::npos);

    const bool hasResult =
        (pgn.find("1-0") != std::string::npos) || (pgn.find("0-1") != std::string::npos) ||
        (pgn.find("1/2-1/2") != std::string::npos);

    st.kind = (looksLikeMoves || hasResult) ? PgnStatus::Kind::OkNoFen : PgnStatus::Kind::Error;
    return st;
  }

  bool looks_like_fen(const std::string &s)
  {
    const std::string t = normalize_fen(s);
    if (t.empty())
      return false;

    const bool hasSlashes = (t.find('/') != std::string::npos);
    int spaces = 0;
    for (char c : t)
      spaces += (c == ' ');
    return hasSlashes && spaces >= 3;
  }

  bool looks_like_pgn(const std::string &s)
  {
    const std::string t = trim_copy(s);
    if (t.empty())
      return false;

    if (t.find("[Event") != std::string::npos)
      return true;
    if (t.find("1.") != std::string::npos)
      return true;
    if (t.find("1-0") != std::string::npos || t.find("0-1") != std::string::npos ||
        t.find("1/2-1/2") != std::string::npos)
      return true;
    return false;
  }

} // namespace lilia::view::ui::game_setup
