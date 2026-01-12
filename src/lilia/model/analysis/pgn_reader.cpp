#include "lilia/model/analysis/pgn_reader.hpp"

#include <cctype>
#include <string>
#include <vector>

#include "lilia/model/chess_game.hpp"
#include "lilia/model/analysis/san_notation.hpp"

namespace lilia::model::analysis
{
  namespace
  {
    static void stripCommentsAndVariations(std::string &s)
    {
      // Remove {...} comments, ';' to end of line, and (...) variations (single-level robust enough for most PGNs)
      std::string out;
      out.reserve(s.size());

      int brace = 0;
      int paren = 0;
      bool lineComment = false;

      for (std::size_t i = 0; i < s.size(); ++i)
      {
        char c = s[i];

        if (lineComment)
        {
          if (c == '\n' || c == '\r')
            lineComment = false;
          else
            continue;
        }

        if (brace > 0)
        {
          if (c == '{')
            ++brace;
          else if (c == '}')
            --brace;
          continue;
        }

        if (paren > 0)
        {
          if (c == '(')
            ++paren;
          else if (c == ')')
            --paren;
          continue;
        }

        if (c == ';')
        {
          lineComment = true;
          continue;
        }
        if (c == '{')
        {
          brace = 1;
          continue;
        }
        if (c == '(')
        {
          paren = 1;
          continue;
        }

        out.push_back(c);
      }

      s.swap(out);
    }

    static void pushTokenSplittingMoveNumber(std::vector<std::string> &toks, std::string tok)
    {
      if (tok.empty())
        return;

      // Split tokens like "1.e4" or "10...O-O" into "1." + "e4" or "10..." + "O-O"
      std::size_t i = 0;
      while (i < tok.size() && std::isdigit((unsigned char)tok[i]))
        ++i;

      std::size_t j = i;
      while (j < tok.size() && tok[j] == '.')
        ++j;

      const bool hasMoveNoPrefix = (i > 0 && j > i);
      const bool hasTail = (j < tok.size());

      if (hasMoveNoPrefix && hasTail)
      {
        toks.push_back(tok.substr(0, j)); // "1." or "10..."
        toks.push_back(tok.substr(j));    // "e4" or "O-O"
        return;
      }

      toks.push_back(std::move(tok));
    }

    static std::vector<std::string> tokenizeMovetext(const std::string &s)
    {
      std::vector<std::string> toks;
      std::string cur;

      auto flush = [&]
      {
        if (!cur.empty())
        {
          pushTokenSplittingMoveNumber(toks, cur);
          cur.clear();
        }
      };

      for (std::size_t i = 0; i < s.size(); ++i)
      {
        unsigned char c = (unsigned char)s[i];

        if (std::isspace(c))
        {
          flush();
          continue;
        }

        // Properly skip NAGs like $1, $15, etc.
        if (c == '$')
        {
          flush();
          ++i;
          while (i < s.size() && std::isdigit((unsigned char)s[i]))
            ++i;
          --i; // compensate for loop ++i
          continue;
        }

        cur.push_back((char)c);
      }

      flush();
      return toks;
    }

    static bool isMoveNumberToken(const std::string &t)
    {
      std::size_t i = 0;
      while (i < t.size() && std::isdigit((unsigned char)t[i]))
        ++i;
      if (i == 0)
        return false;

      // must be followed by one or more dots, and nothing else
      std::size_t j = i;
      while (j < t.size() && t[j] == '.')
        ++j;

      return (j > i) && (j == t.size());
    }

    static bool isResultToken(const std::string &t)
    {
      return (t == "1-0" || t == "0-1" || t == "1/2-1/2" || t == "*");
    }

    static void parseTags(std::string_view pgn, GameRecord &out)
    {
      // Read [Key "Value"] lines at the top.
      std::size_t i = 0;
      while (i < pgn.size())
      {
        while (i < pgn.size() && (pgn[i] == '\r' || pgn[i] == '\n' || std::isspace((unsigned char)pgn[i])))
          ++i;
        if (i >= pgn.size() || pgn[i] != '[')
          break;

        const std::size_t end = pgn.find(']', i);
        if (end == std::string_view::npos)
          break;

        const std::string_view line = pgn.substr(i + 1, end - i - 1);
        const std::size_t sp = line.find(' ');
        if (sp != std::string_view::npos)
        {
          std::string key(line.substr(0, sp));
          std::string_view rest = line.substr(sp + 1);

          const std::size_t q1 = rest.find('"');
          const std::size_t q2 = rest.rfind('"');
          if (q1 != std::string_view::npos && q2 != std::string_view::npos && q2 > q1)
          {
            std::string val(rest.substr(q1 + 1, q2 - q1 - 1));
            out.tags[key] = val;
          }
        }

        i = end + 1;
      }
    }

    static std::string extractMovetext(std::string_view pgn)
    {
      // Skip tags, return remainder as string.
      std::size_t i = 0;
      while (i < pgn.size())
      {
        while (i < pgn.size() && (pgn[i] == '\r' || pgn[i] == '\n' || std::isspace((unsigned char)pgn[i])))
          ++i;
        if (i < pgn.size() && pgn[i] == '[')
        {
          const std::size_t end = pgn.find(']', i);
          if (end == std::string_view::npos)
            break;
          i = end + 1;
          continue;
        }
        break;
      }
      return std::string(pgn.substr(i));
    }
  } // namespace

  bool parsePgnToRecord(std::string_view pgn, GameRecord &out, std::string *err)
  {
    out = GameRecord{};
    parseTags(pgn, out);

    // start FEN
    auto itFen = out.tags.find("FEN");
    if (itFen != out.tags.end() && !itFen->second.empty())
      out.startFen = itFen->second;

    if (out.startFen.empty())
      out.startFen = core::START_FEN;

    std::string movetext = extractMovetext(pgn);
    stripCommentsAndVariations(movetext);

    // Tokenize
    auto toks = tokenizeMovetext(movetext);

    model::ChessGame g;
    g.setPosition(out.startFen);
    g.setResult(core::GameResult::ONGOING);

    for (const std::string &t : toks)
    {
      if (t.empty())
        continue;
      if (isMoveNumberToken(t))
        continue;

      // Drop common annotation suffixes like "e4!" "Nf3?" etc: fromSan() normalizes too, but keep it clean.
      if (isResultToken(t))
      {
        out.result = t;
        break;
      }

      model::Move mv;
      const model::Position &pos = g.getPositionRefForBot();

      if (!model::notation::fromSan(pos, t, mv))
      {
        if (err)
          *err = "Could not parse SAN token: " + t;
        return false;
      }

      // Apply to validate legality and advance.
      if (!g.doMove(mv.from(), mv.to(), mv.promotion()))
      {
        if (err)
          *err = "Illegal move in PGN: " + t;
        return false;
      }

      PlyRecord pr{};
      pr.move = mv;
      pr.timeAfter = TimeView{}; // optional; keep zero unless you parse %clk
      out.plies.push_back(pr);
    }

    if (out.result.empty())
      out.result = "*";

    return true;
  }
}
