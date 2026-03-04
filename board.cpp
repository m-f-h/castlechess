#include "board.h"
#include <iostream>
#include <cctype>

// Helper to print squares like "e2"
std::string square_to_algebraic(int sq) {
    std::string s = "";
    s += (char)('a' + (sq & 7));
    s += (char)('1' + (sq >> 3));
    return s;
}
// Helper to print moves like "e2e4"
std::string move_to_uci(Move m) {
    return square_to_algebraic(get_from(m)) + square_to_algebraic(get_to(m));//TODO: append promotion
}

// symbol to pieceType conversion:
int get_piece_type(char symbol) {
            int pieceType = NONE;
            for (int i = 0; i < 12; i++) {
                if (PIECE_CHARS[i] == symbol) return i;
            }
            return NONE;
        }

Move Board::move_from_uci(std::string uci) {
    if (uci.length() < 4) return 0;
    int from = algebraic_to_square(&uci[0]),
        to =  algebraic_to_square(&uci[2]),// FIXME : convert prom char to code
        flags = uci.length() > 4 ? get_piece_type(std::toupper(uci[4])) // PROMOTION_xxx
                : 0; // TODO : fixme. (ignore "=", "+"", etc...)
    if (flags==NONE) {
        std::cerr<<"Ignoring invalid promotion spec '"<<uci.substr(4)<<"' in move '"<<uci<<"'."<<std::endl;
        flags = 0;
    }
    switch(get_piece_at(from)%6){
        case P: switch (abs(from-to)) {
            // direct return(...) if flag mutually exclusive with all others
            case 16: return encode_move(from, to, DOUBLE_PAWN_PUSH);
            case 8: // normal pawn move or promotion (no check if given as 5th letter,
                    // otherwise PROMOTE_QUEEN if arrival on 8th or 1st)
                return encode_move(from, to, flags ? flags : // no check 
                    BITTEST(1<<0 | 1<<7, to >> 3) ? PROMOTE_QUEEN : QUIET_MOVE); 
            default: // capture, possibly e.p. XOR with promotion
                return encode_move(from, to, // compute piece_at only if on 3rd or 6th rank
                    BITTEST(1<<2 | 1<<5, to >> 3) && get_piece_at(to) == NONE 
                    ? EP_CAPTURE : flags | CAPTURE);
            }
        case K: if (abs(from-to)==2) // castling
                    return encode_move(from, to, CASTLING); // MUTEX w/ other flags
    }
    return encode_move(from, to, get_piece_at(to) == NONE ? flags: flags | CAPTURE);
}

void Board::printBitboard(Bitboard bb) {
    std::cout << "  +-----------------+\n";
    for (int rank = 8; rank; ) {
        std::cout << rank-- << " | ";
        for (int file = 0; file < 8; file++) {
            std::cout << (BITTEST(bb, rank * 8 + file) ? "1 " : ". ");
        }
        std::cout << "|\n";
    }
    std::cout << "  +-----------------+\n";
    std::cout << "    a b c d e f g h" << std::endl;
}

void Board::print() {
    std::cout << "  +-----------------+" << std::endl;
    for (int rank = 8; rank ;) {
        std::cout << rank-- << " | ";
        for (int file = 0; file < 8; file++) {
            std::cout << getPieceChar(rank * 8 + file) << " ";
        }
        std::cout << "|" << std::endl;
    }
    std::cout << "  +-----------------+\n";
    std::cout << "    a b c d e f g h\n";
    std::cout << "Move number " << move_number_string() << (
        sideToMove == WHITE ? "(White" : "(Black" ) << " to move)" << std::endl;
}
std::string Board::move_number_string() {
    return std::to_string(fullMoveNumber) + (sideToMove==WHITE ? ". " : "... ");
}

Board::Board() { reset(); }

void Board::reset() {
    for (int i = 0; i < 12; i++) pieces[i] = 0;
    for (int i = 0; i < 3; i++) occupancy[i] = 0;
    sideToMove = WHITE; 
    halfMoveClock = 0; // 50-move rule counter
    fullMoveNumber = 0; // Turn number
    enPassantSquare = NO_EP;
}

