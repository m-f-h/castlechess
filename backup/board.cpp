/* board.cpp
   (c) 2026 by MFH

   This file implements the core logic for 
   move generation and determining if a square is attacked in a chess engine.

 */
#include "board.h"
#include <iostream>

// Define the two global arrays for leapers king & knight
Bitboard knight_attacks[64];
Bitboard king_attacks[64];

void init_tables() {
    for (int sq = 0; sq < 64; sq++) {
        int rank = sq / 8;
        int file = sq % 8;
        
        // --- 1. Init Knight Attacks ---
        Bitboard mask = 0;
        // Up 2
        if (rank < 6 && file > 0) BITSET(mask, sq + 15); // Up 2, Left 1
        if (rank < 6 && file < 7) BITSET(mask, sq + 17); // Up 2, Right 1
        // Up 1
        if (rank < 7 && file > 1) BITSET(mask, sq + 6);  // Up 1, Left 2
        if (rank < 7 && file < 6) BITSET(mask, sq + 10); // Up 1, Right 2
        // Down 1
        if (rank > 0 && file > 1) BITSET(mask, sq - 10); // Down 1, Left 2
        if (rank > 0 && file < 6) BITSET(mask, sq - 6);  // Down 1, Right 2
        // Down 2
        if (rank > 1 && file > 0) BITSET(mask, sq - 17); // Down 2, Left 1
        if (rank > 1 && file < 7) BITSET(mask, sq - 15); // Down 2, Right 1

        knight_attacks[sq] = mask;

        // --- 2. Init King Attacks ---
        mask = 0;
        // Up
        if (rank < 7)  {  BITSET(mask, sq + 8); // Up
            if (file > 0) BITSET(mask, sq + 7); // Up-Left
            if (file < 7) BITSET(mask, sq + 9); // Up-Right
        }
        // Sides
        if (file > 0)     BITSET(mask, sq - 1); // Left
        if (file < 7)     BITSET(mask, sq + 1); // Right
        // Down
        if (rank > 0)  {  BITSET(mask, sq - 8); // Down
            if (file > 0) BITSET(mask, sq - 9); // Down-Left
            if (file < 7) BITSET(mask, sq - 7); // Down-Right
        }
        king_attacks[sq] = mask;
    }
}
// --- String Helpers ---
std::string move_to_uci(int move) {
    return square_to_algebraic(get_from(move)) + square_to_algebraic(get_to(move));
}

// --- Pawn Moves with Safe Board Wrapping ---
Bitboard Board::get_pawn_attacks(int sq, Color c) {
    Bitboard attacks = 0;
    if (c == WHITE) {
        if (sq >= 56) return 0; // No attacks from the 8th rank
        int file = sq%8;
        // Capture Left (Ensure we aren't on the A-file)
        if (file > 0) BITSET(attacks, sq + 7);
        // Capture Right (Ensure we aren't on the H-file)
        if (file < 7) BITSET(attacks, sq + 9);
    } else { // BLACK
        if (sq < 8) return 0; // No attacks from the 1st rank
        int file = sq%8;
        // Capture Left (Ensure we aren't on the A-file)
        if (file > 0) BITSET(attacks, sq - 9);
        // Capture Right (Ensure we aren't on the H-file)
        if (file < 7) BITSET(attacks, sq - 7);
    }
    return attacks;
}

// --- The Optimized Attack Detection ---
bool Board::is_square_attacked(int sq, Color attacker_side) {
    // A square is attacked by an enemy pawn if a pawn of OUR color 
    // sitting on that square could "attack" the enemy pawn's actual location.
    Color defender_side = WHITE+BLACK-attacker_side;
    enemy_offset = (attacker_side == WHITE) ? 0 : 6; // P/N/B/R/Q/K vs p/n/b/r/q/k
    
    // king & knight attacks are simple lookups. 
    // We check knights first (quick)
    // but since there's only 1 king, we check that last. 
    // R, B & Q are less numerous but attack many squares, so we check those in the middle.
    // pawns are , so we check those first
    return knight_attacks[sq] & pieces[N + enemy_offset]
        || get_bishop_attacks(sq, occupancy[BOTH]) 
           & (pieces[B + enemy_offset] | pieces[Q + enemy_offset])
        || get_rook_attacks(sq, occupancy[BOTH]) 
           & (pieces[R + enemy_offset] | pieces[Q + enemy_offset])
        || get_pawn_attacks(sq, defender_side) & pieces[P + enemy_offset]
        || king_attacks[sq] & pieces[K + enemy_offset];
}