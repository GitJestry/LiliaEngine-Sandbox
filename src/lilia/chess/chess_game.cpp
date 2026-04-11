#include "lilia/chess/chess_game.hpp"

#include <cctype>
#include <optional>
#include <string_view>

#include "lilia/chess/move_helper.hpp"
#include "lilia/protocol/uci/uci_helper.hpp"

namespace lilia::chess
{
  namespace
  {
    LILIA_ALWAYS_INLINE bool ep_square_is_canonical(const Board &board,
                                                    const GameState &st) noexcept
    {
      const Square ep = st.enPassantSquare;
      if (ep == NO_SQUARE)
        return true;

      const int epIdx = static_cast<int>(ep);
      const Color stm = st.sideToMove;
      const Color them = ~stm;

      if (board.getPiece(ep).has_value())
        return false;

      const int rank = bb::rank_of(ep);
      if (stm == Color::White)
      {
        if (rank != 5)
          return false;
      }
      else
      {
        if (rank != 2)
          return false;
      }

      const Square capturedPawnSq =
          (stm == Color::White) ? static_cast<Square>(epIdx - 8)
                                : static_cast<Square>(epIdx + 8);

      const auto capturedPawn = board.getPiece(capturedPawnSq);
      if (!capturedPawn || capturedPawn->color != them || capturedPawn->type != PieceType::Pawn)
        return false;

      const bb::Bitboard pawnsSTM = board.getPieces(stm, PieceType::Pawn);
      return (pawnsSTM & Zobrist::epCaptureMask[bb::ci(stm)][epIdx]) != 0ULL;
    }

    LILIA_ALWAYS_INLINE void canonicalize_ep_square(Position &pos) noexcept
    {
      auto &st = pos.getState();
      if (st.enPassantSquare == NO_SQUARE)
        return;

      if (!ep_square_is_canonical(pos.getBoard(), st))
        st.enPassantSquare = NO_SQUARE;
    }
  }

  ChessGame::ChessGame()
  {
    m_pseudo_moves.reserve(256);
    m_legal_moves.reserve(256);
  }

  bool ChessGame::doMoveUCI(const std::string &uciMove)
  {
    const size_t len = uciMove.size();
    if (len < 4)
      return false;

    const char *ptr = uciMove.c_str();
    const int from = protocol::uci::squareFromUCI(ptr + 0);
    const int to = protocol::uci::squareFromUCI(ptr + 2);
    if (from < 0 || to < 0)
      return false;

    PieceType promo = PieceType::None;
    if (len >= 5)
    {
      switch (protocol::uci::tolower_ascii(uciMove[4]))
      {
      case 'q':
        promo = PieceType::Queen;
        break;
      case 'r':
        promo = PieceType::Rook;
        break;
      case 'b':
        promo = PieceType::Bishop;
        break;
      case 'n':
        promo = PieceType::Knight;
        break;
      default:
        promo = PieceType::None;
        break;
      }
    }

    return doMove(static_cast<Square>(from), static_cast<Square>(to), promo);
  }

  std::optional<Move> ChessGame::getMove(Square from, Square to)
  {
    const auto &moves = generateLegalMoves();
    for (const auto &m : moves)
    {
      if (m.from() == from && m.to() == to)
        return m;
    }
    return std::nullopt;
  }

