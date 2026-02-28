/* board.h
 * (c) 2026 by MFH
 */
#pragma once
#include "defs.h"
//#include "cc_engine.h"
#include <string>
#include <iostream>
#define SORT_MOVES 1 // Set to 0 to disable move sorting (for testing)

const std::string STANDARD_FEN = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

// --- Global Lookup Tables & Init ---
// Computed exactly once at startup
extern Bitboard knight_attacks[64];
extern Bitboard king_attacks[64];
void init_tables();

// Zobrist feature arrays
extern uint64_t zobrist_pieces[12][64]; // [Piece enum][Square 0-63]
extern uint64_t zobrist_side;           // XOR this in if it is Black's turn
extern uint64_t zobrist_castling[4];   // 4 bits of castling rights = 16 possible states
extern uint64_t zobrist_enpassant[64];  // 8 possible en passant squares

// --- String & UCI Helpers ---
 int square_from_algebraic(std::string s);
 Move move_from_uci(std::string uci);
 std::string square_to_algebraic(int sq); // convert square number A1=0 .. H8=63 to string "a1",...,"h8"
 std::string move_to_uci(Move move);

// --- Data Structures ---
struct MoveList {
    Move moves[256]; 
    int count;
    inline void add_move(Move move) {
        moves[count++] = move;
    }
    int score[256]; // preliminary scores, for order of eval only, must be initialized after generate_moves
    int current = 0;
    inline Move next_best() { // this may be used only once, moves are overwritten
        if (current >= count) return 0;
#if !SORT_MOVES
        else return moves[current++]; // No sorting, just return in order generated
#else
        Move move = moves[current];
        int best_idx = current, best_score = score[current];
        for (int j = count; --j > current; )
            if (score[j] > best_score) best_score = score[best_idx = j];
        if (best_idx > current) {
            score[best_idx] = score[current]; // copy score to that later position
            Move best = moves[best_idx];
            moves[best_idx] = move;    // copy move[i] to that later position
            move = best; // no need to overwrite move[i]: no more used
        }
        ++current;
        return move;
#endif
    }
};

// --- The Board Class ---
class Board {
public:
    // --- Core State ---
    Bitboard pieces[12];     
    Bitboard occupancy[3];   
    Piece mailbox[64];       
    
    int enPassantSquare;     // Uses NO_SQ (64) when empty
    unsigned long long castling_rights; 
    
    int halfmove_clock;      // Tracks half-moves for the 50-move draw rule
    int fullmove_number;     // Tracks the total turn number (starts at 1)
    Color side;         // whose turn it is
    bool searching = false; // Flag to indicate if we're in search mode 
                            //(used for optimizations like early exit on castling)
    uint64_t hash = 0; // zobrist hash of the current position
    uint64_t generate_hash(); // function to compute the hash from scratch (used after parsing FEN)

    // --- Initialization & Debugging ---
    void reset();                     
    void parse_fen(const std::string fen = STANDARD_FEN); // set up the board from a FEN string (default is standard chess starting position)
    void print(bool flipped = false); // print the board, flipped for Black's perspective if flipped=true              

    // --- Main Engine Interface ---
    bool is_square_attacked(int sq, Color attacker_side);
    void generate_moves(MoveList& list);
    void make_move(uint16_t move);
    uint64_t perft(int depth);
    void perft_divide(int depth);
    void update_occupancy();
    bool king_can_be_captured(); // (enemy) king was left hanging (side to move could capture it)
    bool is_check(); // (own) king is under attack (opponent could capture our king on their turn)
    bool is_pseudolegal_move(Move m); // does anyone use this?
    Move identify_pseudolegal_move(Move m);// the move may be missing some flags (capture, e.p., double push)

    std::string move_number_string();
    std::string san(Move move);
    std::string numbered_san(Move move);
    std::string fen();
    std::string result(); // textual description of the result, if game is over.
    int game_over(); // 0 if not over, 1 if white wins, 2 if black wins, 3 if draw, 4 if win by CastleChess rules.
    
private:
    // --- Individual Piece Move Generators ---
    // These calculate the actual pseudo-legal destination squares
    Bitboard get_pawn_moves(int sq, Color c);
    Bitboard get_knight_moves(int sq, Color c);
    Bitboard get_bishop_moves(int sq, Color c);
    Bitboard get_rook_moves(int sq, Color c);
    Bitboard get_queen_moves(int sq, Color c);
    Bitboard get_king_moves(int sq, Color c);

    // --- Individual Piece Attack Generators ---
    // These generate pure attack rays/patterns
    // Pawns need color to know direction. Sliders need occupancy to find blockers.
    // (Knights and Kings are omitted here since they just use the global arrays now)
    Bitboard get_pawn_attacks(int sq, Color c);
    Bitboard get_bishop_attacks(int sq, Bitboard occ);
    Bitboard get_rook_attacks(int sq, Bitboard occ);
    Bitboard get_queen_attacks(int sq, Bitboard occ);
};
