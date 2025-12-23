#pragma once
#include <array>
#include <cassert>
#include <cstdint>
#include <optional>

#include "core/bitboard.hpp"
#include "core/model_types.hpp"

#ifndef LILIA_ALWAYS_INLINE
#if defined(_MSC_VER)
#define LILIA_ALWAYS_INLINE __forceinline
#elif defined(__GNUC__) || defined(__clang__)
#define LILIA_ALWAYS_INLINE inline __attribute__((always_inline))
#else
#define LILIA_ALWAYS_INLINE inline
#endif
#endif

namespace lilia::model {

namespace detail {

// Assumes core::PieceType values map to [0..6] where 6 is None.
// Returns [0..5] for pieces, -1 for None/invalid.
LILIA_ALWAYS_INLINE constexpr int type_index(core::PieceType t) noexcept {
  const int ti = static_cast<int>(t);
  return (ti >= 0 && ti < 6) ? ti : -1;
}

LILIA_ALWAYS_INLINE constexpr int decode_ti(std::uint8_t packed) noexcept {
  return (packed & 0x7) - 1;
}
LILIA_ALWAYS_INLINE constexpr int decode_ci(std::uint8_t packed) noexcept {
  return (packed >> 3) & 0x1;
}

}  // namespace detail

class Board {
 public:
  LILIA_ALWAYS_INLINE Board();

  LILIA_ALWAYS_INLINE void clear() noexcept;

  LILIA_ALWAYS_INLINE void setPiece(core::Square sq, bb::Piece p) noexcept;
  LILIA_ALWAYS_INLINE void removePiece(core::Square sq) noexcept;
  LILIA_ALWAYS_INLINE std::optional<bb::Piece> getPiece(core::Square sq) const noexcept;

  LILIA_ALWAYS_INLINE bb::Bitboard getPieces(core::Color c) const noexcept {
    return m_color_occ[bb::ci(c)];
  }
  LILIA_ALWAYS_INLINE bb::Bitboard getAllPieces() const noexcept { return m_all_occ; }

  // Safe for None/invalid PieceType
  LILIA_ALWAYS_INLINE bb::Bitboard getPieces(core::Color c, core::PieceType t) const noexcept {
    const int ti = detail::type_index(t);
    return (ti < 0) ? 0ULL : m_bb[bb::ci(c)][ti];
  }

  LILIA_ALWAYS_INLINE void movePiece_noCapture(core::Square from, core::Square to) noexcept;
  LILIA_ALWAYS_INLINE void movePiece_withCapture(core::Square from, core::Square capSq,
                                                 core::Square to, bb::Piece captured) noexcept;

  // Additive fast helpers (do not affect existing call sites)
  LILIA_ALWAYS_INLINE std::uint8_t getPiecePacked(core::Square sq) const noexcept {
    return m_piece_on[static_cast<int>(sq)];
  }
  LILIA_ALWAYS_INLINE bool isEmpty(core::Square sq) const noexcept {
    return m_piece_on[static_cast<int>(sq)] == 0;
  }

 private:
  // [color][typeIndex 0..5]
  std::array<std::array<bb::Bitboard, 6>, 2> m_bb{};
  std::array<bb::Bitboard, 2> m_color_occ{};
  bb::Bitboard m_all_occ = 0;

  // O(1) per square (0 = empty, else (ptIdx+1) | (color<<3))
  std::array<std::uint8_t, 64> m_piece_on{};

  static constexpr std::uint8_t pack_piece(bb::Piece p) noexcept;
  static constexpr bb::Piece unpack_piece(std::uint8_t pp) noexcept;
};

}  // namespace lilia::model

// ---------------- Inline implementation ----------------

