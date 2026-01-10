#include "lilia/view/ui/style/modals/game_setup/position_builder_rules.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace lilia::view::pb
{
  bool inBounds(int x, int y) { return x >= 0 && x < 8 && y >= 0 && y < 8; }

  static char at(const Board &b, int x, int y) { return b[y][x]; }

  std::string squareName(int x, int y)
  {
    // y=0 is rank 8, y=7 is rank 1
    const char file = char('a' + x);
    const char rank = char('8' - y);
    return std::string() + file + rank;
  }

  bool parseSquareName(const std::string &s, int &x, int &y)
  {
    if (s.size() != 2)
      return false;
    const char f = s[0];
    const char r = s[1];
    if (f < 'a' || f > 'h')
      return false;
    if (r < '1' || r > '8')
      return false;
    x = int(f - 'a');
    y = int('8' - r);
    return inBounds(x, y);
  }

  void clearBoard(Board &b)
  {
    for (auto &row : b)
      row.fill('.');
  }

  void countKings(const Board &b, int &whiteKings, int &blackKings)
  {
    int wk = 0, bk = 0;
    for (int y = 0; y < 8; ++y)
      for (int x = 0; x < 8; ++x)
      {
        const char p = at(b, x, y);
        if (p == 'K')
          ++wk;
        else if (p == 'k')
          ++bk;
      }
    whiteKings = wk;
    blackKings = bk;
  }

  bool kingsOk(const Board &b)
  {
    int wk = 0, bk = 0;
    countKings(b, wk, bk);
    return wk == 1 && bk == 1;
  }

  bool pawnsOk(const Board &b)
  {
    auto isPawn = [](char c)
    { return c == 'P' || c == 'p'; };

    // rank 8 (y=0) and rank 1 (y=7)
    for (int x = 0; x < 8; ++x)
    {
      if (isPawn(at(b, x, 0)))
        return false;
      if (isPawn(at(b, x, 7)))
        return false;
    }
    return true;
  }

  PlacementFailReason validateSetPiece(const Board &b, int x, int y, char newP)
  {
    if (!inBounds(x, y))
      return PlacementFailReason::None;

    if (newP == '.')
      return PlacementFailReason::None;

    // Pawn on last rank is never allowed in the builder.
    if ((newP == 'P' || newP == 'p') && (y == 0 || y == 7))
      return PlacementFailReason::PawnOnLastRank;

    // King uniqueness: cannot have two kings of same color.
    if (newP == 'K' || newP == 'k')
    {
      const char old = at(b, x, y);
      if (old == newP)
        return PlacementFailReason::None;

      int count = 0;
      for (int yy = 0; yy < 8; ++yy)
        for (int xx = 0; xx < 8; ++xx)
        {
          if (xx == x && yy == y)
            continue;
          if (at(b, xx, yy) == newP)
            ++count;
        }

      if (count >= 1)
        return PlacementFailReason::KingUniqueness;
    }

    return PlacementFailReason::None;
  }

  bool hasCastleStructure(const Board &b, bool white, bool kingSide)
  {
    const int y = white ? 7 : 0;
    const char K = white ? 'K' : 'k';
    const char R = white ? 'R' : 'r';

    if (at(b, 4, y) != K)
      return false;
    const int rookX = kingSide ? 7 : 0;
    return at(b, rookX, y) == R;
  }

  bool isValidEnPassantTarget(const Board &b, int x, int y, char sideToMove)
  {
    if (!inBounds(x, y))
      return false;
    if (at(b, x, y) != '.')
      return false;

    const bool stmWhite = (sideToMove == 'w');
    const int requiredY = stmWhite ? 2 : 5;
    if (y != requiredY)
      return false;

    const int pawnY = y + (stmWhite ? 1 : -1);
    if (!inBounds(x, pawnY))
      return false;

    const char movedPawn = stmWhite ? 'p' : 'P';
    if (at(b, x, pawnY) != movedPawn)
      return false;

    const char captPawn = stmWhite ? 'P' : 'p';
    bool adj = false;
    if (x > 0 && at(b, x - 1, pawnY) == captPawn)
      adj = true;
    if (x < 7 && at(b, x + 1, pawnY) == captPawn)
      adj = true;

    return adj;
  }

  void sanitizeMeta(const Board &b, FenMeta &m)
  {
    if (m.sideToMove != 'w' && m.sideToMove != 'b')
      m.sideToMove = 'w';

    m.halfmove = std::max(0, m.halfmove);
    m.fullmove = std::max(1, m.fullmove);

    if (m.castleK && !hasCastleStructure(b, true, true))
      m.castleK = false;
    if (m.castleQ && !hasCastleStructure(b, true, false))
      m.castleQ = false;
    if (m.castlek && !hasCastleStructure(b, false, true))
      m.castlek = false;
    if (m.castleq && !hasCastleStructure(b, false, false))
      m.castleq = false;

    if (m.epTarget)
    {
      auto [ex, ey] = *m.epTarget;
      if (!isValidEnPassantTarget(b, ex, ey, m.sideToMove))
        m.epTarget.reset();
    }
  }

  std::string placementToFen(const Board &b)
  {
    std::string out;
    out.reserve(64);

    for (int y = 0; y < 8; ++y)
    {
      int empties = 0;
      for (int x = 0; x < 8; ++x)
      {
        const char p = at(b, x, y);
        if (p == '.')
        {
          ++empties;
          continue;
        }
        if (empties)
        {
          out.push_back(char('0' + empties));
          empties = 0;
        }
        out.push_back(p);
      }
      if (empties)
        out.push_back(char('0' + empties));
      if (y != 7)
        out.push_back('/');
    }

    return out;
  }

  std::string castlingString(const FenMeta &m)
  {
    std::string s;
    if (m.castleK)
      s.push_back('K');
    if (m.castleQ)
      s.push_back('Q');
    if (m.castlek)
      s.push_back('k');
    if (m.castleq)
      s.push_back('q');
    return s.empty() ? std::string("-") : s;
  }

  std::string epString(const FenMeta &m)
  {
    if (!m.epTarget)
      return "-";
    auto [x, y] = *m.epTarget;
    if (!inBounds(x, y))
      return "-";
    return squareName(x, y);
  }

  std::string fen(const Board &b, const FenMeta &m)
  {
    return placementToFen(b) + " " + std::string(1, m.sideToMove) + " " +
           castlingString(m) + " " + epString(m) + " " +
           std::to_string(m.halfmove) + " " + std::to_string(m.fullmove);
  }

  static std::vector<std::string> split_ws(const std::string &s)
  {
    std::vector<std::string> out;
    std::istringstream iss(s);
    std::string tok;
    while (iss >> tok)
      out.push_back(tok);
    return out;
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

  static std::vector<std::string> split_char(const std::string &s, char delim)
  {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s)
    {
      if (c == delim)
      {
        out.push_back(cur);
        cur.clear();
      }
      else
      {
        cur.push_back(c);
      }
    }
    out.push_back(cur);
    return out;
  }

  std::optional<std::string> validateFenBasic(const std::string &fen)
  {
    const auto parts = split_ws(fen);
    if (parts.size() != 6)
      return std::string("needs 6 fields");

    const std::string &placement = parts[0];

    // placement ranks
    const auto ranks = split_char(placement, '/');
    if (ranks.size() != 8)
      return std::string("not 8 ranks");

    // builder rule: no pawns on rank 8 or rank 1
    auto hasPawn = [](const std::string &rankStr)
    {
      return rankStr.find('P') != std::string::npos || rankStr.find('p') != std::string::npos;
    };
    if (hasPawn(ranks.front()) || hasPawn(ranks.back()))
      return std::string("pawn on last rank");

    // structural rank validation
    for (const auto &r : ranks)
    {
      int fileCount = 0;
      for (char c : r)
      {
        if (c >= '1' && c <= '8')
        {
          fileCount += (c - '0');
        }
        else
        {
          if (!is_piece_placement_char(c))
            return std::string("bad char");
          fileCount += 1;
        }

        if (fileCount > 8)
          return std::string("rank overflow");
      }

      if (fileCount != 8)
        return std::string("rank not 8");
    }

    // side to move
    if (!(parts[1] == "w" || parts[1] == "b"))
      return std::string("turn not w/b");

    // castling field characters
    const std::string &cs = parts[2];
    if (cs != "-")
    {
      for (char c : cs)
      {
        if (!(c == 'K' || c == 'Q' || c == 'k' || c == 'q'))
          return std::string("castling invalid");
      }
    }

    // ep field shape
    const std::string &ep = parts[3];
    if (ep != "-")
    {
      if (ep.size() != 2)
        return std::string("ep invalid");
      if (!(ep[0] >= 'a' && ep[0] <= 'h'))
        return std::string("ep file invalid");
      if (!(ep[1] == '3' || ep[1] == '6'))
        return std::string("ep rank invalid");
    }

    // halfmove/fullmove: intentionally permissive here (sanitizeMeta clamps later)
    return std::nullopt;
  }

  void setFromFen(Board &b, FenMeta &m, const std::string &fenStr)
  {
    clearBoard(b);

    m.sideToMove = 'w';
    m.castleK = m.castleQ = m.castlek = m.castleq = false;
    m.epTarget.reset();
    m.halfmove = 0;
    m.fullmove = 1;

    const auto parts = split_ws(fenStr);
    const std::string placement = parts.empty() ? fenStr : parts[0];

    int x = 0, y = 0;
    for (char c : placement)
    {
      if (c == '/')
      {
        ++y;
        x = 0;
        continue;
      }
      if (std::isdigit(static_cast<unsigned char>(c)))
      {
        x += (c - '0');
        continue;
      }
      if (inBounds(x, y))
      {
        b[y][x] = c;
        ++x;
      }
    }

    if (parts.size() >= 2 && (parts[1] == "w" || parts[1] == "b"))
      m.sideToMove = parts[1][0];

    if (parts.size() >= 3)
    {
      if (parts[2] != "-")
      {
        for (char c : parts[2])
        {
          if (c == 'K')
            m.castleK = true;
          else if (c == 'Q')
            m.castleQ = true;
          else if (c == 'k')
            m.castlek = true;
          else if (c == 'q')
            m.castleq = true;
        }
      }
    }

    if (parts.size() >= 4)
    {
      if (parts[3] != "-")
      {
        int ex = 0, ey = 0;
        if (parseSquareName(parts[3], ex, ey))
          m.epTarget = std::make_pair(ex, ey);
      }
    }

    if (parts.size() >= 5)
    {
      try
      {
        m.halfmove = std::max(0, std::stoi(parts[4]));
      }
      catch (...)
      {
        m.halfmove = 0;
      }
    }
    if (parts.size() >= 6)
    {
      try
      {
        m.fullmove = std::max(1, std::stoi(parts[5]));
      }
      catch (...)
      {
        m.fullmove = 1;
      }
    }

    sanitizeMeta(b, m);
  }

} // namespace lilia::view::pb
