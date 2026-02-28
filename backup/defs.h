/* defs.h for the castlechess project
  (c) 2026 by MFH
*/
#pragma once
#include <cstdint>
#include <string>
#include <chrono> // needed for engine and main.
#define DEBUG(args) std::cerr<<args;

// Colors
enum Color { WHITE, BLACK, BOTH };

// Pieces (Standard mapping based on your array usage)
enum Piece { P, N, B, R, Q, K, p, n, b, r, q, k, NONE };
#define PIECE_MOD 6 /* For modulo operations to get piece type ignoring color */
static const char* PIECE_SYMBOL = "PNBRQKpnbrqk.";

// Squares mapping (A1 = 0 ... H8 = 63)
enum Square {
    A1, B1, C1, D1, E1, F1, G1, H1,
    A2, B2, C2, D2, E2, F2, G2, H2,
    A3, B3, C3, D3, E3, F3, G3, H3,
    A4, B4, C4, D4, E4, F4, G4, H4,
    A5, B5, C5, D5, E5, F5, G5, H5,
    A6, B6, C6, D6, E6, F6, G6, H6,
    A7, B7, C7, D7, E7, F7, G7, H7,
    A8, B8, C8, D8, E8, F8, G8, H8, NO_SQUARE
};

// *** MOVES ***
// Moves are encoded as a single integer with the following bit structure:
// Bits 0-5: 'from' square (0-63)
// Bits 6-11: 'to' square (0-63)
// Bits 12-15: flags (0-15)
typedef uint16_t Move;

inline Move encode_move(int from, int to, int flags) {
    return (Move)(from | to << 6 | flags << 12);
}
// Move Extraction Helpers
inline int get_from(int move) { return move & 0x3F; }
inline int get_to(int move) { return move >> 6 & 0x3F; }
inline int get_flags(int move) { return move >> 12; }

enum MoveFlags {
    QUIET_MOVE = 0,
    PROMOTE_KNIGHT = N,
    PROMOTE_BISHOP = B,
    PROMOTE_ROOK = R,
    PROMOTE_QUEEN = Q,  // Promotion can be | CAPTURE
    DOUBLE_PAWN_PUSH = 5,
    CASTLE = 6,
    // CASTLE_LONG = 7, // maybe not needed
    PROMOTE_MASK = 7, // Mask for any promotion type
    CAPTURE = 8,
    EN_PASSANT = CAPTURE + DOUBLE_PAWN_PUSH, // = 13
};

// Bit Manipulation Macros
#define BITTEST(bb, sq) ((bb)>>(sq) & 1)
#define BITSET(bb, sq)  ((bb) |= (1ULL << (sq)))
#define BITPOP(bb, sq)  ((bb) &= ~(1ULL << (sq)))

// get the index (=square number) of the lowest 1-bit
#define GET_LSB(bb) __builtin_ctzll(bb)

// clears the LSB (so we can find the next piece)
#define POP_LSB(bb) ((bb) &= (bb) - 1)

// #define popcount __builtin_popcountll
inline int popcount(uint64_t bitboard) {
    return __builtin_popcountll(bitboard);
    //return std::popcount(bitboard); // valid only in C++20 or later
}

typedef uint64_t Bitboard;

// Files
const Bitboard FILE_A = 0x0101010101010101ULL;
const Bitboard FILE_H = 0x8080808080808080ULL;
const Bitboard NOT_A_FILE = ~FILE_A;
const Bitboard NOT_H_FILE = ~FILE_H;

// Ranks
const Bitboard RANK_2 = 0x000000000000FF00ULL; // Black promotions happen from here
const Bitboard RANK_3 = 0x0000000000FF0000ULL;
const Bitboard RANK_6 = 0x0000FF0000000000ULL;
const Bitboard RANK_7 = 0x00FF000000000000ULL; // White promotions happen from here

// Castling Rights (Flags)
enum CastlingRights : unsigned long long {
  WHITE_O_O_O = 1ULL<<A1,
  WHITE_O_O = 1ULL<<H1,
  BLACK_O_O_O = 1ULL<<A8,
  BLACK_O_O = 1ULL<<H8 };

enum TerminalResult {
    NOT_OVER = 0,
    WHITE_WINS = 1,
    BLACK_WINS = 2,
    DRAW = 3,
    CASTLECHESS_WIN = 4,
    ILLEGAL_POSITION = 7 // = 4 | 3
};