void Board::parseFEN(const std::string& fen) {
    reset(); // Clear board and permissions first
    
    std::stringstream ss(fen);
    std::string placement, side, castling, epStr;
    
    // 1. Extract the chunks automatically
    ss >> placement >> side >> castling >> epStr;
    
    // 2. Parse Placement
    int sq = A8;
    for (char c : placement) {
        if (std::isdigit(c)) {
            sq += (c - '0');
        } else if (c == '/') { // DEBUG: this shoudn't occur when sq%8==0
            sq -= 16;
        } else {
            int pieceType = get_piece_type(c);
            if (pieceType != NONE) pieces[pieceType] |= (1ULL << sq);
            else std::cout << "Got unrecognized piece '"<<c<<"' in parseFEN!" << std::endl;// DEBUG : NONE should not occur !!
            sq++;
        }
    }// DEBUG : sq should now be 8
    
    // 3. Parse Side to Move
    sideToMove = (side == "w") ? WHITE : BLACK;//DEBUG: if not "w", side should be "b"
    
    // 4. Parse Castling Rights
    castlePermission = 0;
    if (castling != "-") {
        for (char c : castling) {
            switch (c) {
                case 'K': castlePermission |= WK_CASTLE; break;
                case 'Q': castlePermission |= WQ_CASTLE; break;
                case 'k': castlePermission |= BK_CASTLE; break;
                case 'q': castlePermission |= BQ_CASTLE; break;
            }
        }
    }
    // 5. epStr to set enPassantSquare
    enPassantSquare = epStr == "-" ? NO_EP : algebraic_to_square(epStr);
    
    // 6. Halfmove Clock & Fullmove Number
    if (ss >> halfMoveClock) {
        ss >> fullMoveNumber;
    } else {
        halfMoveClock = 0;
        fullMoveNumber = 1;
    }

    updateOccupancy();
}

void Board::updateOccupancy() {
    occupancy[WHITE] = pieces[P]|pieces[N]|pieces[B]|pieces[R]|pieces[Q]|pieces[K];
    occupancy[BLACK] = pieces[p]|pieces[n]|pieces[b]|pieces[r]|pieces[q]|pieces[k];
    occupancy[BOTH] = occupancy[WHITE] | occupancy[BLACK];
}

int Board::get_piece_at(int sq) {
    Bitboard mask = 1ULL << sq; // Calculate once!
    for (int i = 0; i < 12; i++) {
        if (pieces[i] & mask) return i;
    }
    return NONE; // 12
}

char Board::getPieceChar(int sq) {
    return PIECE_CHARS[get_piece_at(sq)]; // we added '.' for (p == NONE==12) in PIECE_CHARS
}

std::string Board::san(Move m) { // TODO : add + if check
    int to = get_to(m), from = get_from(m), piece = get_piece_at(from) % 6, target = get_piece_at(to);
    if (piece == K && abs(from-to)==2)
        return (to & 7) == C1 ? "O-O-O" : "O-O"; 
    std::string uci = move_to_uci(m);
    if (target != NONE || get_flags(m)==EP_CAPTURE) {
        uci[1] = 'x'; // keep initial file & dest. square
        if (piece == P) return uci;
    }
    else if (piece == P) return uci.substr(2); // non capture pawn move
    //else uci.erase(1); // erase 2nd char if not capture                  // TODO: disambiguation
    else uci = uci.substr(1); // discard 1st char if not capture                  // TODO: disambiguation
    uci[0] = (char)PIECE_CHARS[piece];
    return uci; 
}

