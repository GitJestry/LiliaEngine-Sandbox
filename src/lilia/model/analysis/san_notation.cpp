#include "lilia/model/analysis/san_notation.hpp"

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>
#include <vector>

#include "lilia/model/chess_game.hpp"

namespace lilia::model::notation
{
  namespace
  {
    inline char fileChar(core::Square sq) { return char('a' + (int(sq) & 7)); }
    inline char rankChar(core::Square sq) { return char('1' + (int(sq) >> 3)); }

    inline std::string sqStr(core::Square sq)
    {
      std::string s;
      s.push_back(fileChar(sq));
      s.push_back(rankChar(sq));
      return s;
    }

    inline char pieceLetter(core::PieceType pt)
    {
      switch (pt)
      {
      case core::PieceType::Knight:
        return 'N';
      case core::PieceType::Bishop:
        return 'B';
      case core::PieceType::Rook:
        return 'R';
      case core::PieceType::Queen:
        return 'Q';
      case core::PieceType::King:
        return 'K';
      default:
        return '\0'; // pawn/none
      }
    }

    inline std::string trim(std::string_view v)
    {
      std::size_t a = 0, b = v.size();
      while (a < b && std::isspace((unsigned char)v[a]))
        ++a;
      while (b > a && std::isspace((unsigned char)v[b - 1]))
        --b;
      return std::string(v.substr(a, b - a));
    }

    inline std::string normalizeSan(std::string_view in)
    {
      std::string s = trim(in);
      if (s == "0-0")
        s = "O-O";
      if (s == "0-0-0")
        s = "O-O-O";

      // strip trailing annotations and check symbols
      while (!s.empty())
      {
        char c = s.back();
        if (c == '+' || c == '#' || c == '!' || c == '?')
          s.pop_back();
        else
          break;
      }
      return s;
    }

    inline bool sameMove(const model::Move &a, const model::Move &b)
    {
      return a.from() == b.from() &&
             a.to() == b.to() &&
             a.promotion() == b.promotion() &&
             a.castle() == b.castle() &&
             a.isEnPassant() == b.isEnPassant();
    }

    inline model::ChessGame gameFromPosition(const model::Position &pos)
    {
      model::ChessGame g;
      g.getPositionRefForBot() = pos; // requires Position copy-assign
      g.buildHash();
      g.setResult(core::GameResult::ONGOING);
      return g;
    }

    inline bool isUciLike(std::string_view t)
    {
      if (t.size() != 4 && t.size() != 5)
        return false;
      auto in = [&](char c, char lo, char hi)
      { return c >= lo && c <= hi; };
      if (!in(t[0], 'a', 'h') || !in(t[1], '1', '8') || !in(t[2], 'a', 'h') || !in(t[3], '1', '8'))
        return false;
      if (t.size() == 5)
      {
        char p = char(std::tolower((unsigned char)t[4]));
        if (p != 'q' && p != 'r' && p != 'b' && p != 'n')
          return false;
      }
      return true;
    }

    inline core::Square parseSq(std::string_view s, std::size_t i)
    {
      int f = s[i] - 'a';
      int r = s[i + 1] - '1';
      return (core::Square)(r * 8 + f);
    }

    inline core::PieceType parsePromo(char c)
    {
      c = char(std::tolower((unsigned char)c));
      switch (c)
      {
      case 'q':
        return core::PieceType::Queen;
      case 'r':
        return core::PieceType::Rook;
      case 'b':
        return core::PieceType::Bishop;
      case 'n':
        return core::PieceType::Knight;
      default:
        return core::PieceType::None;
      }
    }
  } // namespace

