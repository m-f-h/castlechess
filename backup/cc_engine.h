/* cc_engine.h
 * Definitions for the castlechess engine's evaluation and search functions,
 * and also for Transposition tables.
 * (c) 2026 by MFH
 */
#pragma once
#include "defs.h"
#include "board.h"
#include <vector>
#define ZOBRIST_DEBUG 0

// Transposition Table Flags
enum TTFlag {
    TT_EXACT,       // We searched all moves and found the exact score
    TT_ALPHA,       // Upper bound (all moves failed low, score is <= this)
    TT_BETA         // Lower bound (a move caused a beta cutoff, score is >= this)
};

struct TTEntry { // size = 8 + 4 + (1+1+2) = 16
    uint64_t key;       // The Zobrist hash (to resolve collisions)
    // next 32 bits are shared b/w score & flags
    int32_t score :24;          // The evaluation score
    uint32_t flag :8;           // EXACT, ALPHA, or BETA [2 bits would suffice]
    Move best_move;     // The best move found (crucial for move ordering!)
    uint8_t depth;     // How deep we searched to get this score
    uint8_t epoch;  // to estimate the "age"/ whether position / TT entry became obsolete
};
namespace Engine {
/***** Zobrist Transition Table stuff *****/
    const int TT_SIZE = 8 << 20; // ~ 8 Million entries (approx. 128 MB of RAM)
    extern std::vector<TTEntry> TT; //[TT_SIZE]; //  allocated in engine.cpp

// Clear the hash / transition table before a new game (upon "ucinewgame")
    void clear_tt();
// Resize the hash table before a new game (upon "setoption name Hash...")
    void resize_hash_table(int megabytes);
/***** end Zobrist Transition Table stuff *****/

    const int INFINITY_SCORE = 50000;
    const int CHECKMATE_SCORE = 40000;
    const int CASTLE_WIN_SCORE = 30000;
    extern int evaluation, tt_used, tt_miss, tt_stored;
    extern bool use_tt, reset_tt;
    extern uint64_t nodes_evaluated; // Global counter for nodes evaluated during search,
                                    // reset before each search
                                    // also used for time checking!
    /* "LIMITS" */
    extern int max_depth;
    extern int max_time_ms;
    extern int elapsed_ms;

    // Internal search state
    extern std::chrono::steady_clock::time_point end_time;
    extern bool time_is_up;

    int evaluate(Board& board); // board passed by reference to avoid making unnecessary copies
    int negamax(Board& board, int depth, int alpha, int beta);
    Move search(Board& board, int depth);    // "root search"
    Move think(Board& board);    // main incremental search function
}// end namespace Engine

