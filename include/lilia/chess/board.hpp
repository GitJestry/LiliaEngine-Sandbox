#pragma once
#include <array>
#include <cassert>
#include <cstdint>
#include <optional>

#include "core/bitboard.hpp"
#include "core/piece_encoding.hpp"
#include "lilia/chess/compiler.hpp"

namespace lilia::chess
{

  class Board
  {
  public:
    LILIA_ALWAYS_INLINE Board();

    LILIA_ALWAYS_INLINE void clear() noexcept;

    LILIA_ALWAYS_INLINE void setPiece(Square sq, Piece p) noexcept;
    LILIA_ALWAYS_INLINE void removePiece(Square sq) noexcept;
    LILIA_ALWAYS_INLINE std::optional<Piece> getPiece(Square sq) const noexcept;

    LILIA_ALWAYS_INLINE bb::Bitboard getPieces(Color c) const noexcept
    {
      return m_color_occ[bb::ci(c)];
    }
    LILIA_ALWAYS_INLINE bb::Bitboard getAllPieces() const noexcept { return m_all_occ; }

    // Safe for None/invalid PieceType
    LILIA_ALWAYS_INLINE bb::Bitboard getPieces(Color c, PieceType t) const noexcept
    {
      const int ti = bb::type_index(t);
      return LILIA_UNLIKELY(ti < 0) ? 0ULL : m_bb[bb::ci(c)][ti];
    }

    LILIA_ALWAYS_INLINE void movePiece_noCapture(Square from, Square to) noexcept;
    LILIA_ALWAYS_INLINE void movePiece_withCapture(Square from, Square capSq,
                                                   Square to, Piece captured) noexcept;

    // Additive fast helpers (do not affect existing call sites)
    LILIA_ALWAYS_INLINE std::uint8_t getPiecePacked(Square sq) const noexcept
    {
      return m_piece_on[static_cast<int>(sq)];
    }
    LILIA_ALWAYS_INLINE bool isEmpty(Square sq) const noexcept
    {
      return m_piece_on[static_cast<int>(sq)] == 0;
    }

  private:
    // [color][typeIndex 0..5]
    std::array<std::array<bb::Bitboard, 6>, 2> m_bb{};
    std::array<bb::Bitboard, 2> m_color_occ{};
    bb::Bitboard m_all_occ = 0;

    // O(1) per square (0 = empty, else (ptIdx+1) | (color<<3))
    std::array<std::uint8_t, 64> m_piece_on{};

    static constexpr std::uint8_t pack_piece(Piece p) noexcept;
    static constexpr Piece unpack_piece(std::uint8_t pp) noexcept;
  };

} // namespace lilia::model

// ---------------- Inline implementation ----------------

namespace lilia::chess
{

  LILIA_ALWAYS_INLINE Board::Board()
  {
    clear();
  }

  LILIA_ALWAYS_INLINE void Board::clear() noexcept
  {
    // Value-init is typically compiled to an efficient memset/zeroing sequence.
    m_bb = {};
    m_color_occ = {0ULL, 0ULL};
    m_all_occ = 0ULL;
    m_piece_on.fill(0);
  }

  constexpr std::uint8_t Board::pack_piece(Piece p) noexcept
  {
    const int ti = bb::type_index(p.type);
    if (LILIA_UNLIKELY(ti < 0))
      return 0;

#ifndef NDEBUG
    assert(ti >= 0 && ti < 6 && "Invalid PieceType");
#endif

    const std::uint8_t c = static_cast<std::uint8_t>(bb::ci(p.color) & 1u);
    return static_cast<std::uint8_t>((ti + 1) | (c << 3));
  }

  constexpr Piece Board::unpack_piece(std::uint8_t pp) noexcept
  {
    if (pp == 0)
      return Piece{PieceType::None, Color::White};
    const int ti = (pp & 0x7) - 1; // 0..5
    const PieceType pt = static_cast<PieceType>(ti);
    const Color col = ((pp >> 3) & 1u) ? Color::Black : Color::White;
    return Piece{pt, col};
  }

