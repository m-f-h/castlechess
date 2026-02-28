/* board.cpp
   (c) 2026 by MFH

   This file implements the core logic for 
   move generation and determining if a square is attacked in a chess engine.

 */
#include "board.h"
#include "cc_engine.h" // for Engine::reset_tt in parse_FEN
#include <iostream>
#include <cstdint>
#include <string>
#include <sstream>
#include <random>

uint64_t zobrist_pieces[12][64];
uint64_t zobrist_side;
uint64_t zobrist_ep[64]; // one key for each possible en passant square
uint64_t zobrist_castling[4];
void init_zobrist() {
    // We use a fixed seed (12345) so the random numbers are the exact 
    // same every time the engine runs. This is crucial for debugging!
    std::mt19937_64 rng(12345); 
    for (int p = 0; p < 12; p++) {
        for (int sq = 0; sq < 64; sq++) {
            zobrist_pieces[p][sq] = rng();
        }
    }
    zobrist_side = rng();
    for (int i = 64; i;)
        zobrist_ep[--i] = rng(); // Initialize en passant hash keys
    for (int i = 0; i < 4; i++)
        zobrist_castling[i] = rng();
}

uint64_t Board::generate_hash() {// compute zobrist hash
    uint64_t h = 0;
    // 1. Pieces
    for (int sq = 0; sq < 64; sq++) {
        Piece p = mailbox[sq];
        if (p != NONE) {
            h ^= zobrist_pieces[p][sq];
        }
    }
    // 2. Side to move
    if (side == BLACK) h ^= zobrist_side;
    // 3. En Passant (use the file of the square)
    if (enPassantSquare != NO_SQUARE) { // Or however you represent "no EP square"
        h ^= zobrist_ep[enPassantSquare]; // XOR the file index of the EP square
    }
    // 4. Castling Rights (checking your specific bits)
    if (BITTEST(castling_rights, 0))  h ^= zobrist_castling[0]; // A1
    if (BITTEST(castling_rights, 7))  h ^= zobrist_castling[1]; // H1
    if (BITTEST(castling_rights, 56)) h ^= zobrist_castling[2]; // A8
    if (BITTEST(castling_rights, 63)) h ^= zobrist_castling[3]; // H8
    return h;
}

std::string square_to_algebraic(int sq) {
    if (sq == NO_SQUARE) return "-";
    std::string s = "a1";
    s[0] += sq % 8; // file
    s[1] += sq / 8; // rank
    return s;
}

// A quick helper to print moves like "e2e4"
std::string move_to_uci(Move move) {
    auto uci = square_to_algebraic(get_from(move))
             + square_to_algebraic(get_to(move));
    int promote_piece = get_flags(move) & PROMOTE_MASK;
    if (promote_piece && promote_piece <= PROMOTE_QUEEN) {
        return uci + PIECE_SYMBOL[promote_piece];
    }
    return uci;
}

int square_from_algebraic(std::string s) {
    int file = s[0] - 'a';
    int rank = s[1] - '1';
    return rank * 8 + file;
}
Piece piece_from_char(char c) {
    for (int i = 0; i < 12; i++)
        if (PIECE_SYMBOL[i] == c) return static_cast<Piece>(i);
    return NONE;
}
Move move_from_uci(std::string uci) {// NOTE: this may lack some flags!
    // (Here we don't know the board, so we can't know whether c2c4 is a pawn or rook move.)
    if (uci.length() < 4) return 0; // Invalid move string
    int from = square_from_algebraic(uci);
    int to = square_from_algebraic(uci.substr(2));
    // Promotion? recall: our PROMOTE_xxx flags equal N,B,R,Q. 
    // So we can directly use the piece type as the promotion flag.
    int flags = uci.length() > 4 ? piece_from_char(uci[4]) : 0;
    return encode_move(from, to, flags % PIECE_MOD);
}