bool Board::is_square_attacked(int sq, Color side) {
    // 1. Check Pawn Attacks
    // If we want to know if 'side' (the enemy) is attacking us with pawns:
    int file = sq%8;
    if (side == WHITE) {
        // Check if a White pawn is on sq-9 (SW) or sq-7 (SE) relative to us
        if (file > 0 && sq > 9 && BITTEST(pieces[P], sq - 9)) return true;
        if (file < 7 && sq > 7 && BITTEST(pieces[P], sq - 7)) return true;
    } else {
        // Check if a Black pawn is on sq+9 (NE) or sq+7 (NW)
        if (file < 7 && sq < 54 && BITTEST(pieces[p], sq + 9)) return true;
        if (file > 0 && sq < 56 && BITTEST(pieces[p], sq + 7)) return true;
    }

    // 2. Check Knight Attacks
    // We use our existing knight table. If we place a knight on 'sq', does it hit an enemy knight?
    if (knight_attacks[sq] & pieces[(side == WHITE) ? N : n]) return true;

    // 3. Check King Attacks
    if (king_attacks[sq] & pieces[(side == WHITE) ? K : k]) return true;

    // 4. Check Bishop/Queen Attacks (Diagonals)
    Bitboard bishopQ = pieces[(side == WHITE) ? B : b] | pieces[(side == WHITE) ? Q : q];
    if (get_bishop_attacks(sq, occupancy[BOTH]) & bishopQ) return true;

    // 5. Check Rook/Queen Attacks (Straight lines)
    Bitboard rookQ = pieces[(side == WHITE) ? R : r] | pieces[(side == WHITE) ? Q : q];
    if (get_rook_attacks(sq, occupancy[BOTH]) & rookQ) return true;

    return false;
}

///////////// KING ATTACKS (precalculated)

Bitboard Board::king_attacks[64];

void Board::init_king_attacks() {
    for (int sq = 0; sq < 64; sq++) {
        Bitboard attacks = 0;
        int rank = sq / 8;
        int file = sq % 8;

        // Up, Down, Left, Right
        if (rank < 7) { attacks |= 1ULL << (sq + 8); // North
          if (file < 7) attacks |= 1ULL << (sq + 9); // NE
          if (file > 0) attacks |= 1ULL << (sq + 7); // NW
        }
        if (rank > 0) { attacks |= 1ULL << (sq - 8); // South
          if (file < 7) attacks |= 1ULL << (sq - 7); // SE
          if (file > 0) attacks |= 1ULL << (sq - 9); // SW
        }
        if (file < 7) attacks |= 1ULL << (sq + 1); // East
        if (file > 0) attacks |= 1ULL << (sq - 1); // West

        king_attacks[sq] = attacks;
    }
}

Bitboard Board::get_king_moves(int sq, Color side) {
    // Moves = King attacks AND NOT our own pieces
    return king_attacks[sq] & ~occupancy[side];
}

///////////// KNIGHT ATTACKS (precalculated)

Bitboard Board::knight_attacks[64];

void Board::init_knight_attacks() {
    for (int sq = 0; sq < 64; sq++) {
        Bitboard b = (1ULL << sq);
        Bitboard attacks = 0;
        int file = sq % 8;
        // The 8 possible Knight moves
        if (sq + 17 < 64 && file < 7) attacks |= 1ULL << (sq + 17);
        if (sq + 15 < 64 && file > 0) attacks |= 1ULL << (sq + 15);
        if (sq + 10 < 64 && file < 6) attacks |= 1ULL << (sq + 10);
        if (sq +  6 < 64 && file > 1) attacks |= 1ULL << (sq + 6);
        if (sq - 17 >= 0 && file > 0) attacks |= 1ULL << (sq - 17);
        if (sq - 15 >= 0 && file < 7) attacks |= 1ULL << (sq - 15);
        if (sq - 10 >= 0 && file > 1) attacks |= 1ULL << (sq - 10);
        if (sq -  6 >= 0 && file < 6) attacks |= 1ULL << (sq - 6);

        knight_attacks[sq] = attacks;
    }
}

