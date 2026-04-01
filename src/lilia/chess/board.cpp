#include "lilia/chess/board.hpp"

#include <cassert>

#include "lilia/chess/core/piece_encoding.hpp"

namespace lilia::chess
{
        Board::Board()
        {
                clear();
        }

        void Board::clear() noexcept
        {
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

                const int ti = (pp & 0x7) - 1;
                const PieceType pt = static_cast<PieceType>(ti);
                const Color col = ((pp >> 3) & 1u) ? Color::Black : Color::White;
                return Piece{pt, col};
        }

        void Board::setPiece(Square sq, Piece p) noexcept
        {
                const int s = static_cast<int>(sq);
#ifndef NDEBUG
                assert(s >= 0 && s < SQ_NB);
#endif
                LILIA_ASSUME(s < SQ_NB);

                const bb::Bitboard mask = bb::sq_bb(sq);
                const std::uint8_t new_packed = pack_piece(p);
                const std::uint8_t old_packed = m_piece_on[s];

                if (LILIA_UNLIKELY(old_packed == new_packed))
                        return;

                if (old_packed)
                {
                        const int old_ti = decode_ti(old_packed);
                        const int old_ci = decode_ci(old_packed);
#ifndef NDEBUG
                        assert(old_ti >= 0 && old_ti < 6 && (old_ci == 0 || old_ci == 1));
#endif
                        m_bb[old_ci][old_ti] &= ~mask;
                        m_color_occ[old_ci] &= ~mask;
                        m_all_occ &= ~mask;
                        m_piece_on[s] = 0;
                }

                if (new_packed)
                {
                        const int ti = decode_ti(new_packed);
                        const int ci = decode_ci(new_packed);
#ifndef NDEBUG
                        assert(ti >= 0 && ti < 6 && (ci == 0 || ci == 1));
#endif
                        m_bb[ci][ti] |= mask;
                        m_color_occ[ci] |= mask;
                        m_all_occ |= mask;
                        m_piece_on[s] = new_packed;
                }
        }

        void Board::removePiece(Square sq) noexcept
        {
                const int s = static_cast<int>(sq);
#ifndef NDEBUG
                assert(s >= 0 && s < SQ_NB);
#endif
                LILIA_ASSUME(s < SQ_NB);

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

        std::optional<Piece> Board::getPiece(Square sq) const noexcept
        {
                const std::uint8_t packed = m_piece_on[static_cast<int>(sq)];
                if (LILIA_UNLIKELY(!packed))
                        return std::nullopt;

                return std::optional<Piece>{std::in_place, unpack_piece(packed)};
        }

        void Board::movePiece_noCapture(Square from, Square to) noexcept
        {
                const int sf = static_cast<int>(from);
                const int st = static_cast<int>(to);
#ifndef NDEBUG
                assert(sf >= 0 && sf < SQ_NB && st >= 0 && st < SQ_NB);
#endif
                LILIA_ASSUME(sf < SQ_NB);
                LILIA_ASSUME(st < SQ_NB);

                const std::uint8_t packed = m_piece_on[sf];
#ifndef NDEBUG
                assert(packed != 0 && "movePiece_noCapture: from must be occupied");
                assert(m_piece_on[st] == 0 && "movePiece_noCapture: to must be empty");
#else
                if (LILIA_UNLIKELY(!packed))
                        return;
#endif

                const int ti = decode_ti(packed);
                const int ci = decode_ci(packed);
#ifndef NDEBUG
                assert(ti >= 0 && ti < 6 && (ci == 0 || ci == 1));
#endif

                const bb::Bitboard from_bb = bb::sq_bb(from);
                const bb::Bitboard to_bb = bb::sq_bb(to);
                const bb::Bitboard flip = from_bb ^ to_bb;

#ifdef NDEBUG
                m_bb[ci][ti] ^= flip;
                m_color_occ[ci] ^= flip;
                m_all_occ ^= flip;
#else
                m_bb[ci][ti] = (m_bb[ci][ti] & ~from_bb) | to_bb;
                m_color_occ[ci] = (m_color_occ[ci] & ~from_bb) | to_bb;
                m_all_occ = (m_all_occ & ~from_bb) | to_bb;
#endif

                m_piece_on[sf] = 0;
                m_piece_on[st] = packed;
        }

        void Board::movePiece_withCapture(Square from, Square cap_sq, Square to, Piece captured) noexcept
        {
                const int sf = static_cast<int>(from);
                const int sc = static_cast<int>(cap_sq);
                const int st = static_cast<int>(to);
#ifndef NDEBUG
                assert(sf >= 0 && sf < SQ_NB && sc >= 0 && sc < SQ_NB && st >= 0 && st < SQ_NB);
#endif
                LILIA_ASSUME(sf < SQ_NB);
                LILIA_ASSUME(sc < SQ_NB);
                LILIA_ASSUME(st < SQ_NB);

                const std::uint8_t mover_packed = m_piece_on[sf];
#ifndef NDEBUG
                assert(mover_packed != 0 && "movePiece_withCapture: from must be occupied");
#else
                if (LILIA_UNLIKELY(!mover_packed))
                        return;
#endif

                const bb::Bitboard from_bb = bb::sq_bb(from);
                const bb::Bitboard cap_bb = bb::sq_bb(cap_sq);
                const bb::Bitboard to_bb = bb::sq_bb(to);

#ifndef NDEBUG
                if (cap_sq != to)
                        assert(m_piece_on[st] == 0 && "En passant target square must be empty");
#endif

                const int m_ti = decode_ti(mover_packed);
                const int m_ci = decode_ci(mover_packed);
#ifndef NDEBUG
                assert(m_ti >= 0 && m_ti < 6 && (m_ci == 0 || m_ci == 1));
#endif

                const std::uint8_t cap_packed = m_piece_on[sc];

#ifndef NDEBUG
                if (cap_packed == 0)
                        assert(captured.type != PieceType::None && "Captured piece must exist");
#endif

                int c_ti, c_ci;
#ifdef NDEBUG
                c_ti = decode_ti(cap_packed);
                c_ci = decode_ci(cap_packed);
#else
                if (cap_packed)
                {
                        c_ti = decode_ti(cap_packed);
                        c_ci = decode_ci(cap_packed);
                }
                else
                {
                        c_ti = bb::type_index(captured.type);
                        c_ci = bb::ci(captured.color);
                }
                assert(c_ti >= 0 && c_ti < 6 && (c_ci == 0 || c_ci == 1));
#endif

#ifdef NDEBUG
                const bb::Bitboard occ_flip = cap_bb ^ from_bb ^ to_bb;
                const bb::Bitboard move_flip = from_bb ^ to_bb;

                m_all_occ ^= occ_flip;
                m_bb[c_ci][c_ti] ^= cap_bb;
                m_color_occ[c_ci] ^= cap_bb;
                m_bb[m_ci][m_ti] ^= move_flip;
                m_color_occ[m_ci] ^= move_flip;
#else
                m_bb[c_ci][c_ti] &= ~cap_bb;
                m_color_occ[c_ci] &= ~cap_bb;
                m_all_occ &= ~cap_bb;

                m_bb[m_ci][m_ti] = (m_bb[m_ci][m_ti] & ~from_bb) | to_bb;
                m_color_occ[m_ci] = (m_color_occ[m_ci] & ~from_bb) | to_bb;
                m_all_occ = (m_all_occ & ~from_bb) | to_bb;
#endif

                m_piece_on[sc] = 0;
                m_piece_on[sf] = 0;
                m_piece_on[st] = mover_packed;
        }

}