std::string Board::move_number_string(){
    return std::to_string(fullmove_number)+(side==WHITE?". ":"... ");
}
std::string Board::numbered_san(Move move) {
    return move_number_string() + san(move);
}
std::string Board::san(Move move){
    int from = get_from(move), to = get_to(move), flags = get_flags(move);
    if (flags == CASTLE)
        return to > from ? "O-O" : "O-O-O";
    // check ? // TODO!
    std::string SAN = move_to_uci(move);
    if (SAN.length()>4) { // Promotion move
        SAN += std::toupper(SAN.back()); // Append promotion piece in uppercase
        SAN[4] = '='; // Change the promotion indicator to '='
    }
    int piece = mailbox[from]; // Get piece type ignoring color
    if (flags & CAPTURE) {
        SAN[1]='x'; // change rank of from-square to 'x' to indicate capture
        if (piece % PIECE_MOD == P) return SAN;
    }
    else if (piece % PIECE_MOD == P) return SAN.substr(2);
    else SAN.erase(1,1); // non-capture
    int pieceType = piece % PIECE_MOD;
    SAN[0] = PIECE_SYMBOL[pieceType];
    if (popcount(pieces[piece]) > 1) { // so it can't be a king!
        // more than 1 such piece on the board: check whether disambiguation is needed
        Bitboard other_attacks = pieceType==N ? knight_attacks[to]
            : pieceType==R ? get_rook_attacks(to, occupancy[side])
            : pieceType==Q ? get_queen_attacks(to, occupancy[side])
            : get_bishop_attacks(to, occupancy[side]);
        if (other_attacks & pieces[piece] ^ 1ULL << from) // remove the piece itself
            SAN.insert(1,square_to_algebraic(from));
    }
    return SAN;
}

// game_over() returns 0 if not over, 1 if white wins, 2 if black wins, 3 if draw,
// 4 if termination by CastleChess rules. 
// 4|3 = 7 means illegal position (e.g., a missing king => board not yet set up!?)
int Board::game_over() { // for use by "end user", not in engine search
    if (!pieces[K] || !pieces[k]) return ILLEGAL_POSITION; // board not yet set up ?
    // Check for CastleChess win condition first
    if ((castling_rights & BLACK_O_O_O) == 0) 
        return (mailbox[C8]==k) ? CASTLECHESS_WIN | BLACK_WINS : CASTLECHESS_WIN | WHITE_WINS;  
    // Check for checkmate or stalemate
    MoveList list;
    generate_moves(list); // note that this is the pseudo-legal move generator
    for (int i = 0; i < list.count; i++) {
        Board copy = *this;
        copy.make_move(list.moves[i]);
        if (! copy.king_can_be_captured())
            return NOT_OVER; // a legal move exists, so the game is not over (even if we're in check)
    }
    // No legal moves available: Check if it's checkmate or stalemate
    return is_check() ? (side == WHITE ? BLACK_WINS : WHITE_WINS) : DRAW; // If king can be captured, it's checkmate. Otherwise, it's stalemate (draw).
}
std::string Board::result() {
    switch(int res = game_over()){
        case NOT_OVER: return "Game is not over.";
        case DRAW: return "Draw.";
        case ILLEGAL_POSITION: return "Illegal position: missing king!";
        default: return std::string(res & WHITE_WINS ? "White" : "Black") + " wins by " +
                (res & CASTLECHESS_WIN ? "CastleChess rules" : "checkmate.");
    }
}

// Define the two global arrays for leapers king & knight
Bitboard knight_attacks[64];
Bitboard king_attacks[64];