  void ChessGame::setPosition(const std::string &fen)
  {
    m_position = Position{};
    m_result = GameResult::Ongoing;
    m_pseudo_moves.clear();
    m_legal_moves.clear();
    m_stateHistory.clear();

    std::string_view sv{fen};
    std::string_view fields[6]{};
    for (int i = 0; i < 6; ++i)
    {
      const size_t sp = sv.find(' ');
      if (sp == std::string_view::npos)
      {
        fields[i] = sv;
        for (++i; i < 6; ++i)
          fields[i] = {};
        break;
      }
      fields[i] = sv.substr(0, sp);
      sv.remove_prefix(sp + 1);
    }

    const std::string_view board = fields[0];
    const std::string_view activeColor = fields[1];
    const std::string_view castling = fields[2];
    const std::string_view enPassant = fields[3];
    const std::string_view halfmoveClock = fields[4];
    const std::string_view fullmoveNumber = fields[5];

    uint8_t rank = 7, file = 0;
    for (char ch : board)
    {
      if (ch == '/')
      {
        file = 0;
        if (rank > 0)
          --rank;
        continue;
      }

      if (std::isdigit(static_cast<unsigned char>(ch)))
      {
        file = static_cast<uint8_t>(file + (ch - '0'));
        continue;
      }

      if (file > 7)
        continue;

      const char lo = protocol::uci::tolower_ascii(ch);
      PieceType type;
      switch (lo)
      {
      case 'k':
        type = PieceType::King;
        break;
      case 'q':
        type = PieceType::Queen;
        break;
      case 'r':
        type = PieceType::Rook;
        break;
      case 'b':
        type = PieceType::Bishop;
        break;
      case 'n':
        type = PieceType::Knight;
        break;
      case 'p':
        type = PieceType::Pawn;
        break;
      default:
        type = PieceType::None;
        break;
      }

      if (type != PieceType::None)
      {
        const Square sq = static_cast<Square>(file + rank * 8);
        const Color col = (ch == lo) ? Color::Black : Color::White;
        m_position.getBoard().setPiece(sq, {type, col});
        ++file;
      }
    }

    auto &st = m_position.getState();

    st.sideToMove =
        (!activeColor.empty() && activeColor[0] == 'w') ? Color::White : Color::Black;

    uint8_t rights = 0;
    for (char c : castling)
    {
      switch (c)
      {
      case 'K':
        rights |= CastlingRights::WhiteKingSide;
        break;
      case 'Q':
        rights |= CastlingRights::WhiteQueenSide;
        break;
      case 'k':
        rights |= CastlingRights::BlackKingSide;
        break;
      case 'q':
        rights |= CastlingRights::BlackQueenSide;
        break;
      default:
        break;
      }
    }
    st.castlingRights = rights;

    if (enPassant.size() == 2)
      st.enPassantSquare = protocol::uci::stringToSquare(enPassant);
    else
      st.enPassantSquare = NO_SQUARE;

    int hm = 0, fm = 1;
    if (!halfmoveClock.empty())
      hm = protocol::uci::parseInt(halfmoveClock);

    if (!fullmoveNumber.empty())
    {
      fm = protocol::uci::parseInt(fullmoveNumber);
      if (fm <= 0)
        fm = 1;
    }

    st.halfmoveClock = static_cast<std::uint16_t>(hm);
    st.fullmoveNumber = static_cast<std::uint32_t>(fm);

    canonicalize_ep_square(m_position);
    m_position.buildHash();
  }

  void ChessGame::buildHash()
  {
    canonicalize_ep_square(m_position);
    m_position.buildHash();
  }

  const std::vector<Move> &ChessGame::generateLegalMoves()
  {
    m_pseudo_moves.clear();
    m_legal_moves.clear();

    m_move_gen.generatePseudoLegalMoves(m_position.getBoard(), m_position.getState(), m_pseudo_moves);

    StateInfo scratch{};
    for (const auto &m : m_pseudo_moves)
    {
      if (m_position.doMove(m, scratch))
      {
        m_position.undoMove();
        m_legal_moves.push_back(m);
      }
    }

    return m_legal_moves;
  }

  const GameState &ChessGame::getGameState() const
  {
    return m_position.getState();
  }

  Square ChessGame::getRookSquareFromCastleside(CastleSide castleSide, Color side)
  {
    if (castleSide == CastleSide::KingSide)
      return (side == Color::White) ? static_cast<Square>(bb::H1)
                                    : static_cast<Square>(bb::H8);
    if (castleSide == CastleSide::QueenSide)
      return (side == Color::White) ? static_cast<Square>(bb::A1)
                                    : static_cast<Square>(bb::A8);
    return NO_SQUARE;
  }

  Square ChessGame::getKingSquare(Color color)
  {
    const bb::Bitboard kbb = m_position.getBoard().getPieces(color, PieceType::King);
    if (!kbb)
      return NO_SQUARE;
    return static_cast<Square>(bb::ctz64(kbb));
  }

