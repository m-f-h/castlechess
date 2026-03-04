#ifndef BOARD_H
#define BOARD_H

#include "defs.h"
#include <string>
#include <sstream>

std::string square_to_algebraic(int sq);// used in move_to_uci and in main; defined in board.cpp
std::string move_to_uci(Move m);// used in Board::san and in main ; defined in board.cpp 

struct MoveList {
    Move moves[256]; // 256 is the absolute max moves possible in a chess position
    int count = 0;

    void add(Move move) {
        moves[count] = move;
        count++;
    }
};

class Board {
public:
    Bitboard pieces[12];
    Bitboard occupancy[3];
    Color sideToMove;
    uint8_t castlePermission; // cf. WK,WQ,BK,BQ_CASTLE in defs.h
    int halfMoveClock; // 50-move rule counter
    int fullMoveNumber; // Turn number
    int enPassantSquare; // if previous move was DOUBLE_PAWN_PUSH

    static Bitboard knight_attacks[64];
    static void init_knight_attacks();
    Bitboard get_knight_moves(int sq, Color side);
    static void printBitboard(Bitboard bb); 
    Bitboard get_rook_attacks(int sq, Bitboard blockers);
    Bitboard get_rook_moves(int sq, Color side);
    Bitboard get_bishop_attacks(int sq, Bitboard blockers);
    Bitboard get_bishop_moves(int sq, Color side);
    Bitboard get_queen_moves(int sq, Color side);
    static Bitboard king_attacks[64];
    static void init_king_attacks();
    Bitboard get_king_moves(int sq, Color side);
    Bitboard get_pawn_moves(int sq, Color side);
    void generate_moves(MoveList& list);
    int get_piece_at(int sq);
    void make_move(Move m);
    bool is_square_attacked(int sq, Color side); // Is 'sq' attacked by 'side'?
    std::string san(Move m);
    std::string move_number_string(); // s.th. like "1. " or "2... "
    Board();
    void reset();
    void parseFEN(const std::string& fen);
    void print();
    char getPieceChar(int sq);
    Move move_from_uci(const std::string uci);
    bool is_legal(Move m);

private:
    void updateOccupancy();
};

uint64_t perft(Board board, int depth);// defined in board.cpp, used in main

#endif // board.h
