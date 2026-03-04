/* castlechess.cpp 
   (c) Feb 2026 by MFH

   Study the Castlechess variant proposed by my late friend Eric Angelini.
   CastleChess is like normal chess, except that Black (also) wins if they castle long
   (we'll write: ...O-O-O#) 
   and White (also) wins as soon as Black loses the right to castle long, 
   e.g., by capturing the rook on a8 or forcing that rook or the king to move.
   Note that the rules aren't symmetric, this does not apply for the White king
   and/or queenside rook, they can move and castle as they want without any consequence.
 */
#include <iostream>
#include <cctype> // Needed for isupper() and tolower()
#include <vector> //  for move stack 'history'
#include <chrono> // for timing the perft
#include <fmt/core.h>
#include "board.h"

void showHelp() {
    std::cout << R"(*** Welcome to CastleChess v. 0.8 - (c) 2026 by MFH ***
    --- AVAILABLE COMMANDS ---
  atk     : Check attacks (?)
  d       : Display current board
  fen ... : Set up the position with the given FEN
  h       : Show this help menu
  ml      : Show move list
  t Xyz   : Test: show bitmap for moves of piece X (lowercase: black) on square yz
  perft n : Do a perft with depth n
  r       : Show Castle Chess rules
  start   : Set up the standard starting position
  q / x   : Exit the program
--------------------------\n)" << std::endl;
}

int debug=1;// see also #define DEBUG in defs.h

// Perft split up in individual moves.
// (User command: perft <depth>)
void perft_divide(Board& board, int depth) {
    MoveList list;
    board.generate_moves(list);
    uint64_t totalNodes = 0;
    Color us = board.sideToMove;

    auto start_time = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < list.count; i++) {
        Move m = list.moves[i];
        
        Board nextBoard = board;
        nextBoard.make_move(m);

        int kingSq = __builtin_ctzll(nextBoard.pieces[(us == WHITE) ? K : k]);
        if (nextBoard.is_square_attacked(kingSq, nextBoard.sideToMove)) continue;

        uint64_t nodes = perft(nextBoard, depth - 1);
        // Print the move and its branch count
        std::cout << move_to_uci(m) << ": " << nodes << std::endl;
        totalNodes += nodes;
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> diff = end_time - start_time;

    std::cout << "\nTotal Nodes: " << totalNodes << std::endl;
    std::cout << "Time taken: " << diff.count() << " seconds" << std::endl;
    std::cout << "NPS (Nodes Per Second): " << (uint64_t)(totalNodes / diff.count()) << std::endl;
}

void show_move_list(Board& board)
        { // show move list
            MoveList list;
            board.generate_moves(list);
            
            std::cout << "Legal Moves (" << list.count << "):" << std::endl;
            for (int i = 0; i < list.count; i++) {
                std::cout << move_to_uci(list.moves[i]) << " ($"<<std::hex<<list.moves[i]<<") ";
            }
            std::cout << std::endl;
        }

void test_movegen(Board& board) { // testing "move generator"
    std::string arg; 
    std::cin >> arg; // Grab the "Nc4" part
    if (arg.length() < 3) {
        std::cout << "Usage: t [Piece][Square] (e.g., t Nc4 or t re8)" << std::endl;
    } else {
        char pChar = arg[0];
        int file = arg[1] - 'a';
        int rank = arg[2] - '1';
        // Check if the square is valid (a-h, 1-8)
        if (file < 0 || file > 7 || rank < 0 || rank > 7) { 
            std::cout << "Invalid square coordinates. Use a-h and 1-8." << std::endl;
        } else {
            int sq = rank * 8 + file;
            Color side = std::isupper(pChar) ? WHITE : BLACK;
            Bitboard moves = 0;
            // Generate moves based on the piece letter
            switch (std::tolower(pChar)) {
                case 'n': moves = board.get_knight_moves(sq, side); break;
                case 'r': moves = board.get_rook_moves(sq, side); break;
                case 'b': moves = board.get_bishop_moves(sq, side); break;
                case 'q': moves = board.get_queen_moves(sq, side); break;
                case 'k': moves = board.get_king_moves(sq, side); break;
                case 'p': moves = board.get_pawn_moves(sq, side); break;
                default: std::cout << "Piece '" << pChar << "' not supported yet." << std::endl; break;
            }
            if (moves) {
                std::cout << "\nValid moves for " << pChar << " on " << arg[1] << arg[2] << ":\n";
                Board::printBitboard(moves);
            }
        }
    }
}