Bitboard Board::get_knight_moves(int sq, Color side) {
    // Moves = Precalculated attacks AND NOT our own pieces
    return knight_attacks[sq] & ~occupancy[side];
}
/////////////// PAWN MOVES
Bitboard Board::get_pawn_moves(int sq, Color side) {//TODO : implement e.p.
    Bitboard moves = 0;
    int rank = sq / 8;
    int file = sq % 8;

    if (side == WHITE) {
        // 1. Single Push (Up) 
        // note that rank < 7 anyways -- no pawn on any backrank !
        if (!BITTEST(occupancy[BOTH], sq + 8)) {
            moves |= 1ULL << (sq + 8);
            
            // 2. Double Push (Only if single push is valid AND on Rank 2 (index 1))
            if (rank == 1 && !BITTEST(occupancy[BOTH], sq + 16)) {
                moves |= 1ULL << (sq + 16);
            }
        }
        // 3. Captures (Must have a BLACK piece to capture)
        if (file > 0 && (BITTEST(occupancy[BLACK], sq + 7) || enPassantSquare==sq+7)) { // NW
            moves |= 1ULL << (sq + 7);
        }// ditched rank < 7 here
        if (file < 7 && (BITTEST(occupancy[BLACK], sq + 9) || enPassantSquare==sq+9)) { // NE
            moves |= 1ULL << (sq + 9);
        }
    } 
    else { // BLACK
        // 1. Single Push (Down) // rank > 0 is always true !
        if (!((occupancy[BOTH] >> (sq - 8)) & 1ULL)) {
            moves |= 1ULL << (sq - 8);
            
            // 2. Double Push (Only if single push is valid AND on Rank 7 (index 6))
            if (rank == 6 && !((occupancy[BOTH] >> (sq - 16)) & 1ULL)) {
                moves |= 1ULL << (sq - 16);
            }
        }
        // 3. Captures (Must have a WHITE piece to capture)
        if (file > 0 && (BITTEST(occupancy[WHITE], sq - 9) || enPassantSquare==sq-9)) { // SW
            moves |= 1ULL << (sq - 9);
        }// ditched rank > 0 
        if (file < 7 && (BITTEST(occupancy[WHITE], sq - 7) || enPassantSquare==sq-7)) { // SE
            moves |= 1ULL << (sq - 7);
        }
    }
    
    return moves;
}

////////// ROOK MOVES - through ray casting 

Bitboard Board::get_rook_attacks(int sq, Bitboard blockers) {
    Bitboard attacks = 0;
    int rank = sq / 8;
    int file = sq % 8;
    int r, f;

    // North (Up)
    for (r = rank + 1; r <= 7; r++) {
        attacks |= (1ULL << (r * 8 + file));
        if (blockers & (1ULL << (r * 8 + file))) break;
    }
    // South (Down)
    for (r = rank - 1; r >= 0; r--) {
        attacks |= (1ULL << (r * 8 + file));
        if (blockers & (1ULL << (r * 8 + file))) break;
    }
    // East (Right)
    for (f = file + 1; f <= 7; f++) {
        attacks |= (1ULL << (rank * 8 + f));
        if (blockers & (1ULL << (rank * 8 + f))) break;
    }
    // West (Left)
    for (f = file - 1; f >= 0; f--) {
        attacks |= (1ULL << (rank * 8 + f));
        if (blockers & (1ULL << (rank * 8 + f))) break;
    }

    return attacks;
}

Bitboard Board::get_rook_moves(int sq, Color side) {
    // 1. Get all attacked squares, stopping at ANY piece (occupancy[BOTH])
    Bitboard attacks = get_rook_attacks(sq, occupancy[BOTH]);
    
    // 2. Remove squares occupied by our OWN pieces
    return attacks & ~occupancy[side];
}

/////// BISHOP MOVES

Bitboard Board::get_bishop_attacks(int sq, Bitboard blockers) {
    Bitboard attacks = 0;
    int rank = sq / 8;
    int file = sq % 8;
    int r, f;

    // North-East
    for (r = rank + 1, f = file + 1; r <= 7 && f <= 7; r++, f++) {
        attacks |= (1ULL << (r * 8 + f));
        if (blockers & (1ULL << (r * 8 + f))) break;
    }
    // North-West
    for (r = rank + 1, f = file - 1; r <= 7 && f >= 0; r++, f--) {
        attacks |= (1ULL << (r * 8 + f));
        if (blockers & (1ULL << (r * 8 + f))) break;
    }
    // South-East
    for (r = rank - 1, f = file + 1; r >= 0 && f <= 7; r--, f++) {
        attacks |= (1ULL << (r * 8 + f));
        if (blockers & (1ULL << (r * 8 + f))) break;
    }
    // South-West
    for (r = rank - 1, f = file - 1; r >= 0 && f >= 0; r--, f--) {
        attacks |= (1ULL << (r * 8 + f));
        if (blockers & (1ULL << (r * 8 + f))) break;
    }

    return attacks;
}

