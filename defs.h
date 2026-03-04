#ifndef DEFS_H
#define DEFS_H

#include <cstdint>
#include <string>

#define NO_EP 64 /* no enPassantSquare */
// prefer this over a & (1<<b) for b >= 32 which would need 1ULL
#define BITTEST(a, b) ((a)>>(b) & 1)
#define DEBUG(a) if (debug) std::cerr<<a;

typedef uint64_t Bitboard;

enum Piece { P, N, B, R, Q, K, p, n, b, r, q, k, NONE };
#define PIECE_MOD 6 /* use (piece % PIECE_MOD) to "remove the color" */ 
enum Color { WHITE, BLACK, BOTH };
enum Square {
    A1, B1, C1, D1, E1, F1, G1, H1,
    A2, B2, C2, D2, E2, F2, G2, H2,
    A3, B3, C3, D3, E3, F3, G3, H3,
    A4, B4, C4, D4, E4, F4, G4, H4,
    A5, B5, C5, D5, E5, F5, G5, H5,
    A6, B6, C6, D6, E6, F6, G6, H6,
    A7, B7, C7, D7, E7, F7, G7, H7,
    A8, B8, C8, D8, E8, F8, G8, H8
};
const char PIECE_CHARS[] = "PNBRQKpnbrqk.";
const std::string START_FEN = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

inline int algebraic_to_square(const std::string s){
    return (s[0] - 'a') + (s[1] - '1')*8;
}

// Castling Rights Bits
enum: unsigned long long {
    WQ_CASTLE = 1ULL<<A1, // White King-side  (1)
    WK_CASTLE = 1ULL<<H1, // White Queen-side (2)
    BQ_CASTLE = 1ULL<<A8, // Black King-side  (4)
    BK_CASTLE = 1ULL<<H8  // Black Queen-side (8)
};

// --- Move Encoding ---
// A move is a 16-bit integer:
// 0000 000000 000000
// Flag   To    From

typedef uint16_t Move;

// Move Flags (4 bits)
enum MoveFlags {
    QUIET_MOVE  = 0,
    PROMOTE_KNIGHT  = 1,
    PROMOTE_BISHOP  = 2,
    PROMOTE_ROOK    = 3,
    PROMOTE_QUEEN   = 4, // promotion can happen together with capture
    DOUBLE_PAWN_PUSH = 5, // mutually exclusive with capture or promotion
    CASTLING        = 6,  // mutually exclusive with capture or promotion
    //CASTLING_LONG   = 7,  // mutually exclusive with capture or promotion
    CAPTURE         = 8,
    EP_CAPTURE      = CAPTURE + DOUBLE_PAWN_PUSH // not exactly that but we'll use that 
};

// Inline helper functions to pack and unpack the bits instantly
inline Move encode_move(int from, int to, int flags) {
    return (from) | (to << 6) | (flags << 12);
}
inline int get_from(Move move) { return move & 0x3F; } // 0x3F is 63 (binary 111111)
inline int get_to(Move move) { return (move >> 6) & 0x3F; }
inline int get_flags(Move move) { return (move >> 12) & 0xF; } // 0xF is 15 (binary 1111)

// --- Bitboard Utilities ---
// Returns the index of the first 1-bit, and removes it from the bitboard
inline int popLSB(Bitboard &bb) {
    int lsb = __builtin_ctzll(bb);
    bb &= bb - 1; // Magic bitwise trick to erase the lowest 1-bit
    return lsb;
}

#endif