std::vector<Board> history; // <-- Our move stack
        // Helper: Try to parse a string as a move. 
// If valid, apply it and return true. If not, return false.
Move try_parse_and_make_move(Board& board, std::string input) { //, std::vector<Board>& history//GLOBAL
    Move move = board.move_from_uci(input);
    if (!move) return false;
    DEBUG("Move '"<<input<<"' is 0x"<< std::hex<<move <<" in hex."<<std::endl)

    //  Generate all legal moves for the current position
    MoveList list;
    board.generate_moves(list);

    // Loop through legal moves to see if input matches
    for (int i = list.count; i-->0; ){ 
        Move m = list.moves[i];
        if ( m == move ) { // remove the CAPTURE flag (MSB in flag)
            // (TODO: handle promotion notation like "a7a8q" later)
            // Found a match! Save history and make move.
            history.push_back(board);
            std::cout << "OK, making move "<< input << " (" << board.move_number_string() << board.san(m) << ")" << std::endl;
            board.make_move(move);
            board.print();
            return  m;
        }
    }
    return 0;
}

void check_attacks(Board& board){
            std::string sqStr;
            std::cin >> sqStr;
            int sq = algebraic_to_square(sqStr);
            
            // Check if attacked by WHITE
            std::cout << "Square " << sqStr << " attacked by White? " 
                      << (board.is_square_attacked(sq, WHITE) ? "YES" : "NO") << std::endl;
            
            // Check if attacked by BLACK
            std::cout << "Square " << sqStr << " attacked by Black? " 
                      << (board.is_square_attacked(sq, BLACK) ? "YES" : "NO") << std::endl;
        }

int main() {
    Board board;
    std::string input;
    
    std::cout << "CastleChess v0.3 (FEN Support Enabled)" << std::endl;

    // initialize some tables for move gen
    Board::init_knight_attacks();
    Board::init_king_attacks();
    char gobble_variant_or_annotation=0;
    while (true) {
        if (!gobble_variant_or_annotation ) 
            std::cout<<"[give move or command, 'h' for help] "<< board.move_number_string();
        if (!(std::cin >> input)) break;
        else if (std::isdigit(input[0]) || input[0]=='.') continue; // gobble 5. and 5... and ...
        else if ( input[0]=='(' || input[0]=='{' ) { 
            gobble_variant_or_annotation = input[0]=='(' ? ')' : '}';
            if (input.back() == gobble_variant_or_annotation) {
                gobble_variant_or_annotation = 0;
                DEBUG("Ignored input '"<<input<<"'\n");
            } else
                DEBUG("[ignoring input from '"<<input<<"' up to next '"
                      <<gobble_variant_or_annotation<<"'] ")
        }
        else if (gobble_variant_or_annotation) {//up to next ')' or '}'
            DEBUG(input<<" ")
            if (input.back() == gobble_variant_or_annotation) {
                gobble_variant_or_annotation = 0; DEBUG(std::endl);
            }
            continue;
        }
        else if (input == "atk") check_attacks(board);// will wait for <square> arg
        else if (input == "d") board.print();
        else if (input == "fen") {
            std::string fen_string;
            std::getline(std::cin >> std::ws, fen_string); // Get rest of line
            board.parseFEN(fen_string);
        }
        else if (input == "h") showHelp();
        else if (input == "m") {// make move -- 'm' can be omitted, kept "just in case"
            std::cin >> input;
            if (!try_parse_and_make_move(board, input)) {
                std::cout << "Illegal or unknown move: " << input << std::endl;
            }
        }
        else if (input == "ml") show_move_list(board);
        else if (input == "perft") { int depth=0; std::cin>>depth;
            std::cout<<"Performing perft with depth = "<<depth<<"..."; 
            perft_divide(board, depth);
        }
        else if (input == "t") test_movegen(board);
        else if (input == "u") { // undo move
            if (!history.empty()) {
                board = history.back(); // Restore the last board state
                history.pop_back();     // Remove it from history
                std::cout << "Move undone." << std::endl;
                board.print();
            } else {
                std::cout << "No moves to undo!" << std::endl;
            }
        }
        else if (input == "start") board.parseFEN(START_FEN);
        else if (input == "x" || input == "q") break; // Exit CLI
        else if (!try_parse_and_make_move(board, input)) {
            std::cout << "Unknown command or illegal move: '" << input << "'." << std::endl;
        }
    }
    std::cout<<"Bye!\n";
    return 0;
}