  std::string toSan(const model::Position &pos, const model::Move &mv)
  {
    model::ChessGame g = gameFromPosition(pos);
    const auto &legals = g.generateLegalMoves();

    bool legal = false;
    for (const auto &m : legals)
      if (sameMove(m, mv))
      {
        legal = true;
        break;
      }
    if (!legal)
      return "";

    // Castling
    if (mv.castle() != model::CastleSide::None)
    {
      model::ChessGame after = gameFromPosition(pos);
      after.doMove(mv.from(), mv.to(), mv.promotion());

      const core::Color stmNow = after.getGameState().sideToMove;
      const bool inCheck = after.isKingInCheck(stmNow);

      std::string san = (mv.castle() == model::CastleSide::KingSide) ? "O-O" : "O-O-O";
      if (inCheck)
      {
        const bool mate = after.generateLegalMoves().empty();
        san.push_back(mate ? '#' : '+');
      }
      return san;
    }

    const auto mover = g.getPiece(mv.from());
    const core::PieceType pt = mover.type;
    const bool isPawn = (pt == core::PieceType::Pawn);
    const bool isCap = mv.isCapture();

    std::string san;

    // Piece letter (none for pawn)
    if (!isPawn)
      san.push_back(pieceLetter(pt));

    // Disambiguation for non-pawns
    if (!isPawn)
    {
      const int fromFile = int(mv.from()) & 7;
      const int fromRank = int(mv.from()) >> 3;

      std::vector<core::Square> competitors;
      for (const auto &m : legals)
      {
        if (m.to() != mv.to())
          continue;
        if (m.from() == mv.from())
          continue;

        const auto pc = g.getPiece(m.from());
        if (pc.type == pt && pc.color == mover.color)
          competitors.push_back(m.from());
      }

      if (!competitors.empty())
      {
        bool anySameFile = false;
        bool anySameRank = false;
        for (auto sq : competitors)
        {
          if ((int(sq) & 7) == fromFile)
            anySameFile = true;
          if ((int(sq) >> 3) == fromRank)
            anySameRank = true;
        }

        if (!anySameFile)
          san.push_back(char('a' + fromFile));
        else if (!anySameRank)
          san.push_back(char('1' + fromRank));
        else
        {
          san.push_back(char('a' + fromFile));
          san.push_back(char('1' + fromRank));
        }
      }
    }

    // Capture marker (pawn captures include origin file)
    if (isCap)
    {
      if (isPawn)
        san.push_back(fileChar(mv.from()));
      san.push_back('x');
    }

    san += sqStr(mv.to());

    // Promotion
    if (mv.promotion() != core::PieceType::None)
    {
      san.push_back('=');
      san.push_back(pieceLetter(mv.promotion()));
    }

    // Check / mate suffix
    model::ChessGame after = gameFromPosition(pos);
    after.doMove(mv.from(), mv.to(), mv.promotion());
    const core::Color stmNow = after.getGameState().sideToMove;
    const bool inCheck = after.isKingInCheck(stmNow);
    if (inCheck)
    {
      const bool mate = after.generateLegalMoves().empty();
      san.push_back(mate ? '#' : '+');
    }

    return san;
  }

  bool fromSan(const model::Position &pos, std::string_view sanToken, model::Move &out)
  {
    std::string tok = normalizeSan(sanToken);
    if (tok.empty())
      return false;

    if (tok == "1-0" || tok == "0-1" || tok == "1/2-1/2" || tok == "*")
      return false;

    model::ChessGame g = gameFromPosition(pos);
    const auto &legals = g.generateLegalMoves();

    // UCI-like fallback
    if (isUciLike(tok))
    {
      core::Square from = parseSq(tok, 0);
      core::Square to = parseSq(tok, 2);
      core::PieceType promo = (tok.size() == 5) ? parsePromo(tok[4]) : core::PieceType::None;

      for (const auto &m : legals)
        if (m.from() == from && m.to() == to && m.promotion() == promo)
        {
          out = m;
          return true;
        }
      return false;
    }

    // Robust SAN matching by generation
    for (const auto &m : legals)
    {
      const std::string cand = normalizeSan(toSan(pos, m));
      if (cand == tok)
      {
        out = m;
        return true;
      }
    }
    return false;
  }

} // namespace lilia::model::notation