Bitboard Board::get_bishop_moves(int sq, Color side) {
    Bitboard attacks = get_bishop_attacks(sq, occupancy[BOTH]);
    return attacks & ~occupancy[side];
}

Bitboard Board::get_queen_moves(int sq, Color side) {
    // A Queen is just a Rook + Bishop
    Bitboard attacks = get_rook_attacks(sq, occupancy[BOTH]) | get_bishop_attacks(sq, occupancy[BOTH]);
    return attacks & ~occupancy[side];
}
// end ray casting 


// *********** generate_moves() ************

void Board::generate_moves(MoveList& list) {
    list.count = 0; // Reset the list
    
    // If White, we check pieces[0] to pieces[5]. If Black, pieces[6] to pieces[11].
    int offset = (sideToMove == WHITE) ? 0 : 6;

    for (int pType = 0; pType < 6; pType++) {
        Bitboard pieceBB = pieces[pType + offset];

        // Loop through every piece of this type on the board
        while (pieceBB) {
            int from = popLSB(pieceBB); // Get the square and erase it from the loop
            Bitboard attacks = 0;

            // Generate moves based on the piece type
            switch (pType) {
                case P: attacks = get_pawn_moves(from, sideToMove); break;
                case N: attacks = get_knight_moves(from, sideToMove); break;
                case B: attacks = get_bishop_moves(from, sideToMove); break;
                case R: attacks = get_rook_moves(from, sideToMove); break;
                case Q: attacks = get_queen_moves(from, sideToMove); break;
                case K: attacks = get_king_moves(from, sideToMove); 
                // CASTLING LOGIC
                    if (sideToMove == WHITE) {
                        // White King-side (e1 -> g1)
                        if (castlePermission & WK_CASTLE) {
                            // Check empty squares f1, g1 AND check if e1, f1, g1 are safe
                            if (!(occupancy[BOTH] >> F1 & 3)) {
                                if (!is_square_attacked(E1, BLACK) && 
                                    !is_square_attacked(F1, BLACK) && 
                                    !is_square_attacked(G1, BLACK)) {
                                    list.add(encode_move(E1, G1, CASTLING));
                                }
                            }
                        }
                        // White Queen-side (e1 -> c1)
                        if (castlePermission & WQ_CASTLE) {
                            if (!(occupancy[BOTH] >> B1 & 7)) {
                                if (!is_square_attacked(E1, BLACK) && 
                                    !is_square_attacked(D1, BLACK) && 
                                    !is_square_attacked(C1, BLACK)) {
                                    list.add(encode_move(E1, C1, CASTLING));
                                }
                            }
                        }
                    } else {// Black King-side (e8 -> g8)
                        if (castlePermission & BK_CASTLE) {
                            if (!(occupancy[BOTH] >> F8 & 3)) { //  3  << F8 <=> F8 or G8
                                if (!is_square_attacked(E8, WHITE) && 
                                    !is_square_attacked(F8, WHITE) && 
                                    !is_square_attacked(G8, WHITE)) {
                                    list.add(encode_move(E8, G8, CASTLING));
                                }
                            }
                        }
                        if (castlePermission & BQ_CASTLE) {// Black Queen-side (e8 -> c8)
                            if (!(occupancy[BOTH] >> B8 & 7)) {
                                if (!is_square_attacked(E8, WHITE) && 
                                    !is_square_attacked(D8, WHITE) && 
                                    !is_square_attacked(C8, WHITE)) {
                                    list.add(encode_move(E8, C8, CASTLING));
                                }
                            }
                        }
                    }
                    break;// case KING
            }

            // Loop through all the generated destination squares
            while (attacks) {
                int to = popLSB(attacks);
                int flags = get_piece_at(to) != NONE ? CAPTURE: 
                            pType != P ? QUIET_MOVE: // no special move other than castling, treated above
                            abs(to-from)==16 ? DOUBLE_PAWN_PUSH:
                            abs(to-from)!= 8 ? EP_CAPTURE : QUIET_MOVE;
                if (pType==P && (1 + 128) & 1 << (to >> 3)) {// pawn arrives on 1st or 8th rank
                    for (int prom = Q; prom; prom--)
                        list.add(encode_move(from, to, flags | prom )); // N=1 .. Q=4: promotions
                }
                else list.add(encode_move(from, to, flags )); 
            }
        }//while pieceBB
    }//for pType
}
// end movegen