  LILIA_ALWAYS_INLINE void Board::setPiece(Square sq, Piece p) noexcept
  {
    const int s = static_cast<int>(sq);
#ifndef NDEBUG
    assert(s >= 0 && s < 64);
#endif
    LILIA_ASSUME(s < 64);

    const bb::Bitboard mask = bb::sq_bb(sq);

    const std::uint8_t newPacked = pack_piece(p);
    const std::uint8_t oldPacked = m_piece_on[s];
    if (LILIA_UNLIKELY(oldPacked == newPacked))
      return;

    // Remove old if present
    if (oldPacked)
    {
      const int oldTi = decode_ti(oldPacked);
      const int oldCi = decode_ci(oldPacked);
#ifndef NDEBUG
      assert(oldTi >= 0 && oldTi < 6 && (oldCi == 0 || oldCi == 1));
#endif
      m_bb[oldCi][oldTi] &= ~mask;
      m_color_occ[oldCi] &= ~mask;
      m_all_occ &= ~mask;
      m_piece_on[s] = 0;
    }

    // Place new if not empty
    if (newPacked)
    {
      const int ti = decode_ti(newPacked);
      const int ci = decode_ci(newPacked);
#ifndef NDEBUG
      assert(ti >= 0 && ti < 6 && (ci == 0 || ci == 1));
#endif
      m_bb[ci][ti] |= mask;
      m_color_occ[ci] |= mask;
      m_all_occ |= mask;
      m_piece_on[s] = newPacked;
    }
  }

  LILIA_ALWAYS_INLINE void Board::removePiece(Square sq) noexcept
  {
    const int s = static_cast<int>(sq);
#ifndef NDEBUG
    assert(s >= 0 && s < 64);
#endif
    LILIA_ASSUME(s < 64);

    const std::uint8_t packed = m_piece_on[s];
    if (LILIA_UNLIKELY(!packed))
      return;

    const int ti = decode_ti(packed);
    const int ci = decode_ci(packed);
#ifndef NDEBUG
    assert(ti >= 0 && ti < 6 && (ci == 0 || ci == 1));
#endif

    const bb::Bitboard mask = bb::sq_bb(sq);

    m_bb[ci][ti] &= ~mask;
    m_color_occ[ci] &= ~mask;
    m_all_occ &= ~mask;
    m_piece_on[s] = 0;
  }

  LILIA_ALWAYS_INLINE std::optional<Piece> Board::getPiece(Square sq) const noexcept
  {
    const std::uint8_t packed = m_piece_on[static_cast<int>(sq)];
    if (LILIA_UNLIKELY(!packed))
      return std::nullopt;
    // Avoid extra temporaries; still returns the same type.
    return std::optional<Piece>{std::in_place, unpack_piece(packed)};
  }

  LILIA_ALWAYS_INLINE void Board::movePiece_noCapture(Square from, Square to) noexcept
  {
    const int sf = static_cast<int>(from);
    const int st = static_cast<int>(to);
#ifndef NDEBUG
    assert(sf >= 0 && sf < 64 && st >= 0 && st < 64);
#endif
    LILIA_ASSUME(sf < 64);
    LILIA_ASSUME(st < 64);

    const std::uint8_t packed = m_piece_on[sf];
#ifndef NDEBUG
    assert(packed != 0 && "movePiece_noCapture: from must be occupied");
    assert(m_piece_on[st] == 0 && "movePiece_noCapture: 'to' must be empty");
#else
    if (LILIA_UNLIKELY(!packed))
      return;
#endif

    const int ti = decode_ti(packed);
    const int ci = decode_ci(packed);
#ifndef NDEBUG
    assert(ti >= 0 && ti < 6 && (ci == 0 || ci == 1));
#endif

    const bb::Bitboard fromBB = bb::sq_bb(from);
    const bb::Bitboard toBB = bb::sq_bb(to);
    const bb::Bitboard flip = fromBB ^ toBB; // squares distinct; XOR = OR

#ifdef NDEBUG
    m_bb[ci][ti] ^= flip;
    m_color_occ[ci] ^= flip;
    m_all_occ ^= flip;
#else
    m_bb[ci][ti] = (m_bb[ci][ti] & ~fromBB) | toBB;
    m_color_occ[ci] = (m_color_occ[ci] & ~fromBB) | toBB;
    m_all_occ = (m_all_occ & ~fromBB) | toBB;
#endif

    m_piece_on[sf] = 0;
    m_piece_on[st] = packed;
  }