void init_tables() {
    init_zobrist();
    for (int sq = 0; sq < 64; sq++) {// for each square...
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
// --- Bishop Attacks (Diagonals) ---
Bitboard Board::get_bishop_attacks(int sq, Bitboard occ) {
    Bitboard attacks = 0;
    int tr = sq / 8; // Target rank
    int tf = sq % 8; // Target file
    int r, f;

    // Up-Right (+9)
    for (r = tr + 1, f = tf + 1; r <= 7 && f <= 7; r++, f++) {
        BITSET(attacks, r * 8 + f);
        if (BITTEST(occ, r * 8 + f)) break; // Stop if we hit a blocker
    }
    // Up-Left (+7)
    for (r = tr + 1, f = tf - 1; r <= 7 && f >= 0; r++, f--) {
        BITSET(attacks, r * 8 + f);
        if (BITTEST(occ, r * 8 + f)) break;
    }
    // Down-Right (-7)
    for (r = tr - 1, f = tf + 1; r >= 0 && f <= 7; r--, f++) {
        BITSET(attacks, r * 8 + f);
        if (BITTEST(occ, r * 8 + f)) break;
    }
    // Down-Left (-9)
    for (r = tr - 1, f = tf - 1; r >= 0 && f >= 0; r--, f--) {
        BITSET(attacks, r * 8 + f);
        if (BITTEST(occ, r * 8 + f)) break;
    }
    
    return attacks;
}

// --- Rook Attacks (Straights) ---
Bitboard Board::get_rook_attacks(int sq, Bitboard occ) {
    Bitboard attacks = 0;
    int tr = sq / 8;
    int tf = sq % 8;
    int r, f;

    // Up (+8)
    for (r = tr + 1; r <= 7; r++) {
        BITSET(attacks, r * 8 + tf);
        if (BITTEST(occ, r * 8 + tf)) break;
    }
    // Down (-8)
    for (r = tr - 1; r >= 0; r--) {
        BITSET(attacks, r * 8 + tf);
        if (BITTEST(occ, r * 8 + tf)) break;
    }
    // Right (+1)
    for (f = tf + 1; f <= 7; f++) {
        BITSET(attacks, tr * 8 + f);
        if (BITTEST(occ, tr * 8 + f)) break;
    }
    // Left (-1)
    for (f = tf - 1; f >= 0; f--) {
        BITSET(attacks, tr * 8 + f);
        if (BITTEST(occ, tr * 8 + f)) break;
    }

    return attacks;
}

// --- Queen Attacks (Bishop + Rook) ---
Bitboard Board::get_queen_attacks(int sq, Bitboard occ) {
    return get_bishop_attacks(sq, occ) | get_rook_attacks(sq, occ);
}

// --- The Optimized Attack Detection ---
bool Board::is_square_attacked(int sq, Color attacker_side) {
    // A square is attacked by an enemy pawn if a pawn of OUR color 
    // sitting on that square could "attack" the enemy pawn's actual location.
    Color defender_side = attacker_side == WHITE ? BLACK : WHITE;
    int enemy_offset = (attacker_side == WHITE) ? 0 : 6; // P/N/B/R/Q/K vs p/n/b/r/q/k
    
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


void Board::generate_moves(MoveList& list) {
    list.count = 0; // Reset list
    Color us = side;
    Bitboard occ_both = occupancy[BOTH];// not used often

    // --- CASTLING --- (done 1st because for black => terminating move)
    if (us == WHITE) {
        // White Kingside (O-O)
        // King at E1 (4), transits F1 (5), lands G1 (6). Rook at H1 (7).
        if (castling_rights & WHITE_O_O) { // Use whatever constant you have for White Kingside right
            // F1 and G1 must be empty
            if (!(occ_both >> F1 & 3)) {// F1 and G1 must be empty
                // E1, F1, and G1 cannot be attacked
                if (!is_square_attacked(E1, BLACK) && 
                    !is_square_attacked(F1, BLACK) && 
                    !is_square_attacked(G1, BLACK)) {
                    list.add_move(encode_move(E1, G1, CASTLE)); // Or your specific castling flag
                }
            }
        }
        // White Queenside (O-O-O)
        if (castling_rights & WHITE_O_O_O) {
            // D1, C1, and B1 must be empty
            if (!(occ_both >> B1 & 7)) { // B1, C1, D1 must be empty
                // E1, D1, and C1 cannot be attacked (B1 is allowed to be attacked!)
                if (!is_square_attacked(E1, BLACK) && 
                    !is_square_attacked(D1, BLACK) && 
                    !is_square_attacked(C1, BLACK)) {
                    list.add_move(encode_move(E1, C1, CASTLE));
                }
            }
        }
    } else {
        // Black Queenside (O-O-O)
        if (castling_rights & BLACK_O_O_O) {
            if (!(occ_both >> B8 & 7)) { // B8, C8, D8 must be empty
                if (!is_square_attacked(E8, WHITE) && 
                    !is_square_attacked(D8, WHITE) && 
                    !is_square_attacked(C8, WHITE)) {
                    list.add_move(encode_move(E8, C8, CASTLE));
                    if (searching) return; // If we're searching for the best move, we can stop here
                    // since castling is the best move in this position.
                }
            }
        }
        // Black Kingside (O-O)
        // King at E8 (60), transits F8 (61), lands G8 (62).
        if (castling_rights & BLACK_O_O) {
            if (!(occ_both >> F8 & 3)) { // F8 and G8 must be empty
                if (!is_square_attacked(E8, WHITE) && 
                    !is_square_attacked(F8, WHITE) && 
                    !is_square_attacked(G8, WHITE)) {
                    list.add_move(encode_move(E8, G8, CASTLE));
                }
            }
        }
    }

    Color them = (us == WHITE) ? BLACK : WHITE;
    Bitboard occ_them = occupancy[them],
             not_us = ~occupancy[us];
    
    // --- HELPER FUNCTIONS ---
    // The [&] tells C++ to automatically access 'list', 'not_us', etc., from the surrounding scope
    auto add_moves = [&](int sq, Bitboard moves) {
        while (moves) {
            int to = GET_LSB(moves);
            list.add_move(encode_move(sq, to, 
                        BITTEST(occ_them, to) ? CAPTURE : QUIET_MOVE));
            POP_LSB(moves);
        }
    };
    auto make_leaper_moves = [&](Bitboard pieces_bb, const Bitboard attacks_array[]) {
        while (pieces_bb) {
            add_moves(GET_LSB(pieces_bb),
                      attacks_array[GET_LSB(pieces_bb)] & not_us);
            POP_LSB(pieces_bb);
        }
    };
    auto make_slider_moves = [&](Bitboard pieces_bb, auto get_attacks) {
        while (pieces_bb) {
            add_moves(GET_LSB(pieces_bb),
                      (this->*get_attacks)(GET_LSB(pieces_bb), occ_both) & not_us);
            POP_LSB(pieces_bb);
        }
    };
    int offset = (us == WHITE) ? 0 : p-P; // P/N/B/R/Q/K vs p/n/b/r/q/k
    make_leaper_moves(pieces[N + offset], knight_attacks);
    make_leaper_moves(pieces[K + offset], king_attacks);
    make_slider_moves(pieces[B + offset], &Board::get_bishop_attacks);
    make_slider_moves(pieces[R + offset], &Board::get_rook_attacks);
    make_slider_moves(pieces[Q + offset], &Board::get_queen_attacks);

    // --- PAWNS (And Castling) ---

    Bitboard empty_sqs = ~occ_both;
    
    auto add_promotions = [&](Move move) {
        list.add_move(move | PROMOTE_QUEEN << 12);  
        list.add_move(move | PROMOTE_ROOK  << 12);
        list.add_move(move | PROMOTE_BISHOP << 12);
        list.add_move(move | PROMOTE_KNIGHT << 12);
    };

    if (us == WHITE) {
        Bitboard pawns = pieces[P];
        // 1. Single Pushes (Shift up 8, must land on empty square)
        Bitboard pushes = (pawns << 8) & empty_sqs;
        Bitboard double_pushes = (pushes & RANK_3) << 8 & empty_sqs;

        while (pushes) {
            int to = GET_LSB(pushes);
            if (to < A8) // Normal single push (not promotion)
                list.add_move(encode_move(to - 8, to, QUIET_MOVE));
            else // Promotion single push
                add_promotions(encode_move(to - 8, to, QUIET_MOVE));
            POP_LSB(pushes);
        }
        
        // 2. Double Pushes (Take the successful single pushes that landed on Rank 3, shift up 8 again)
        while (double_pushes) {
            int to = GET_LSB(double_pushes);
            list.add_move(encode_move(to - 16, to, DOUBLE_PAWN_PUSH));
            POP_LSB(double_pushes);
        }
        // ... Captures,
        // --- 3. Captures Left (North-West, +7) ---
        // Mask out the A-file, shift up 7, must land on an enemy piece
        Bitboard captures_left = ((pawns & NOT_A_FILE) << 7) & occ_them;
        while (captures_left) {
            int to = GET_LSB(captures_left);
            if (to < 56)
                list.add_move(encode_move(to - 7, to, CAPTURE));
            else
                add_promotions(encode_move(to - 7, to, CAPTURE));
            POP_LSB(captures_left);
        }
        // --- 4. Captures Right (North-East, +9) ---
        // Mask out the H-file, shift up 9, must land on an enemy piece
        Bitboard captures_right = ((pawns & NOT_H_FILE) << 9) & occ_them;
        while (captures_right) {
            int to = GET_LSB(captures_right);
            if (to < 56)
                list.add_move(encode_move(to - 9, to, CAPTURE));
            else
                add_promotions(encode_move(to - 9, to, CAPTURE));
            POP_LSB(captures_right);
        }
        // --- 5. En Passant ---
        if (enPassantSquare != NO_SQUARE) {
            Bitboard ep_bb = 1ULL << enPassantSquare;
            
            if (Bitboard ep_left  = ((pawns & NOT_A_FILE) << 7) & ep_bb) {
                int to = GET_LSB(ep_left);
                list.add_move(encode_move(to - 7, to, EN_PASSANT));
            }
            if (Bitboard ep_right = ((pawns & NOT_H_FILE) << 9) & ep_bb) {
                int to = GET_LSB(ep_right);
                list.add_move(encode_move(to - 9, to, EN_PASSANT));
            }
        }
    } else { // us == BLACK //
        Bitboard pawns = pieces[p];
        Bitboard pushes = (pawns >> 8) & empty_sqs;
        // Double Pushes = successful single pushes that landed on Rank 6, shift by 8 again
        Bitboard double_pushes = ((pushes & RANK_6) >> 8) & empty_sqs;
        while (pushes) {
            int to = GET_LSB(pushes);
            if (to >= A2) // Normal single push (not promotion) 
                list.add_move(encode_move(to + 8, to, QUIET_MOVE));
            else // Promotion single push
                add_promotions(encode_move(to + 8, to, QUIET_MOVE));
            POP_LSB(pushes);
        }
        while (double_pushes) {
            int to = GET_LSB(double_pushes);
            list.add_move(encode_move(to + 16, to, DOUBLE_PAWN_PUSH));
            POP_LSB(double_pushes);
        }
        // ... Captures: Left (SE, -7) ---
        Bitboard captures_left = ((pawns & NOT_H_FILE) >> 7) & occ_them;
        while (captures_left) {
            int to = GET_LSB(captures_left);
            if (to >= A2) // Normal capture (not promotion)
                list.add_move(encode_move(to + 7, to, CAPTURE));
            else
                add_promotions(encode_move(to + 7, to, CAPTURE));
            POP_LSB(captures_left);
        }
        // --- Captures Right (SW, -9) ---
        Bitboard captures_right = ((pawns & NOT_A_FILE) >> 9) & occ_them;
        while (captures_right) {
            int to = GET_LSB(captures_right);
            if (to >= A2) 
                list.add_move(encode_move(to + 9, to, CAPTURE));
            else
                add_promotions(encode_move(to + 9, to, CAPTURE));
            POP_LSB(captures_right);
        }
        // --- 5. En Passant ---
        if (enPassantSquare != NO_SQUARE) {
            Bitboard ep_bb = 1ULL << enPassantSquare;
            if (Bitboard ep_left  = ((pawns & NOT_H_FILE) >> 7) & ep_bb) {
                int to = GET_LSB(ep_left);
                list.add_move(encode_move(to + 7, to, EN_PASSANT));
            }
            if (Bitboard ep_right = ((pawns & NOT_A_FILE) >> 9) & ep_bb) {
                int to = GET_LSB(ep_right);
                list.add_move(encode_move(to + 9, to, EN_PASSANT));
            }
        }
    }
}

void Board::make_move(Move move) {
    int from = get_from(move);
    int to   = get_to(move);
    int flags = get_flags(move); // For castling, promotions, etc.

    Bitboard to_bb = 1ULL << to,
            from_bb = 1ULL << from,
            move_bb = from_bb | to_bb;

    const Piece piece = mailbox[from], captured = mailbox[to];
    const Color us = side, them = (us == WHITE) ? BLACK : WHITE;

    // In all cases: 
    // Update Mailbox for the "move" part - capture etc handled later
    mailbox[from] = NONE;
    mailbox[to] = piece;
    hash ^= zobrist_pieces[piece][from]; // Remove piece from old square in hash
    hash ^= zobrist_pieces[piece][to]; // Add piece to new square in hash
    
    pieces[piece] ^= move_bb;   // our piece moves from -> to
    occupancy[us] ^= move_bb;   // same for our occupancy 
    // occupancy[BOTH] will be updated later depending on the move type 

    // Store old castling rights for zobrist update
    Bitboard old_castling_rights = castling_rights;

    if (captured != NONE ) {// captures other than EP captures (handled separately below)      
        pieces[captured] ^= to_bb;       // Remove from enemy piece bitboard
        hash ^= zobrist_pieces[captured][to]; // Remove captured piece from hash

        occupancy[them]  ^= to_bb;       // Remove from enemy occupancy
        // general occupancy changes only on 'from' (1->0), not on 'to'
        occupancy[BOTH]  ^= from_bb;
    }
    else if (flags == EN_PASSANT) {
        // The captured pawn (to disappear) is on RANK(from) + FILE(to) :
        int ep_capture_sq = from & 7*8 | to & 7;
        // that's NOT the same as enPassantSquare !!! (= the "to" square)
        mailbox[ep_capture_sq] = NONE; // Clear the captured pawn from the mailbox
        hash ^= zobrist_pieces[p - piece][ep_capture_sq]; // Remove the captured pawn from the hash

        // update piece and occupancy Bitboards:
        Bitboard ep_capture_bb = 1ULL << ep_capture_sq;
        // The captured pawn is p - piece.
        pieces[p - piece] ^= ep_capture_bb;// remove it

        occupancy[them]   ^= ep_capture_bb;
        occupancy[BOTH]   ^= ep_capture_bb | move_bb;
    } 
    else if (flags == CASTLE) {
        // Move the rook as well
        int rook_to = (from+to)/2, 
            rook_from = to>from ? to + 1 : to - 2;
        Piece rook = side==WHITE ? R : r;

        mailbox[rook_to] = rook;
        mailbox[rook_from] = NONE;
        hash ^= zobrist_pieces[rook][rook_from];
        hash ^= zobrist_pieces[rook][rook_to];
        
        Bitboard rook_move_bb = (1ULL << rook_from) | (1ULL << rook_to);
        pieces[rook] ^= rook_move_bb; // Move the rook
        occupancy[us] ^= rook_move_bb;             // Update our occupancy for the rook move
        occupancy[BOTH] ^= rook_move_bb | move_bb; // Update general occupancy for R & K
    } 
    else occupancy[BOTH] ^= move_bb; // update for non-capture, non-EP, non-castling moves

    if (enPassantSquare != NO_SQUARE)
        // Clear the en passant square from the hash
        hash ^= zobrist_ep[enPassantSquare];

    if (flags == DOUBLE_PAWN_PUSH)
        hash ^= zobrist_ep[enPassantSquare = (from + to)/2];
    else enPassantSquare = NO_SQUARE; // reset EP square if not a double pawn push

    castling_rights &= 
        piece == K ? ~(WHITE_O_O_O | WHITE_O_O) : // Lose both white castling rights
        piece == k ? ~(BLACK_O_O_O | BLACK_O_O) : // Lose both black castling rights
        ~ move_bb; // clear rights if from or to is a rook square 

    // promotion
    if (piece == P && to >= 56 || piece == p && to < 8) {
        Piece promo_piece = (Piece)((flags & 7) + piece); // piece: P=0 (white) or p=6 (black) added as "side offset"
        mailbox[to] = promo_piece; // Update mailbox for promotion
        pieces[piece] ^= to_bb; // Remove the pawn
        pieces[promo_piece] |= to_bb; // Add the promoted piece
        hash ^= zobrist_pieces[piece][to];    // Pick up the PAWN from the to square where it just moved to
        hash ^= zobrist_pieces[promo_piece][to];// Put down the PROMOTED PIECE on the destination square
    }

    // Switch sides
    side = them;
    
    // Increment move counters
    if (us == BLACK) fullmove_number++;
    
    if (piece % PIECE_MOD == P || captured != NONE) {
        halfmove_clock = 0; // Reset 50-move rule on pawn move or capture
    } else {
        halfmove_clock++;
    }

    if (old_castling_rights ^= castling_rights) { // it's no more "old" but "xor'ed" castling rights
        if (BITTEST(old_castling_rights, A1))  hash ^= zobrist_castling[0]; // A1
        if (BITTEST(old_castling_rights, H1))  hash ^= zobrist_castling[1]; // H1
        if (BITTEST(old_castling_rights, A8))  hash ^= zobrist_castling[2]; // A8
        if (BITTEST(old_castling_rights, H8))  hash ^= zobrist_castling[3]; // H8
    }
    // ***** remaining zobrist stuff *****
    hash ^= zobrist_side;     // Flip the side to move in the hash
}// end make_move

void Board::reset(){
    // 1. Clear the board completely before setting it up
    for (int i = 0; i < 12; i++) pieces[i] = 0ULL;
    update_occupancy(); 
    castling_rights = 0;
    enPassantSquare = NO_SQUARE;
    halfmove_clock = 0;
    fullmove_number = 1;
    side = WHITE; searching = false;
    for(int i=0; i<64; i++) mailbox[i] = NONE; // empty board. use parse_fen to set up the standard position.
    if (Engine::reset_tt) clear_tt();
}

void Board::update_occupancy(){
    occupancy[WHITE] = 0ULL;
    occupancy[BLACK] = 0ULL;
    // OR together all White pieces (assuming indices 0 to 5)
    for (int i = 0; i < 6; i++) {
        occupancy[WHITE] |= pieces[i];
    }
    // OR together all Black pieces (assuming indices 6 to 11)
    for (int i = 6; i < 12; i++) {
        occupancy[BLACK] |= pieces[i];
    }
    occupancy[BOTH] = occupancy[WHITE] | occupancy[BLACK];
}
std::string Board::fen() {
    std::string f="";
    for(int sq=A8;; ) {
        if (mailbox[sq] != NONE)
            f += PIECE_SYMBOL[mailbox[sq]];
        else if (f.size() && std::isdigit(f.back()))
            f[f.size()-1]++;
        else f += '1';
        if (++sq % 8 == 0){
            if (sq == A2) break;
            f += '/'; sq -= 16;
        }
    }
    f += ' '; f += side==WHITE?'w':'b';
    f += ' ';
    if (castling_rights>>H1 & 1) f+='K';
    if (castling_rights>>A1 & 1) f+='Q';
    if (castling_rights>>H8 & 1) f+='k';
    if (castling_rights>>A8 & 1) f+='q';
    f += ' '; 
    f += square_to_algebraic(enPassantSquare);
    f += ' '; f += std::to_string(halfmove_clock);
    f += ' '; f += std::to_string(fullmove_number);
    return f;
}
void Board::parse_fen(std::string fen) { // declared in board.h with default arg. = STANDARD_FEN
    if (fen.empty()) fen = STANDARD_FEN;
    std::istringstream iss(fen);
    reset(); // Clear the board before setting it up
    std::string board_str, active, castling, en_passant, halfmove, fullmove;
    iss >> board_str >> active >> castling >> en_passant; 
    // We can ignore halfmove/fullmove for Perft

    // 2. Parse the piece placement
    int sq = A8; // Start at A8
    for (char c : board_str) {
        if (c == '/') {
            sq -= 16; // Drop down a rank (e.g., moving from end of rank 8 [64] to start of rank 7 [48])
        } else if (isdigit(c)) {
            sq += (c - '0'); // Skip empty squares
        } else {
            // Map character to piece type and set the bit
            Piece piece = piece_from_char(c);
            if (piece != NONE) {
                BITSET(pieces[piece], sq);
                mailbox[sq] = piece;
            }
            sq++;
        }
    }
    update_occupancy(); // After placing all pieces, update occupancy bitboards

    // 3. Parse Active Color
    side = (active == "w") ? WHITE : BLACK;

    // 4. Parse Castling Rights
    for(auto c : castling) {
        switch(c) {
            case 'K': castling_rights |= WHITE_O_O; break;
            case 'Q': castling_rights |= WHITE_O_O_O; break;
            case 'k': castling_rights |= BLACK_O_O; break;
            case 'q': castling_rights |= BLACK_O_O_O; break;
        }
    }

    // 5. Parse En Passant square
    if (en_passant != "-") {
        int file = en_passant[0] - 'a';
        int rank = en_passant[1] - '1';
        enPassantSquare = rank * 8 + file;
    }
    if (iss >> halfmove_clock) {
        iss >> fullmove_number;
    } else {
        halfmove_clock = 0;
    }   fullmove_number = 1;
    hash = generate_hash(); // initialize zobrist hash for the new position
}
#define BRIGHT_WHITE "\033[97m"
#define BRIGHT_BLACK "\033[90m"
#define NORMAL_BLACK "\033[30m"
#define NORMAL_WHITE "\033[37m"
#define TRUE_BLACK "\x1b[38;2;0;0;0m"
#define TRUE_WHITE "\x1b[38;2;255;255;255m"
/*
- \033[48;5;22m → dark green
- \033[48;5;28m → medium dark green
- \033[48;5;34m → classic green
- \033[48;5;40m → lighter green
- \033[48;5;70m → olive green

static const char* LIGHT = "\033[48;5;223m";    // light tan
static const char* DARK  = "\033[48;5;94m";     // dark brown = 94
*/
#define OLIVE_GREEN "\033[48;5;70m"
static const char* LIGHT = "\033[48;5;223m";    // light tan
static const char* DARK  = OLIVE_GREEN;     // dark brown = 94
static const char* WHITE_P = TRUE_WHITE; //"\033[38;5;231m";  // bright white
static const char* BLACK_P = TRUE_BLACK; //"\033[38;5;16m";   // bright black (really dark gray)
static const char* RESET = "\033[0m";
#if 2
static const char* HLINE = "  +----------------+";
static const char* FILES = "   a b c d e f g h \n";
static const char* FILES_FLIPPED = "   h g f e d c b a \n";
#else
static const char* HLINE = "  +------------------------+";
static const char* FILES = "    a  b  c  d  e  f  g  h \n";
static const char* FILES_FLIPPED = "    h  g  f  e  d  c  b  a \n";
#endif
//static const char* UNICODE_PIECES = "♙♘♗♖♕♔♟♞♝♜♛♚ □■";
static const char* UNICODE_PIECES[] = {
    "♙", "♘", "♗", "♖", "♕", "♔",
    "♟", "♞", "♝", "♜", "♛", "♚", " ",
    "□", "■"
};

void Board::print(bool flipped) {
    std::cout << HLINE << "\n";
    for (int rank = 8; rank > 0; ) {
        std::cout << (flipped ? 8 - --rank : rank--) << " |";
        for (int file = 0; file < 8; file++) {
            int sq = rank * 8 + file;
            Piece piece = mailbox[flipped ? 63 - sq : sq];
            //char symbol = // (piece != NONE) ? PIECE_SYMBOL;
            const char* bg = (rank+file)%2 ? LIGHT : DARK;
            const char* fg = (piece < p ? WHITE_P : BLACK_P);
            std::cout << bg << fg //<<' '
                << UNICODE_PIECES[piece] << ' ';
        }
        std::cout <<RESET<< '|' << std::endl;
    }
    std::cout << HLINE << '\n' << (flipped ? FILES_FLIPPED : FILES);
}

uint64_t Board::perft(int depth) {
    MoveList list;
    generate_moves(list);

    uint64_t nodes = 0;
    
    //Color us = side;
    Piece our_king = (side == WHITE) ? K : k;

    for (int i = 0; i < list.count; i++) {
        Move move = list.moves[i];
        // --- STATE SAVING ---
        Board board = *this;

        // We need a backup of engine state (castling rights, EP square, etc.)
        // depending on how your unmake_move is structured.
        
        board.make_move(move);

        // PSEUDO-LEGAL CHECK: Did this move leave our King in check?
        // (Assuming 'side' flipped in make_move, so we check the color that just moved)
        int king_sq = GET_LSB(board.pieces[our_king]);
        
        if (!board.is_square_attacked(king_sq, board.side)) { // 'side' is now the enemy
            if (depth>1) nodes += board.perft(depth - 1);
            else nodes += 1; // Leaf node
        }
        // No need to undo move since we use a copy of the board (board = *this)
    }
    return nodes;
}
//end perft
void Board::perft_divide(int depth) {
    MoveList list;
    generate_moves(list);

    uint64_t total_nodes = 0;
    std::cout << "--- Perft Divide Depth " << depth << " ---" << std::endl;
   
    for (int i = 0; i < list.count; i++) {
        Move move = list.moves[i];
        Board copy = *this; // Make a copy of the board to test the move
        copy.make_move(move);
        std::cout << move_to_uci(move) << ": " ;
        if (copy.king_can_be_captured())
            std::cout << 0 << " (pseudo-legal, leaves king in check)\n";
        else {
            uint64_t nodes = depth > 1 ? copy.perft(depth - 1) : 1;
            std::cout << nodes << std::endl;
            total_nodes += nodes;
        }
    }
    std::cout << "\nTotal Nodes: " << total_nodes << std::endl;
}
// end perft_divide

/*/ NOTE: this isn't inteded for use in the search, but for checking user input
bool Board::is_pseudolegal_move(Move m) {
    MoveList list;
    generate_moves(list);
    for (int i = 0; i < list.count; i++) {
        if (list.moves[i] == m) return true;
    }
    return false;
}
*/
Move Board::identify_pseudolegal_move(Move m) {// the move may be missing some flags (capture, e.p., double push)
    MoveList list;
    generate_moves(list);
    for (int i = 0; i < list.count; i++) {
        if ((list.moves[i] ^ m) >= 1<<12) { // to & from must match, but flags can differ
            // promotion, if any, must also match
            // if list[i] has E.P., double pawn push or Castling flags, we can ignore them.
            int diff = (list.moves[i] - m)>>12 & ~CAPTURE; // shift out the flags, ignore CAPTURE flag
            if (diff >= PROMOTE_QUEEN) // only differ in EN_PASSANT, DOUBLE_PAWN_PUSH or CASTLE flags
            // if the user omitted the promotion piece type, we assume Q promotion
                return list.moves[i];
        }
    }
    return 0;
}
// This checks whether a move left the king hanging, i.e., whether the side to move can capture the enemy king.
// It is not the same as "king is in check" (which refers to the "own" king)

//inline//doesn't work with -O3 : have to move inline code into .h
bool Board::king_can_be_captured() {
    return is_square_attacked(GET_LSB(pieces[side == WHITE ? k : K]), side);
}
//inline//doesn't work with -O3 : have to move inline code into .h
bool Board::is_check() {
    return is_square_attacked(GET_LSB(pieces[side == WHITE ? K : k]), side == WHITE ? BLACK : WHITE);
}
