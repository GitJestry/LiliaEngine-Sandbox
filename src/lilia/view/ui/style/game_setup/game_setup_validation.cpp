#include "lilia/view/ui/style/modals/game_setup/game_setup_validation.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>

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
    return pb::validateFenBasic(fen);
  }

  std::string sanitize_fen_playable(const std::string &fenRaw)
  {
    const std::string norm = normalize_fen(fenRaw);
    if (trim_copy(norm).empty())
      return {};

    if (pb::validateFenBasic(norm).has_value())
      return {};

    pb::Board b{};
    pb::FenMeta m{};
    pb::setFromFen(b, m, norm); // parses + pb::sanitizeMeta(b,m)

    // "playable" means your builder constraints apply:
    if (!pb::kingsOk(b) || !pb::pawnsOk(b))
      return {};

    return pb::fen(b, m);
  }

  // ---------- PGN helpers (basic but materially improved) ----------

  static bool parse_tag_pair_line(const std::string &line, std::string &outKey, std::string &outVal)
  {
    // Accept: [Key "Value"]
    // Very small, strict parser (no escapes).
    std::string s = trim_copy(line);
    if (s.size() < 5)
      return false;
    if (s.front() != '[' || s.back() != ']')
      return false;

    s = s.substr(1, s.size() - 2); // inside brackets
    s = trim_copy(s);

    // key until space
    size_t sp = s.find(' ');
    if (sp == std::string::npos || sp == 0)
      return false;

    std::string key = s.substr(0, sp);
    std::string rest = trim_copy(s.substr(sp + 1));
    if (rest.size() < 2 || rest.front() != '"')
      return false;

    // find closing quote
    size_t qend = rest.find('"', 1);
    if (qend == std::string::npos)
      return false;

    std::string val = rest.substr(1, qend - 1);
    std::string tail = trim_copy(rest.substr(qend + 1));
    if (!tail.empty())
      return false;

    outKey = key;
    outVal = val;
    return true;
  }

  static std::string strip_brace_comments(std::string s)
  {
    // Remove {...} (non-nested, common in PGN)
    std::string out;
    out.reserve(s.size());
    bool in = false;
    for (char c : s)
    {
      if (!in && c == '{')
      {
        in = true;
        continue;
      }
      if (in && c == '}')
      {
        in = false;
        continue;
      }
      if (!in)
        out.push_back(c);
    }
    return out;
  }

  static std::string strip_semicolon_comments(std::string s)
  {
    // Remove ';' to end-of-line comments
    std::string out;
    out.reserve(s.size());
    bool inComment = false;
    for (size_t i = 0; i < s.size(); ++i)
    {
      char c = s[i];
      if (!inComment && c == ';')
      {
        inComment = true;
        continue;
      }
      if (inComment && c == '\n')
      {
        inComment = false;
        out.push_back(c);
        continue;
      }
      if (!inComment)
        out.push_back(c);
    }
    return out;
  }

  static std::string strip_variations(std::string s)
  {
    // Remove (...) variations (supports nesting)
    std::string out;
    out.reserve(s.size());
    int depth = 0;
    for (char c : s)
    {
      if (c == '(')
      {
        ++depth;
        continue;
      }
      if (c == ')')
      {
        if (depth > 0)
          --depth;
        continue;
      }
      if (depth == 0)
        out.push_back(c);
    }
    return out;
  }

  static bool is_result_token(const std::string &tok)
  {
    return tok == "1-0" || tok == "0-1" || tok == "1/2-1/2" || tok == "*";
  }

  static bool is_move_number_token(const std::string &tok)
  {
    // Examples: "1." or "23..." or "7.."
    if (tok.empty())
      return false;

    size_t i = 0;
    while (i < tok.size() && std::isdigit(static_cast<unsigned char>(tok[i])))
      ++i;
    if (i == 0)
      return false;

    // Must be followed by '.' one or more
    size_t dots = 0;
    while (i < tok.size() && tok[i] == '.')
    {
      ++dots;
      ++i;
    }
    if (dots == 0)
      return false;

    // No other chars
    return i == tok.size();
  }

  static bool looks_like_san(std::string tok)
  {
    tok = trim_copy(tok);
    if (tok.empty())
      return false;

    // Drop NAG tokens like $1
    if (!tok.empty() && tok[0] == '$')
      return false;

    // Strip trailing annotation/check markers: ! ? + #
    while (!tok.empty())
    {
      char c = tok.back();
      if (c == '!' || c == '?' || c == '+' || c == '#')
        tok.pop_back();
      else
        break;
    }
    if (tok.empty())
      return false;

    // Strip optional e.p. suffix
    auto ends_with = [](const std::string &s, const std::string &suffix)
    {
      return s.size() >= suffix.size() &&
             s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
    };
    if (ends_with(tok, "e.p."))
      tok.resize(tok.size() - 4);
    else if (ends_with(tok, "ep"))
      tok.resize(tok.size() - 2);

    tok = trim_copy(tok);
    if (tok.empty())
      return false;

    // Castling
    if (tok == "O-O" || tok == "O-O-O")
      return true;

    auto isFile = [](char c)
    { return c >= 'a' && c <= 'h'; };
    auto isRank = [](char c)
    { return c >= '1' && c <= '8'; };
    auto isPiece = [](char c)
    { return c == 'K' || c == 'Q' || c == 'R' || c == 'B' || c == 'N'; };
    auto isPromo = [](char c)
    { return c == 'Q' || c == 'R' || c == 'B' || c == 'N'; };

    // Handle promotion suffix: e8=Q or e8Q
    std::string core = tok;
    if (core.size() >= 4 && core[core.size() - 2] == '=' && isPromo(core.back()))
    {
      core = core.substr(0, core.size() - 2);
    }
    else if (core.size() >= 3 && isPromo(core.back()))
    {
      // allow e8Q (non-standard but seen)
      char df = core[core.size() - 3];
      char dr = core[core.size() - 2];
      if (isFile(df) && isRank(dr))
        core = core.substr(0, core.size() - 1);
    }

    if (core.size() < 2)
      return false;

    // Destination square must be at end of core
    const char destFile = core[core.size() - 2];
    const char destRank = core[core.size() - 1];
    if (!isFile(destFile) || !isRank(destRank))
      return false;

    std::string pre = core.substr(0, core.size() - 2);

    // Optional capture 'x' in pre
    bool capture = false;
    size_t xPos = pre.find('x');
    if (xPos != std::string::npos)
    {
      capture = true;
      // must be only one 'x'
      if (pre.find('x', xPos + 1) != std::string::npos)
        return false;
      pre.erase(xPos, 1);
    }

    if (pre.empty())
    {
      // Pawn push (e4)
      return true;
    }

    if (isPiece(pre.front()))
    {
      // Piece move: [Piece][disambig]{capture}dest
      std::string dis = pre.substr(1);
      if (dis.size() > 2)
        return false;
      for (char c : dis)
      {
        if (!isFile(c) && !isRank(c))
          return false;
      }
      (void)capture;
      return true;
    }

    // Pawn capture must have origin file only: exd5 => after removing 'x', pre == "e"
    if (pre.size() == 1 && isFile(pre[0]))
      return capture; // must be a capture if origin file present

    return false;
  }

  std::optional<std::string> extract_fen_tag(const std::string &pgn)
  {
    // Prefer tag-pair parsing over substring search.
    std::istringstream iss(pgn);
    std::string line;
    while (std::getline(iss, line))
    {
      line = trim_copy(line);
      if (line.empty())
        continue;
      if (line.front() != '[')
        break; // done with tag section

      std::string k, v;
      if (!parse_tag_pair_line(line, k, v))
        continue;

      if (k == "FEN")
        return v;
    }
    return std::nullopt;
  }

  PgnStatus validate_pgn_basic(const std::string &pgnRaw)
  {
    PgnStatus st{};
    std::string pgn = pgnRaw;
    pgn = trim_copy(pgn);

    if (pgn.empty())
    {
      st.kind = PgnStatus::Kind::Empty;
      return st;
    }

    // Validate tag-pair section if present; extract FEN if present.
    {
      std::istringstream iss(pgn);
      std::string line;
      bool sawAnyTag = false;
      while (std::getline(iss, line))
      {
        std::string t = trim_copy(line);
        if (t.empty())
          continue;

        if (t.front() != '[')
          break;

        sawAnyTag = true;
        std::string k, v;
        if (!parse_tag_pair_line(t, k, v))
        {
          st.kind = PgnStatus::Kind::Error;
          return st;
        }
      }
      (void)sawAnyTag;
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

    // Basic movetext plausibility:
    // - strip comments/variations
    // - require at least one plausible SAN/castle move OR a valid result token.
    std::string movetext = pgn;
    movetext = strip_semicolon_comments(movetext);
    movetext = strip_brace_comments(movetext);
    movetext = strip_variations(movetext);

    // Remove tag lines
    {
      std::istringstream iss(movetext);
      std::ostringstream oss;
      std::string line;
      bool inTags = true;
      while (std::getline(iss, line))
      {
        std::string t = trim_copy(line);
        if (inTags && !t.empty() && t.front() == '[')
          continue;
        inTags = false;
        oss << line << '\n';
      }
      movetext = oss.str();
    }

    movetext = trim_copy(movetext);
    if (movetext.empty())
    {
      // PGN with only tags is acceptable in this "basic" validator.
      st.kind = PgnStatus::Kind::OkNoFen;
      return st;
    }

    auto toks = split_ws(movetext);

    bool hasResult = false;
    int sanMoves = 0;

    for (const auto &tok : toks)
    {
      if (is_result_token(tok))
      {
        hasResult = true;
        continue;
      }
      if (is_move_number_token(tok))
        continue;

      // ignore NAGs: $<digits>
      if (!tok.empty() && tok[0] == '$')
        continue;

      if (looks_like_san(tok))
        ++sanMoves;
    }

    if (sanMoves > 0 || hasResult)
    {
      st.kind = PgnStatus::Kind::OkNoFen;
      return st;
    }

    st.kind = PgnStatus::Kind::Error;
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