  LILIA_ALWAYS_INLINE void Board::movePiece_withCapture(Square from, Square capSq,
                                                        Square to,
                                                        Piece captured) noexcept
  {
    const int sf = static_cast<int>(from);
    const int sc = static_cast<int>(capSq);
    const int st = static_cast<int>(to);
#ifndef NDEBUG
    assert(sf >= 0 && sf < 64 && sc >= 0 && sc < 64 && st >= 0 && st < 64);
#endif
    LILIA_ASSUME(sf < 64);
    LILIA_ASSUME(sc < 64);
    LILIA_ASSUME(st < 64);

    const std::uint8_t moverPacked = m_piece_on[sf];
#ifndef NDEBUG
    assert(moverPacked != 0 && "movePiece_withCapture: from must be occupied");
#else
    if (LILIA_UNLIKELY(!moverPacked))
      return;
#endif

    const bb::Bitboard fromBB = bb::sq_bb(from);
    const bb::Bitboard capBB = bb::sq_bb(capSq);
    const bb::Bitboard toBB = bb::sq_bb(to);

#ifndef NDEBUG
    // Normal capture: capSq == to (occupied by captured)
    // En passant:     capSq != to and 'to' is empty before move
    if (capSq != to)
    {
      assert(m_piece_on[st] == 0 && "EP target square must be empty before the move");
    }
#endif

    // Decode mover
    const int m_ti = decode_ti(moverPacked);
    const int m_ci = decode_ci(moverPacked);
#ifndef NDEBUG
    assert(m_ti >= 0 && m_ti < 6 && (m_ci == 0 || m_ci == 1));
#endif

    // Decode captured: fast path trusts board invariants in release.
    const std::uint8_t capPacked = m_piece_on[sc];

#ifndef NDEBUG
    // If board is inconsistent, fall back to the argument only in debug (to surface issues).
    if (capPacked == 0)
    {
      assert(captured.type != PieceType::None && "captured must exist");
    }
#endif

    int c_ti, c_ci;
#ifdef NDEBUG
    // In release, assume capSq is occupied (normal capture or EP).
    c_ti = decode_ti(capPacked);
    c_ci = decode_ci(capPacked);
#else
    if (capPacked)
    {
      c_ti = decode_ti(capPacked);
      c_ci = decode_ci(capPacked);
    }
    else
    {
      c_ti = bb::type_index(captured.type);
      c_ci = bb::ci(captured.color);
    }
    assert(c_ti >= 0 && c_ti < 6 && (c_ci == 0 || c_ci == 1));
#endif

#ifdef NDEBUG
    // Combined occupancy flip:
    // - EP:  capBB ^ fromBB ^ toBB toggles cap off, from off, to on
    // - Normal capture where capBB==toBB: capBB cancels toBB => flip is fromBB only (correct)
    const bb::Bitboard occFlip = capBB ^ fromBB ^ toBB;
    m_all_occ ^= occFlip;

    // Captured side off at cap
    m_bb[c_ci][c_ti] ^= capBB;
    m_color_occ[c_ci] ^= capBB;

    // Mover side from -> to
    const bb::Bitboard moveFlip = fromBB ^ toBB;
    m_bb[m_ci][m_ti] ^= moveFlip;
    m_color_occ[m_ci] ^= moveFlip;
#else
    // Debug-safe explicit updates
    m_bb[c_ci][c_ti] &= ~capBB;
    m_color_occ[c_ci] &= ~capBB;
    m_all_occ &= ~capBB;

    m_bb[m_ci][m_ti] = (m_bb[m_ci][m_ti] & ~fromBB) | toBB;
    m_color_occ[m_ci] = (m_color_occ[m_ci] & ~fromBB) | toBB;
    m_all_occ = (m_all_occ & ~fromBB) | toBB;
#endif

    // By-square update
    m_piece_on[sc] = 0;
    m_piece_on[sf] = 0;
    m_piece_on[st] = moverPacked;
  }

} // namespace lilia::model
