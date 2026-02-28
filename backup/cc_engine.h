/* cc_engine.h
 * Definitions for the castlechess engine's evaluation and search functions,
 * and also for Transposition tables.
 * (c) 2026 by MFH
 */
#pragma once
#include "defs.h"
#include "board.h"
#define ZOBRIST_DEBUG 0

namespace Engine {
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
    extern int elapsed_time_ms;

    // Internal search state
    extern std::chrono::steady_clock::time_point end_time;
    extern bool time_is_up;

    int evaluate(Board& board); // board passed by reference to avoid making unnecessary copies
    int negamax(Board& board, int depth, int alpha, int beta);
    Move search(Board& board, int depth);    // "root search"
    Move think(Board& board);    // main incremental search function

//    void clear_tt();// Clear the table before a new game
}// end namespace Engine

// Transposition Table Flags
enum TTFlag {
    TT_EXACT,       // We searched all moves and found the exact score
    TT_ALPHA,       // Upper bound (all moves failed low, score is <= this)
    TT_BETA         // Lower bound (a move caused a beta cutoff, score is >= this)
};

struct TTEntry {
    uint64_t key;       // The Zobrist hash (to resolve collisions)
    int score;          // The evaluation score
    int depth;          // How deep we searched to get this score
    int flag;           // EXACT, ALPHA, or BETA
    Move best_move;     // The best move found (crucial for move ordering!)
};
const int TT_SIZE = 1024 * 1024; // ~1 Million entries (approx 25MB of RAM)
extern TTEntry TT[TT_SIZE];

// Clear the table before a new game
extern void clear_tt();
