#pragma once
#include <array>
#include <optional>
#include <cstdint>
#include <cassert>

#include "core/model_types.hpp"

namespace lilia::model {

namespace detail {
inline constexpr std::int8_t kTypeIndex[7] = {0, 1, 2, 3, 4, 5, -1};
inline int type_index(core::PieceType t) noexcept {
  return kTypeIndex[static_cast<int>(t)];
}
inline int decode_ti(std::uint8_t packed) noexcept { return (packed & 0x7) - 1; }
inline int decode_ci(std::uint8_t packed) noexcept { return (packed >> 3) & 0x1; }
}  // namespace detail

class Board {
 public:
  inline Board();

  inline void clear() noexcept;

  inline void setPiece(core::Square sq, bb::Piece p) noexcept;
  inline void removePiece(core::Square sq) noexcept;
  inline std::optional<bb::Piece> getPiece(core::Square sq) const noexcept;

  bb::Bitboard getPieces(core::Color c) const { return m_color_occ[bb::ci(c)]; }
  bb::Bitboard getAllPieces() const { return m_all_occ; }
  bb::Bitboard getPieces(core::Color c, core::PieceType t) const {
    return m_bb[bb::ci(c)][static_cast<int>(t)];
  }

  inline void movePiece_noCapture(core::Square from, core::Square to) noexcept;
  inline void movePiece_withCapture(core::Square from, core::Square capSq, core::Square to,
                                    bb::Piece captured) noexcept;

 private:
  std::array<std::array<bb::Bitboard, 6>, 2> m_bb{};
  std::array<bb::Bitboard, 2> m_color_occ{};
  bb::Bitboard m_all_occ = 0;

  // O(1)-Lookup per square (0 = empty, else (ptIdx+1) | (color<<3))
  std::array<std::uint8_t, 64> m_piece_on{};

  // Helper
  static constexpr std::uint8_t pack_piece(bb::Piece p) noexcept;
  static constexpr bb::Piece unpack_piece(std::uint8_t pp) noexcept;
};

}  // namespace lilia::model