namespace lilia::model {

LILIA_ALWAYS_INLINE Board::Board() {
  clear();
}

LILIA_ALWAYS_INLINE void Board::clear() noexcept {
  // Value-init is typically compiled to an efficient memset/zeroing sequence.
  m_bb = {};
  m_color_occ = {0ULL, 0ULL};
  m_all_occ = 0ULL;
  m_piece_on.fill(0);
}

constexpr std::uint8_t Board::pack_piece(bb::Piece p) noexcept {
  const int ti = detail::type_index(p.type);
  if (ti < 0) return 0;

#ifndef NDEBUG
  assert(ti >= 0 && ti < 6 && "Invalid PieceType");
#endif

  const std::uint8_t c = static_cast<std::uint8_t>(bb::ci(p.color) & 1u);
  return static_cast<std::uint8_t>((ti + 1) | (c << 3));
}

constexpr bb::Piece Board::unpack_piece(std::uint8_t pp) noexcept {
  if (pp == 0) return bb::Piece{core::PieceType::None, core::Color::White};
  const int ti = (pp & 0x7) - 1;  // 0..5
  const core::PieceType pt = static_cast<core::PieceType>(ti);
  const core::Color col = ((pp >> 3) & 1u) ? core::Color::Black : core::Color::White;
  return bb::Piece{pt, col};
}

LILIA_ALWAYS_INLINE void Board::setPiece(core::Square sq, bb::Piece p) noexcept {
  const int s = static_cast<int>(sq);
#ifndef NDEBUG
  assert(s >= 0 && s < 64);
#endif

  const bb::Bitboard mask = bb::sq_bb(sq);

  const std::uint8_t newPacked = pack_piece(p);
  const std::uint8_t oldPacked = m_piece_on[s];
  if (oldPacked == newPacked) return;

  // Remove old if present
  if (oldPacked) {
    const int oldTi = detail::decode_ti(oldPacked);
    const int oldCi = detail::decode_ci(oldPacked);
#ifndef NDEBUG
    assert(oldTi >= 0 && oldTi < 6 && (oldCi == 0 || oldCi == 1));
#endif
    m_bb[oldCi][oldTi] &= ~mask;
    m_color_occ[oldCi] &= ~mask;
    m_all_occ &= ~mask;
    m_piece_on[s] = 0;
  }

  // Place new if not empty
  if (newPacked) {
    const int ti = detail::decode_ti(newPacked);
    const int ci = detail::decode_ci(newPacked);
#ifndef NDEBUG
    assert(ti >= 0 && ti < 6 && (ci == 0 || ci == 1));
#endif
    m_bb[ci][ti] |= mask;
    m_color_occ[ci] |= mask;
    m_all_occ |= mask;
    m_piece_on[s] = newPacked;
  }
}

LILIA_ALWAYS_INLINE void Board::removePiece(core::Square sq) noexcept {
  const int s = static_cast<int>(sq);
#ifndef NDEBUG
  assert(s >= 0 && s < 64);
#endif

  const std::uint8_t packed = m_piece_on[s];
  if (!packed) return;

  const int ti = detail::decode_ti(packed);
  const int ci = detail::decode_ci(packed);
#ifndef NDEBUG
  assert(ti >= 0 && ti < 6 && (ci == 0 || ci == 1));
#endif

  const bb::Bitboard mask = bb::sq_bb(sq);

  m_bb[ci][ti] &= ~mask;
  m_color_occ[ci] &= ~mask;
  m_all_occ &= ~mask;
  m_piece_on[s] = 0;
}

LILIA_ALWAYS_INLINE std::optional<bb::Piece> Board::getPiece(core::Square sq) const noexcept {
  const std::uint8_t packed = m_piece_on[static_cast<int>(sq)];
  if (!packed) return std::nullopt;
  // Avoid extra temporaries; still returns the same type.
  return std::optional<bb::Piece>{std::in_place, unpack_piece(packed)};
}

LILIA_ALWAYS_INLINE void Board::movePiece_noCapture(core::Square from, core::Square to) noexcept {
  const int sf = static_cast<int>(from);
  const int st = static_cast<int>(to);
#ifndef NDEBUG
  assert(sf >= 0 && sf < 64 && st >= 0 && st < 64);
#endif

  const std::uint8_t packed = m_piece_on[sf];
#ifndef NDEBUG
  assert(packed != 0 && "movePiece_noCapture: from must be occupied");
  assert(m_piece_on[st] == 0 && "movePiece_noCapture: 'to' must be empty");
#else
  if (!packed) return;
#endif

  const int ti = detail::decode_ti(packed);
  const int ci = detail::decode_ci(packed);
#ifndef NDEBUG
  assert(ti >= 0 && ti < 6 && (ci == 0 || ci == 1));
#endif

  const bb::Bitboard fromBB = bb::sq_bb(from);
  const bb::Bitboard toBB = bb::sq_bb(to);
  const bb::Bitboard flip = fromBB ^ toBB;  // squares distinct; XOR = OR

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

LILIA_ALWAYS_INLINE void Board::movePiece_withCapture(core::Square from, core::Square capSq,
                                                      core::Square to,
                                                      bb::Piece captured) noexcept {
  const int sf = static_cast<int>(from);
  const int sc = static_cast<int>(capSq);
  const int st = static_cast<int>(to);
#ifndef NDEBUG
  assert(sf >= 0 && sf < 64 && sc >= 0 && sc < 64 && st >= 0 && st < 64);
#endif

  const std::uint8_t moverPacked = m_piece_on[sf];
#ifndef NDEBUG
  assert(moverPacked != 0 && "movePiece_withCapture: from must be occupied");
#else
  if (!moverPacked) return;
#endif

  const bb::Bitboard fromBB = bb::sq_bb(from);
  const bb::Bitboard capBB = bb::sq_bb(capSq);
  const bb::Bitboard toBB = bb::sq_bb(to);

#ifndef NDEBUG
  // Normal capture: capSq == to (occupied by captured)
  // En passant:     capSq != to and 'to' is empty before move
  if (capSq != to) {
    assert(m_piece_on[st] == 0 && "EP target square must be empty before the move");
  }
#endif

  // Decode mover
  const int m_ti = detail::decode_ti(moverPacked);
  const int m_ci = detail::decode_ci(moverPacked);
#ifndef NDEBUG
  assert(m_ti >= 0 && m_ti < 6 && (m_ci == 0 || m_ci == 1));
#endif

  // Decode captured: fast path trusts board invariants in release.
  const std::uint8_t capPacked = m_piece_on[sc];

#ifndef NDEBUG
  // If board is inconsistent, fall back to the argument only in debug (to surface issues).
  if (capPacked == 0) {
    assert(captured.type != core::PieceType::None && "captured must exist");
  }
#endif

  int c_ti, c_ci;
#ifdef NDEBUG
  // In release, assume capSq is occupied (normal capture or EP).
  c_ti = detail::decode_ti(capPacked);
  c_ci = detail::decode_ci(capPacked);
#else
  if (capPacked) {
    c_ti = detail::decode_ti(capPacked);
    c_ci = detail::decode_ci(capPacked);
  } else {
    c_ti = detail::type_index(captured.type);
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

}  // namespace lilia::model