  void ChessGame::checkGameResult()
  {
    if (generateLegalMoves().empty())
    {
      if (isKingInCheck(m_position.getState().sideToMove))
        m_result = GameResult::Checkmate;
      else
        m_result = GameResult::Stalemate;
    }
    else if (m_position.checkInsufficientMaterial())
      m_result = GameResult::InsufficientMaterial;
    else if (m_position.checkMoveRule())
      m_result = GameResult::MoveRule;
    else if (m_position.checkRepetition())
      m_result = GameResult::Repetition;
    else
      m_result = GameResult::Ongoing;
  }

  GameResult ChessGame::getResult() const
  {
    return m_result;
  }

  void ChessGame::setResult(GameResult res)
  {
    m_result = res;
  }

  Piece ChessGame::getPiece(Square sq) const
  {
    auto opt = m_position.getBoard().getPiece(sq);
    return opt.value_or(Piece{PieceType::None, Color::White});
  }

  bool ChessGame::doMove(Square from, Square to, PieceType promotion)
  {
    const auto &moves = generateLegalMoves();
    for (const auto &m : moves)
    {
      if (m.from() == from && m.to() == to && m.promotion() == promotion)
      {
        m_stateHistory.emplace_back();
        if (m_position.doMove(m, m_stateHistory.back()))
        {
          m_pseudo_moves.clear();
          m_legal_moves.clear();
          return true;
        }

        m_stateHistory.pop_back();
        return false;
      }
    }

    return false;
  }

  bool ChessGame::isKingInCheck(Color side) const
  {
    const bb::Bitboard kbb = m_position.getBoard().getPieces(side, PieceType::King);
    if (!kbb)
      return false;

    const Square ksq = static_cast<Square>(bb::ctz64(kbb));
    return attackedBy(m_position.getBoard(), ksq, ~side, m_position.getBoard().getAllPieces());
  }

  Position &ChessGame::getPositionRefForBot()
  {
    return m_position;
  }

  const Position &ChessGame::getPositionRefForBot() const
  {
    return m_position;
  }

  std::string ChessGame::getFen() const
  {
    std::string fen;
    fen.reserve(100);
    const auto &board = m_position.getBoard();

    for (int rank = 7; rank >= 0; --rank)
    {
      int empty = 0;
      for (int file = 0; file < 8; ++file)
      {
        const Square sq = static_cast<Square>(rank * 8 + file);
        const auto piece = board.getPiece(sq);

        if (piece.has_value())
        {
          if (empty)
          {
            fen.push_back(static_cast<char>('0' + empty));
            empty = 0;
          }

          char ch;
          switch (piece->type)
          {
          case PieceType::King:
            ch = 'k';
            break;
          case PieceType::Queen:
            ch = 'q';
            break;
          case PieceType::Rook:
            ch = 'r';
            break;
          case PieceType::Bishop:
            ch = 'b';
            break;
          case PieceType::Knight:
            ch = 'n';
            break;
          case PieceType::Pawn:
            ch = 'p';
            break;
          default:
            ch = '?';
            break;
          }

          if (piece->color == Color::White)
            ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));

          fen.push_back(ch);
        }
        else
        {
          ++empty;
        }
      }

      if (empty)
        fen.push_back(static_cast<char>('0' + empty));
      if (rank)
        fen.push_back('/');
    }

    const auto &st = m_position.getState();
    fen.push_back(' ');
    fen.push_back(st.sideToMove == Color::White ? 'w' : 'b');
    fen.push_back(' ');

    if (st.castlingRights)
    {
      if (st.castlingRights & CastlingRights::WhiteKingSide)
        fen.push_back('K');
      if (st.castlingRights & CastlingRights::WhiteQueenSide)
        fen.push_back('Q');
      if (st.castlingRights & CastlingRights::BlackKingSide)
        fen.push_back('k');
      if (st.castlingRights & CastlingRights::BlackQueenSide)
        fen.push_back('q');
    }
    else
    {
      fen.push_back('-');
    }

    fen.push_back(' ');
    if (st.enPassantSquare == NO_SQUARE)
    {
      fen.push_back('-');
    }
    else
    {
      const int sq = static_cast<int>(st.enPassantSquare);
      fen.push_back(static_cast<char>('a' + (sq & 7)));
      fen.push_back(static_cast<char>('1' + (sq >> 3)));
    }

    fen.push_back(' ');
    fen.append(std::to_string(st.halfmoveClock));
    fen.push_back(' ');
    fen.append(std::to_string(st.fullmoveNumber));

    return fen;
  }

}