namespace lilia::model {

inline Board::Board() { clear(); }

inline void Board::clear() noexcept {
  for (auto& byColor : m_bb) byColor.fill(0);
  m_color_occ = {0, 0};
  m_all_occ = 0;
  m_piece_on.fill(0);
}

constexpr std::uint8_t Board::pack_piece(bb::Piece p) noexcept {
  if (p.type == core::PieceType::None) return 0;
  const int ti = detail::type_index(p.type);  // 0..5
  assert(ti >= 0 && ti < 6 && "Invalid PieceType");
  const std::uint8_t c = static_cast<std::uint8_t>(bb::ci(p.color) & 1u);  // 0=white,1=black
  return static_cast<std::uint8_t>((ti + 1) | (c << 3));  // low3: (ti+1), bit3: color
}

constexpr bb::Piece Board::unpack_piece(std::uint8_t pp) noexcept {
  if (pp == 0) return bb::Piece{core::PieceType::None, core::Color::White};
  const int ti = (pp & 0x7) - 1;  // 0..5
  const core::PieceType pt = static_cast<core::PieceType>(ti);
  const core::Color col = ((pp >> 3) & 1u) ? core::Color::Black : core::Color::White;
  return bb::Piece{pt, col};
}

inline void Board::setPiece(core::Square sq, bb::Piece p) noexcept {
  const int s = static_cast<int>(sq);
  assert(s >= 0 && s < 64);

  const std::uint8_t newPacked = pack_piece(p);
  const std::uint8_t oldPacked = m_piece_on[s];

  // Fast path: same piece already there → nothing to do
  if (oldPacked == newPacked) return;

  // If something stands there, remove it (without constructing a bb::Piece)
  if (oldPacked) {
    const int oldTi = detail::decode_ti(oldPacked);
    const int oldCi = detail::decode_ci(oldPacked);
    const bb::Bitboard mask = bb::sq_bb(sq);

    m_bb[oldCi][oldTi] &= ~mask;
    m_color_occ[oldCi] &= ~mask;
    m_all_occ &= ~mask;

    m_piece_on[s] = 0;
  }

  // Place new (if not empty)
  if (newPacked) {
    const int ti = detail::type_index(p.type);
    assert(ti >= 0 && ti < 6 && "Invalid PieceType in setPiece");
    const int ci = bb::ci(p.color);
    const bb::Bitboard mask = bb::sq_bb(sq);

    m_bb[ci][ti] |= mask;
    m_color_occ[ci] |= mask;
    m_all_occ |= mask;

    m_piece_on[s] = newPacked;
  }
}

inline void Board::removePiece(core::Square sq) noexcept {
  const int s = static_cast<int>(sq);
  assert(s >= 0 && s < 64);

  const std::uint8_t packed = m_piece_on[s];
  if (!packed) return;  // already empty

  // Decode directly from packed (avoid constructing bb::Piece)
  const int ti = detail::decode_ti(packed);
  const int ci = detail::decode_ci(packed);
  assert(ti >= 0 && ti < 6 && (ci == 0 || ci == 1));

  const bb::Bitboard mask = bb::sq_bb(sq);

  m_bb[ci][ti] &= ~mask;
  m_color_occ[ci] &= ~mask;
  m_all_occ &= ~mask;

  m_piece_on[s] = 0;
}

inline std::optional<bb::Piece> Board::getPiece(core::Square sq) const noexcept {
  const std::uint8_t packed = m_piece_on[static_cast<int>(sq)];
  if (!packed) return std::nullopt;
  return unpack_piece(packed);
}

inline void Board::movePiece_noCapture(core::Square from, core::Square to) noexcept {
  const int sf = static_cast<int>(from);
  const int st = static_cast<int>(to);
  assert(sf >= 0 && sf < 64 && st >= 0 && st < 64);

  const std::uint8_t packed = m_piece_on[sf];
  if (!packed) return;  // nothing to move
  assert(m_piece_on[st] == 0 && "movePiece_noCapture: 'to' must be empty");

  const int ti = detail::decode_ti(packed);
  const int ci = detail::decode_ci(packed);
  assert(ti >= 0 && ti < 6 && (ci == 0 || ci == 1));

  const bb::Bitboard fromMask = bb::sq_bb(from);
  const bb::Bitboard toMask = bb::sq_bb(to);

  // Piece bitboard
  m_bb[ci][ti] = (m_bb[ci][ti] & ~fromMask) | toMask;

  // Occupancies (robust form rather than XOR)
  m_color_occ[ci] = (m_color_occ[ci] & ~fromMask) | toMask;
  m_all_occ = (m_all_occ & ~fromMask) | toMask;

  // By-square
  m_piece_on[sf] = 0;
  m_piece_on[st] = packed;
}

inline void Board::movePiece_withCapture(core::Square from, core::Square capSq,
                                         core::Square to, bb::Piece captured) noexcept {
  const int sf = static_cast<int>(from);
  const int sc = static_cast<int>(capSq);
  const int st = static_cast<int>(to);

  const std::uint8_t moverPacked = m_piece_on[sf];
  if (!moverPacked) return;  // nothing to move

  // Decode mover
  const int m_ti = (moverPacked & 0x7) - 1;   // 0..5
  const int m_ci = (moverPacked >> 3) & 0x1;  // 0/1
  assert(m_ti >= 0 && m_ti < 6);

  // Decode captured (must exist)
  assert(captured.type != core::PieceType::None);
  const int c_ti = detail::type_index(captured.type);
  const int c_ci = bb::ci(captured.color);
  assert(c_ti >= 0 && c_ti < 6);

  const bb::Bitboard fromBB = bb::sq_bb(from);
  const bb::Bitboard capBB = bb::sq_bb(capSq);
  const bb::Bitboard toBB = bb::sq_bb(to);

  // Preconditions on squares
  // Normal capture: capSq == to (occupied by captured)
  // En passant:     capSq != to and 'to' is empty before move
  if (capSq == to) {
    // Ensure 'to' currently holds the captured (not strictly required in release)
    // assert(m_piece_on[st] && "capture target must be occupied");
  } else {
    assert(m_piece_on[st] == 0 && "EP target square must be empty before the move");
  }

  // 1) Remove captured piece from its bitboards / occupancies / square
  m_bb[c_ci][c_ti] &= ~capBB;
  m_color_occ[c_ci] &= ~capBB;
  m_all_occ &= ~capBB;
  m_piece_on[sc] = 0;

  // 2) Move mover from -> to (bitboards & occupancies)
  m_bb[m_ci][m_ti] = (m_bb[m_ci][m_ti] & ~fromBB) | toBB;
  m_color_occ[m_ci] = (m_color_occ[m_ci] & ~fromBB) | toBB;
  m_all_occ = (m_all_occ & ~fromBB) | toBB;

  // 3) Update by-square
  m_piece_on[sf] = 0;
  m_piece_on[st] = moverPacked;
}

}  // namespace lilia::model
