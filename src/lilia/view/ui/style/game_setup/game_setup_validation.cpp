#include "lilia/view/ui/style/modals/game_setup/game_setup_validation.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
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
    pb::setFromFen(b, m, norm);

    if (!pb::kingsOk(b) || !pb::pawnsOk(b))
      return {};

    return pb::fen(b, m);
  }

  namespace
  {
    static void stripTrailingNewlines(std::string &s)
    {
      while (!s.empty() && (s.back() == '\n' || s.back() == '\r'))
        s.pop_back();
    }

    static void normalizeLineEndings(std::string &s)
    {
      std::string out;
      out.reserve(s.size());

      for (std::size_t i = 0; i < s.size(); ++i)
      {
        const char c = s[i];
        if (c == '\r')
        {
          if (i + 1 < s.size() && s[i + 1] == '\n')
            ++i;
          out.push_back('\n');
        }
        else
        {
          out.push_back(c);
        }
      }
      s.swap(out);
    }

    static std::string readAllTextFile(const std::string &path)
    {
      std::ifstream in(path, std::ios::binary);
      if (!in)
        return {};

      std::ostringstream ss;
      ss << in.rdbuf();
      std::string s = ss.str();

      if (s.size() >= 3 &&
          (static_cast<unsigned char>(s[0]) == 0xEF) &&
          (static_cast<unsigned char>(s[1]) == 0xBB) &&
          (static_cast<unsigned char>(s[2]) == 0xBF))
      {
        s.erase(0, 3);
      }

      return s;
    }

    static std::string filenameOnly(const std::string &path)
    {
      try
      {
        return std::filesystem::path(path).filename().string();
      }
      catch (...)
      {
        return {};
      }
    }

    static bool hasPgnTagHeader(const std::string &pgn)
    {
      std::size_t i = 0;
      while (i < pgn.size() && std::isspace(static_cast<unsigned char>(pgn[i])))
        ++i;
      return (i < pgn.size() && pgn[i] == '[');
    }

    static void rtrimInPlace(std::string &s)
    {
      while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())))
        s.pop_back();
    }

    static std::string firstGameOnly(std::string pgn)
    {
      normalizeLineEndings(pgn);

      const std::size_t firstEvent = pgn.find("[Event ");
      if (firstEvent != std::string::npos)
      {
        const std::size_t nextEvent = pgn.find("[Event ", firstEvent + 1);
        if (nextEvent != std::string::npos)
        {
          std::string cut = pgn.substr(0, nextEvent);
          rtrimInPlace(cut);
          return cut;
        }
      }

      auto isWS = [](char c)
      { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; };

      auto isStandaloneTokenAt = [&](std::size_t pos, const std::string &tok) -> bool
      {
        if (pos + tok.size() > pgn.size())
          return false;
        if (pgn.compare(pos, tok.size(), tok) != 0)
          return false;

        const char prev = (pos == 0) ? ' ' : pgn[pos - 1];
        const char next = (pos + tok.size() >= pgn.size()) ? ' ' : pgn[pos + tok.size()];
        const bool prevOk = isWS(prev) || prev == '(' || prev == '{' || prev == ';';
        const bool nextOk = isWS(next) || next == ')' || next == '}' || next == ';';
        return prevOk && nextOk;
      };

      const std::string tokens[] = {"1-0", "0-1", "1/2-1/2", "*"};
      for (std::size_t i = 0; i < pgn.size(); ++i)
      {
        for (const auto &tok : tokens)
        {
          if (!isStandaloneTokenAt(i, tok))
            continue;

          const std::size_t end = i + tok.size();

          std::size_t j = end;
          while (j < pgn.size() && std::isspace(static_cast<unsigned char>(pgn[j])))
            ++j;

          if (j < pgn.size() && pgn[j] == '[')
          {
            std::string cut = pgn.substr(0, end);
            rtrimInPlace(cut);
            return cut;
          }
        }
      }

      return pgn;
    }

    static std::string decoratePgnForEditor(std::string pgn, const std::string &fileName)
    {
      if (hasPgnTagHeader(pgn))
        return pgn;

      std::ostringstream out;
      out << "[Event \"Imported PGN\"]\n"
          << "[Site \"?\"]\n"
          << "[Date \"????.??.??\"]\n"
          << "[Round \"?\"]\n"
          << "[White \"?\"]\n"
          << "[Black \"?\"]\n"
          << "[Result \"*\"]\n";
      if (!fileName.empty())
        out << "[Annotator \"File: " << fileName << "\"]\n";
      out << "\n";
      out << pgn;
      return out.str();
    }

    static void mergeSplitMoveNumberDigits(std::string &pgn, std::size_t movetextStart)
    {
      for (std::size_t i = movetextStart; i + 3 < pgn.size();)
      {
        if (!std::isdigit(static_cast<unsigned char>(pgn[i])))
        {
          ++i;
          continue;
        }

        std::size_t j = i + 1;
        bool sawWs = false;
        while (j < pgn.size() && (pgn[j] == ' ' || pgn[j] == '\t'))
        {
          sawWs = true;
          ++j;
        }
        if (!sawWs || j >= pgn.size() || !std::isdigit(static_cast<unsigned char>(pgn[j])))
        {
          ++i;
          continue;
        }

        std::size_t k = i;
        while (k < pgn.size() &&
               (std::isdigit(static_cast<unsigned char>(pgn[k])) || pgn[k] == ' ' || pgn[k] == '\t'))
        {
          ++k;
        }
        if (k >= pgn.size() || pgn[k] != '.')
        {
          ++i;
          continue;
        }

        std::string digits;
        digits.reserve(k - i);
        for (std::size_t p = i; p < k; ++p)
        {
          if (std::isdigit(static_cast<unsigned char>(pgn[p])))
            digits.push_back(pgn[p]);
        }

        pgn.replace(i, k - i, digits);
        i += digits.size();
      }
    }

    static void splitGluedSanAndMoveNumbers(std::string &pgn, std::size_t movetextStart)
    {
      for (std::size_t i = movetextStart + 1; i + 2 < pgn.size(); ++i)
      {
        if (!std::isdigit(static_cast<unsigned char>(pgn[i])) ||
            !std::isdigit(static_cast<unsigned char>(pgn[i + 1])))
          continue;

        const char prev = pgn[i - 1];
        if (!std::isalpha(static_cast<unsigned char>(prev)))
          continue;

        if (pgn[i] < '1' || pgn[i] > '8')
          continue;

        std::size_t j = i + 1;
        while (j < pgn.size() && std::isdigit(static_cast<unsigned char>(pgn[j])))
          ++j;

        if (j < pgn.size() && pgn[j] == '.')
        {
          pgn.insert(i + 1, 1, ' ');
          ++i;
        }
      }
    }

    static void insertSpaceBeforeMoveNumberToken(std::string &pgn, std::size_t movetextStart)
    {
      auto isWS = [](char c)
      { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; };

      for (std::size_t i = movetextStart + 1; i < pgn.size(); ++i)
      {
        if (!std::isdigit(static_cast<unsigned char>(pgn[i])))
          continue;

        const char prev = pgn[i - 1];
        if (isWS(prev) || std::isdigit(static_cast<unsigned char>(prev)))
          continue;

        if (prev == '.' || prev == '(' || prev == '{')
          continue;

        std::size_t j = i;
        while (j < pgn.size() && std::isdigit(static_cast<unsigned char>(pgn[j])))
          ++j;

        if (j < pgn.size() && pgn[j] == '.')
        {
          pgn.insert(i, 1, ' ');
          ++i;
        }
      }
    }

    static std::string normalizePgnFormatting(std::string pgn)
    {
      normalizeLineEndings(pgn);

      std::size_t movetextStart = 0;
      {
        std::size_t i = 0;
        while (i < pgn.size() && std::isspace(static_cast<unsigned char>(pgn[i])))
          ++i;

        if (i < pgn.size() && pgn[i] == '[')
        {
          std::vector<std::string> tags;
          std::size_t pos = i;

          while (pos < pgn.size() && pgn[pos] == '[')
          {
            const std::size_t close = pgn.find(']', pos + 1);
            if (close == std::string::npos)
              break;

            tags.emplace_back(pgn.substr(pos, close - pos + 1));

            pos = close + 1;
            while (pos < pgn.size() && (pgn[pos] == ' ' || pgn[pos] == '\t' || pgn[pos] == '\n'))
              ++pos;

            if (pos < pgn.size() && pgn[pos] == '[')
              continue;

            break;
          }

          std::size_t restStart = pos;
          while (restStart < pgn.size() && (pgn[restStart] == ' ' || pgn[restStart] == '\t' || pgn[restStart] == '\n'))
            ++restStart;

          const std::string rest = (restStart < pgn.size()) ? pgn.substr(restStart) : std::string{};

          std::string rebuilt;
          rebuilt.reserve(pgn.size() + tags.size() + 8);

          for (const auto &t : tags)
          {
            rebuilt += t;
            rebuilt += '\n';
          }
          rebuilt += '\n';
          movetextStart = rebuilt.size();
          rebuilt += rest;

          pgn.swap(rebuilt);
        }
        else
        {
          movetextStart = 0;
        }
      }

      if (movetextStart < pgn.size())
      {
        mergeSplitMoveNumberDigits(pgn, movetextStart);
        splitGluedSanAndMoveNumbers(pgn, movetextStart);
        insertSpaceBeforeMoveNumberToken(pgn, movetextStart);
      }

      return pgn;
    }

  } // namespace

  std::optional<ImportedPgnFile> import_pgn_file(const std::string &path)
  {
    ImportedPgnFile out{};
    out.filename = filenameOnly(path);

    std::string pgn = readAllTextFile(path);
    if (pgn.empty())
      return std::nullopt;

    pgn = normalizePgnFormatting(std::move(pgn));
    pgn = firstGameOnly(std::move(pgn));
    pgn = normalizePgnFormatting(std::move(pgn));
    pgn = decoratePgnForEditor(std::move(pgn), out.filename);

    stripTrailingNewlines(pgn);
    out.pgn = std::move(pgn);
    return out;
  }

  static bool parse_tag_pair_line(const std::string &line, std::string &outKey, std::string &outVal)
  {
    std::string s = trim_copy(line);
    if (s.size() < 5)
      return false;
    if (s.front() != '[' || s.back() != ']')
      return false;

    s = s.substr(1, s.size() - 2);
    s = trim_copy(s);

    size_t sp = s.find(' ');
    if (sp == std::string::npos || sp == 0)
      return false;

    std::string key = s.substr(0, sp);
    std::string rest = trim_copy(s.substr(sp + 1));
    if (rest.size() < 2 || rest.front() != '"')
      return false;

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

  std::optional<std::string> extract_fen_tag(const std::string &pgn)
  {
    std::istringstream iss(pgn);
    std::string line;
    while (std::getline(iss, line))
    {
      line = trim_copy(line);
      if (line.empty())
        continue;
      if (line.front() != '[')
        break;

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
    std::string pgn = trim_copy(pgnRaw);

    if (pgn.empty())
    {
      st.kind = PgnStatus::Kind::Empty;
      return st;
    }

    {
      std::istringstream iss(pgn);
      std::string line;
      while (std::getline(iss, line))
      {
        std::string t = trim_copy(line);
        if (t.empty())
          continue;

        if (t.front() != '[')
          break;

        std::string k, v;
        if (!parse_tag_pair_line(t, k, v))
        {
          st.kind = PgnStatus::Kind::Error;
          return st;
        }
      }
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

    st.kind = PgnStatus::Kind::OkNoFen;
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
