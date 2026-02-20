/* board.h
 * (c) 2026 by MFH
 */

// Add these near the top of board.h, or in defs.h
extern Bitboard knight_attacks[64];
extern Bitboard king_attacks[64];

// And update the private methods inside your Board class:
private:
    Bitboard get_pawn_attacks(int sq, Color c); // Renamed!
    // (We remove get_knight_attacks and get_king_attacks since we just use the arrays now)
    