bool Board::is_legal(Move m) {
    Board copy = *this;     // save a copy of the board
    Color us = sideToMove;  // remember who WE are    
    make_move(m);           // Make the move on our actual board
    
    // 2. Find where our King is NOW.
    // Use the built-in compiler intrinsic to find the King instantly
    int kingSq = __builtin_ctzll(pieces[(us == WHITE) ? K : k]);
    
    // 3. Is that square attacked by the enemy?
    bool inCheck = is_square_attacked(kingSq, sideToMove);
    
    *this = copy;    // 4. UNMAKE the move to restore the board state!
    return !inCheck;
}

void Board::make_move(Move m) {
    int from = get_from(m);
    int to = get_to(m);
    int flags = get_flags(m);

    int piece = get_piece_at(from);
    int captured = get_piece_at(to);
    
    // 1. Remove the piece from the starting square
    pieces[piece] &= ~(1ULL << from);
    
    // 2. Place the piece on the target square
    pieces[piece] |= 1ULL << to;
    
    enPassantSquare = NO_EP;
    
    // 3. If there was a piece on the target square, remove it (Capture!)
    if (captured != NONE) {
        pieces[captured] &= ~(1ULL << to);
        halfMoveClock = 0;
    }
    else if (piece % PIECE_MOD == P) {
        halfMoveClock = 0;
        if (//flags == DOUBLE_PAWN_PUSH // keep |from-to|==16 for robustness
            abs(from-to)==16) enPassantSquare = (to+from)/2;
        else if (flags==EP_CAPTURE) { // remove the captured piece on RANK(from), FILE(to) 
            pieces[p - piece] &= ~(1ULL << ((from & 7*8) + (to & 7)));
        }
    }
    else halfMoveClock++;
    
    // CASTLING: Move the Rook
    if (flags == CASTLING) {
        pieces[sideToMove == WHITE ? R : r // rook type
                ] ^= (1ULL << ((to & 7) == C1 ? to - 2 : to + 1)// rook_from
                        ) + (1ULL << ((to+from)/2)); // rook_to
    }
    // UPDATE CASTLING RIGHTS 
    // If King moves, lose both rights
    // note that e.g. the WHITE K could capture a BLACK r on A8
    // but then they won't be able to castle anyways...(? FIX ?)
    if (piece == K) castlePermission &= ~(WK_CASTLE | WQ_CASTLE);
    else if (piece == k) castlePermission &= ~(BK_CASTLE | BQ_CASTLE);
    else if BITTEST(castlePermission, to) castlePermission ^= 1ULL << to;
    if BITTEST(castlePermission, from) castlePermission ^= 1ULL << from;
    
    // 4. Swap the turn
    sideToMove = (sideToMove == WHITE) ? BLACK : WHITE;
    if (sideToMove == WHITE) fullMoveNumber++;

    // 5. Update the occupancy bitboards so the engine sees the new layout
    updateOccupancy();
}
// end makemove

// Performance Test : Returns the number of leaf nodes at a given depth
uint64_t perft(Board board, int depth) { // Pass by value to auto-copy state
    MoveList list;
    board.generate_moves(list);

    uint64_t nodes = 0;
    Color us = board.sideToMove; // Save who is moving

    for (int i = 0; i < list.count; i++) {
        Move m = list.moves[i];

        // 1. Make move on a copy
        Board nextBoard = board;
        nextBoard.make_move(m);

        // 2. Check legality (Is our King attacked by the enemy?)
        int kingSq = __builtin_ctzll(nextBoard.pieces[(us == WHITE) ? K : k]);
        if (nextBoard.is_square_attacked(kingSq, nextBoard.sideToMove)) {
            continue; // Illegal move: skip counting this branch
        }

        // 3. Recurse if depth>1
        nodes += (depth > 1) ? perft(nextBoard, depth - 1) : 1;
    }

    return nodes;
}
//end perft
