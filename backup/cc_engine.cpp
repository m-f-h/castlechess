/* cc_engine.cpp
 * the Castlechess engine: evaluation and search functions.
 * (c) 2026 by MFH
 */

#include "cc_engine.h"
#include <algorithm> // for std::fill

namespace Engine {
bool use_tt = true,  // Global flag to enable/disable Transposition Table usage
    reset_tt = true; // Global flag to enable/disable reset of Transposition Table upon parse_fen()
int tt_used = 0,
    tt_stored = 0; // counter for TT usage (on each "search")

std::vector<TTEntry> TT(TT_SIZE); // initially dimensioned to that size
uint32_t tt_size = TT_SIZE;    // but later our program might resize it
        // so we must use the variable `tt_size` for all later operations

void clear_tt() {
    // Fills the entire vector with zeroed-out TTEntry structs
    std::fill(TT.begin(), TT.end(), TTEntry{});
    /* old code:
    for (int i = 0; i < tt_size; i++) {
        TT[i].key = 0;
        TT[i].depth = -1; 
    }*/
}
int debug=1;
void resize_hash_table(int megabytes) {
    // 1. Calculate how many 16-byte entries fit in the requested MB
    int target_entries = ((megabytes << 10) / sizeof(TTEntry)) << 10;

    // 2. Find the largest power of 2 that is LESS THAN OR EQUAL to target_entries
    // We use the "count leading zeros" builtin.
    tt_size = 1ULL << (63 - __builtin_clzll(target_entries));
    if (debug) std::cerr<<"New TT size = "<<(tt_size>>10)<<" MB.\n";
    
    // 3. Resize the vector using the power-of-2 size
    TT.assign(tt_size, TTEntry{});
    
    // We use global var. tt_size so the search() can use it, viz:
    // int index = hash_key & (Engine::tt_size - 1);
}

int evaluation = 0; // engine evaluation
uint8_t epoch = 0; // for "age" of TT entries

/* "LIMITS" */
int max_depth = 8;
int max_time_ms = 0;
int elapsed_ms = 0;

    
// Internal search state
uint64_t nodes_evaluated = 0; // also used for time checking!
std::chrono::steady_clock::time_point end_time;
bool time_is_up = false;

/* for MVV-LVA :
// 1. A quick helper to get piece values
int get_piece_value(Piece p) {
    // Adjust these cases based on your actual Piece enums
    switch (p) {
        case P: case p: return 100;
        case N: case n: return 300;
        case B: case b: return 300;
        case R: case r: return 500;
        case Q: case q: return 900;
        case K: case k: return 10000;
        default: return 0;
    }
}*/
const int MVV_LVA_VALUE[] = {10, 30, 30, 50, 90, 1000, 10, 30, 30, 50, 90, 1000, 0};

// 'Move' code for BLACK_O_O_O : move_encode(from = e8, to = c8, flags = CASTLE).
#define BLACK_LONG_CASTLE_MOVE (E8 | C8<<6 | CASTLE<<12)
// 2. Score a single move
int score_move(Board& board, Move move) {
    // Black Castling Long scores highest for Black,
    // capture on A8 is highest for White
    // freeing/occupying squares B8..D8 is favourable for Black/White
    // otherwise, for captures use MVV-LVA: most valuable victim - least valuable attacker
    int from_sq = get_from(move);
    int to_sq = get_to(move);
    if (board.side == WHITE) {
        // if white to move, capture of rook @ a8 is instant win (if legal)
        if ( to_sq == A8 ) 
            return CASTLE_WIN_SCORE;
        // otherwise, moving a piece from elsewhere on the squares A8-D8 is good, too
        if ( A8 < to_sq && to_sq < E8 && from_sq > E8) return 500;
        // (the final > E8 to avoid prioritizing moves within these 3 squares)
    }
    else {// if (board.side == BLACK) // Black to move
        if (move == BLACK_LONG_CASTLE_MOVE ) return CASTLE_WIN_SCORE; 
        if (from_sq == A8 || from_sq == E8 ) return -CASTLE_WIN_SCORE; // instant loss
        // Bonus for freeing one of B8..D8
        if (B8 <= from_sq && from_sq <= D8 && to_sq < A8) return 500;
    }
    Piece victim = board.mailbox[to_sq];
    if (victim != NONE) { // For capture, apply MVV-LVA score +1000 so they are considered before quiet moves 
        return 1000 + MVV_LVA_VALUE[victim] - MVV_LVA_VALUE[board.mailbox[from_sq]];
    }
    // If it's a promotion, we also want to search that early
    // if (IS_PROMOTION(move)) return 800; 
    return 0; // Quiet move
}
// First score all moves, to consider them in order of best 
void score_moves(MoveList& list, Board& board, Move tt_move = 0) {
    if (!board.searching) // If not in search mode, we don't really need to score moves,
        //if (interactive) 
        std::cerr << "Scoring moves...\n";      // then all this is mostly for debugging
    for (int i = 0; i < list.count; i++) {
        Move move = list.moves[i];
        list.score[i] = (move == tt_move) ? 100000 // Boost the TT move to be searched first
                        : score_move(board, move);
        //if (!board.searching) 
        //    std::cerr << "score("<<board.san(list.moves[i])<<"="<<list.score[i]<<") ";
    }
    //if (!board.searching && list.current) // this error was never encountered
    //    std::cerr << "Warning: list.current wasn't initialized!";
    //list.current = 0;
}


int evaluate(Board& board) {
    // evaluate the position FROM THE PERSPECTIVE OF THE SIDE TO MOVE.
    // HOWEVER: COMPUTE IT FOR WHITE -- the sign is simply flipped at the very end!
    /***
    // The terminal condition needs not to be ckeched since already "intercepted" in negamax.
    // Check if Black has lost Queenside castling rights.
    if ((board.castling_rights & BLACK_O_O_O) == 0) {
        // If the right is lost, the game is immediately over.
        // The side which made the previous move has won unless it was Black and they did *not* castle.
        // So the side to move has lost unless it's white and black did not castle long.
        return board.side == WHITE && !BITTEST(board.pieces[k], C8) ? CASTLE_WIN_SCORE : -CASTLE_WIN_SCORE;
    }
    ***/
    int score = 0; // Positive = good for White, Negative = good for Black

    // 2. Standard Material 
    // (Assuming P, N, B, R, Q, p, n, b, r, q are your piece enums)
    score += popcount(board.pieces[P]) * 100;
    score += popcount(board.pieces[N]) * 300;
    score += popcount(board.pieces[B]) * 300;
    score += popcount(board.pieces[R]) * 500;
    score += popcount(board.pieces[Q]) * 900;
    
    score -= popcount(board.pieces[p]) * 100;
    score -= popcount(board.pieces[n]) * 300;
    score -= popcount(board.pieces[b]) * 300;
    score -= popcount(board.pieces[r]) * 500;
    score -= popcount(board.pieces[q]) * 900;

    // 3. Castlechess Positional Heuristics
    // Penalty for Black pieces blocking the castling path
    if (BITTEST(board.pieces[n], B8)) score += 200; // Knight blocking b8
    if (BITTEST(board.pieces[b], C8)) score += 200; // Bishop blocking c8
    if (BITTEST(board.pieces[q], D8)) score += 200; // Queen blocking d8

    // Bonus for White attacking the critical castling squares
    // Note: Since is_square_attacked might be a member of Board, we call it via board.
    if (board.is_square_attacked(A8, WHITE)) score += 200; 
    if (board.is_square_attacked(C8, WHITE)) score += 150;
    if (board.is_square_attacked(D8, WHITE)) score += 150;
    if (board.is_square_attacked(E8, WHITE)) score += 200;

    // Return relative to the side to move (NegaMax style)
    return (board.side == WHITE) ? score : -score;
}
/*end evaluate()*/
    
int negamax(Board& board, int depth, int alpha, int beta) {
    // This function must never be called with already lost castling rights = game already over!

    //if (time_is_up) return 0; // Return a neutral score if time is up.
    // we check this below (if it goes 'true'), and immediately after returning
    // from recursive calls, so negamax should never be called when time is up
    // So no need to check it here (above).

    // we deal with ~ 4000 nodes/ms so this is checked once every 2ms:
    if(!(++nodes_evaluated & (8<<10)-1)) { // Check time every ~ 8000 nodes ~ 2 ms
        if ( std::chrono::steady_clock::now() > end_time ) {
            time_is_up = true;
            return 0; // Return a neutral score if time is up. 
            //The search will be cut off at the next opportunity.
        }
    }
/******* begin Transposition table code ******/
    Move tt_move = 0;
    TTEntry* tt_entry = nullptr;
    // TT_SIZE = 2^k => simple modulo for TT index
    if(use_tt){//int tt_index = board.hash & (tt_size - 1);
      tt_entry = &TT[board.hash & (tt_size - 1)];
      // If the hash matches, we've seen this exact board before.
      if (tt_entry->key == board.hash) {
        if (tt_entry->depth >= depth) {
            ++tt_used;
            if (tt_entry->flag == TT_EXACT) return tt_entry->score;
            if (tt_entry->flag == TT_ALPHA && tt_entry->score <= alpha) return alpha;
            if (tt_entry->flag == TT_BETA  && tt_entry->score >= beta)  return beta;
        }
        // lesser depth => we don't use / return the result, but since it was previously
        // considered the best possible move, we'll again consider that best_move
        // *first* among all possible moves. (see below in score_moves())
        tt_move = tt_entry->best_move; // remember it for move ordering:
    } }
/********** end Transposition table code ******/
    if (depth == 0) return evaluate(board);
    int original_alpha = alpha; // Save original alpha for TT flag determination later
    Move best_move = 0; // null / empty move
    int best_eval = -INFINITY_SCORE; // allows to know whether we had >= 1 legal move
    MoveList list;
    board.generate_moves(list);
#if SORT_MOVES
    score_moves(list, board, tt_move); // First score all moves, to consider them in most relevant order 
#endif
    while (Move move = list.next_best()) {
        Board copy = board; 
        copy.make_move(move);
#if ZOBRIST_DEBUG
        uint64_t expected_hash = copy.generate_hash();
        if (copy.hash != expected_hash) {
            std::cerr << "Zobrist hash mismatch after playing move " 
                <<board.numbered_san(move)<<" ("<<move_to_uci(move)<<") !" << std::endl;
            std::cerr << "Expected: " << expected_hash << std::endl;
            std::cerr << "Actual:   " << copy.hash << std::endl;
            std::cerr << "FEN: " << copy.fen() << std::endl;
            exit(1);
        }
#endif
        if (copy.king_can_be_captured()) continue; // skip illegal moves (leaving king in check)
        int eval;
        if (!(copy.castling_rights & BLACK_O_O_O)) {
            // The side which made the previous move has won unless it was Black and they did *not* castle.
            if (board.side == WHITE // then necessarily the move was : capture A8
             || move == BLACK_LONG_CASTLE_MOVE) {
                best_eval = CASTLE_WIN_SCORE + depth; // Instant win - No need to search deeper.
                best_move = move;
                goto found_best_move; // break out of both loops immediately
            }
            else
            // Black lost castling rights without castling long: instant loss = suicide for Black
                eval = -CASTLE_WIN_SCORE - depth; // Worst possible move, shouldn't really be considered.
                // but that's still > -oo, so this will be considered as "best move"! :-(
                // Obviouly we go on looking for other moves !!
        }
        else {
            // Recursive call (flip alpha and beta, negate the result)
            eval = -negamax(copy, depth - 1, -beta, -alpha);
            if (time_is_up) return 0;
        }
        if (eval > best_eval) {    // Found a better move
            best_eval = eval;
            best_move = move;
            // If we found a forced win or if eval >= beta, there's no need to look at other moves.
            if (eval >= beta)
                goto found_best_move; // Return immediately
            if (eval > alpha) {
                alpha = eval;
            }
        }
    }
    // 3. Standard Chess Terminal Conditions (if game didn't end via Castlechess rules)
    // Did we find a legal move ? Then neither checkmate nor stalemate.
    if (best_eval == -INFINITY_SCORE) { // <=> we didn't find a legal move
        if (board.is_check())
            best_eval = -CHECKMATE_SCORE - depth; // Checkmate (bonus for faster mate: 'depth' = *remaining* depth!)
        else 
            best_eval = 0; // Stalemate
    }
found_best_move:
    /******* begin Transposition table code ******/
    if (tt_entry) { // && !time_is_up is explicit - else we returned earlier!
        // TT replacement strategy : shall we overwrite the entry?
        // * if it's the exact same position -> yes: Replace (We have greater depth now,
        //      otherwise we'd already have returned the move at the beginning.)
        // * if it's completely empty -> yes: Replace.
        // * if a different position is stored there,
        //      overwrite if bad epoch or if greater new depth.

        // So, the only case where we'd *NOT* update, is when the entry
        // corresponds to a *different* position which was evaluated
        // at *greater depth* AND the *same epoch*. 
        if ( tt_entry->key != board.hash ) { // collision 
            // i.e., the entry refers to a different position
            // (or it's empty, but then epoch & depth will be zero, too,
            //  so we *will* overwrite & not return immediately.)
            if ( tt_entry->depth > depth && tt_entry->epoch == epoch ) 
               goto tt_code_done; // don't overwrite. 
            // We've decided to store *our* position in this entry
            // which was empty or used by a *different* position.
            tt_entry->key = board.hash;
        // Now we know that we have the same position, but now greater depth,
        // else we'd have returned it at beginning - unless the ALPHA or BETA
        // wasn't good enough, in which case it's indeed best to overwrite !)
        }// (if key == hash, no need to overwrite)
        // Save the other information to the table!
        tt_entry->best_move = best_move;
        tt_entry->score = best_eval;
        tt_entry->depth = depth;
        tt_entry->epoch = epoch;
        // Determine what kind of score we just found
        tt_entry->flag = best_eval <= original_alpha ? TT_ALPHA // We failed low
                       : best_eval >= beta ? TT_BETA      // We caused a cutoff
                       : TT_EXACT;  // exact score
        ++tt_stored;
    }/******* end Transposition table code ******/
tt_code_done:
    return best_eval;
}

Move search(Board& board, int depth) {
    // This is the "root search function" which returns the best move
    // (in contrast to negamax() which returns the score).
    // However, it is no more a "top level entry point",
    // but now (exclusively) called by `think()` which sets up parameters
    // for time & depth management, node_count etc.
    if (time_is_up || board.game_over()) return 0;
    // note: our game_over() is extremely inefficient (re-computes MoveList !)

    MoveList list;
    board.generate_moves(list);
#if SORT_MOVES
    score_moves(list, board); // First score all moves, to consider them in most relevant order 
#endif
    int alpha = -INFINITY_SCORE;
    int beta = INFINITY_SCORE;
    Move best_move = 0; // null / empty move
    int eval;
    while (Move move = list.next_best()) {
        Board copy = board;
        copy.make_move(move);
        if (copy.king_can_be_captured()) continue; // skip illegal move (leaving king in check)
        // NOTE: assume White is in check but could take the rook on a8, winning by CastleChess rules.
        // It is debatable whether white is allowed to make this classically illegal move.
        // In the present variant, we don't allow such classically illegal moves,
        // just like we don't allow to castle when e.g. the destination square is attacked,
        // and we also write ...O-O-O# meaning "Black has won" *after* this move was completed.
        // This is in contrast to Qh5# meaning "White has won" because Black has no more any legal move.
        // Therefore we check for the CastleChess win condition only *after* checking whether the move is legal.
        if (!(copy.castling_rights & BLACK_O_O_O)) { // Check for CastleChess win condition
            // If the right is lost, the game is over.
            if (board.side == WHITE // necessarily: && get_to(move) == A8
             || // unneeded:  board.side == BLACK &&
                move == BLACK_LONG_CASTLE_MOVE) {
                best_move = move;
                alpha = CASTLE_WIN_SCORE + depth;                
                break;
            }
            // Black lost castling rights without castling long: this is an instant loss for Black
            // They should avoid this suicide move at all cost (~ consider it illegal,
            // but we can't do  this in case it's the only legal move,
            // or the program would detect "no legal move" without being in check => stalemate.
            // But we do want that White may "sacrifice" their Q to oblige Black to capture it, so White wins. 
            eval = -CASTLE_WIN_SCORE - depth; // the sooner (=> more depth remaining) the worse
        }
        else eval = -negamax(copy, depth - 1, -beta, -alpha);
        if (time_is_up) return 0;
        if (eval > alpha) {
            alpha = eval;
            best_move = move;
        }
    }
    evaluation = alpha; // this is in Engine, not in board!

    /*store the best_move in the TT*/
    if (use_tt){ // !time_is_up is implicit
        TTEntry* tt_entry = &TT[board.hash & (tt_size - 1)];
        /*if ( tt_entry->key == board.hash 
          || tt_entry->epoch != epoch 
          || depth >= tt_entry->depth) {*/ // we don't care, we overwrite ! 
        tt_entry->key = board.hash;
        tt_entry->best_move = best_move;
        tt_entry->score = alpha;
        tt_entry->flag = TT_EXACT;  // exact score
        tt_entry->depth = depth;
        tt_entry->epoch = epoch;
    }   
    return best_move; // = 0 means no legal move found (should be game over)
}// end search()

std::vector<Move>PV;
void extract_and_print_pv(int depth, Board board) { // makes a copy of the board
    int max_pv_length = 64; // Safety limit to prevent infinite loops from TT cycles    
    std::cout << " pv ";
    if (PV.size() != 1) {std::cerr<<"ERROR: PV is not of size 1!\n"; return;}
    for (int ply = 0; ply <= depth; ++ply) {
        // Print the move
        if (board.side==WHITE || !ply) 
            std::cout << board.move_number_string() ;
        std::cout << board.san(PV[ply]) << " "; // e.g., "e2e4"
        // Play the move on the board to advance to the next hash
        board.make_move(PV[ply]);
  
        // Probe the TT for the current board hash
        TTEntry* entry = &Engine::TT[board.hash & (Engine::tt_size - 1)];
        // If it's a miss, or there's no move stored, the PV branch is broken: Stop!
        if (entry->key != board.hash || entry->best_move == 0 
            || entry->flag != TT_EXACT) break;

        // We found the PV move! Save it.
        PV.push_back(entry->best_move); // e.g., "e2e4"
    }
    //std::cout << std::endl; // don't output endl !
}
Move think(Board& board) {// think using iterative_deepening
    Move best_move = 0;
    board.searching = true; // Set searching flag to true for move generation optimizations
    tt_used = tt_stored = nodes_evaluated = 0; // Reset node count for this search
    time_is_up = false;
    // Set the alarm clock!
    auto start_time = std::chrono::steady_clock::now();
    // if max_time is not set, use 5000 ms
    if (!max_time_ms) max_time_ms = 5000;
    end_time = start_time + std::chrono::milliseconds(max_time_ms);

    elapsed_ms = 0; // for time management based on branching factor
    int time_previous_depth = 0; // for time management based on branching factor

    // Compute the epoch: a counter that advances iff a pawn was pushed or
    // a piece was captured on the real board. If that didn't happen,
    // positions evaluated earlier remain relevant ; otherwise not.
    epoch = board.fullmove_number*2-board.halfmove_clock-(board.side==WHITE);

    for (int depth = 1; !time_is_up && depth <= max_depth; depth++) {
        // 3. Time Prediction for the NEXT depth
        // Note: time_previous_depth is actually the time from depth-2,
        // while 'elapsed_ms' is the time from depth-1
        int predicted_next_time =
            (time_previous_depth > 10) // avoid dividing by 0 or tiny ~ 1ms times
            ? ((uint64_t)elapsed_ms * elapsed_ms) / time_previous_depth
            : elapsed_ms*2 ; // Fallback for the first couple of shallow depths
/* technically we should maybe compute the time deltas... that is,
   elapsed + (elapsed-previous)² / (previous-before) > max 
   But it may be OK that way. Assume
   depth:   1       2       3       4       5
   delta    10ms    20ms    40ms    80ms    160ms   320     640
   elapsed  10      30      70      150     310ms   630     1270
   e²/prev  -       -       90      160     320ms   640     1280
   
   delta    10ms    30ms    90ms    270ms   810ms   2430    7290
   elapsed  10      40      130     400    1210ms   2640
   e²/prev  -       -       422     1230    320ms   640     1280
   The larger the factor, the less error we make by neglecting the previous time.
 */
        // The SOFT Limit check:
        // Do we have enough budget left to safely finish the next depth?
        if (predicted_next_time > max_time_ms) {
            // We probably can't finish it. Stop now while we're ahead!
            break; 
        }
        //now save old 'elapsed' to previous
        time_previous_depth = elapsed_ms; // for time management based on branching factor

        // Do the search for the current depth
        Move best_move_this_depth = search(board, depth);// returns 0 if time_is_up
        // Even if time_is_up, we want to print the information below.
        // Recall: Engine::elapsed_ms is a "global" variable in this namespace,
        //         available to the main program.
        elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                              std::chrono::steady_clock::now() - start_time).count();
        // print UCI info string, => GUI can update. TOTO: add option to disable in interactive mode
        std::cout << "info depth " << depth << " score cp " << evaluation
                  << " nodes " << nodes_evaluated << " time " << elapsed_ms;
        if(!time_is_up) { // we display the pv (principal variation) only if the search did terminate
            best_move = best_move_this_depth;
            PV.assign({best_move});
            //std::cout << " pv " << move_to_uci(best_move); // e.g., "e8c8 c4c5 e6d5"
            extract_and_print_pv(depth, board);
        } std::cout << std::endl;
    }
    board.searching = false; // Reset searching flag
    return best_move;
}
} // end namespace Engine
