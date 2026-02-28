/* main.cpp for castlechess - A C++ chess engine
    (c) 2026 by MFH

TODO: 
- add quiscence search (to avoid horizon effect and improve move ordering)
- add iterative deepening (to improve move ordering and time management)
- add time management (e.g., allocate more time for complex positions, less for simple ones)

- in the UI, provide options for choosing display style (ASCII vs Unicode, maybe HTML) and colors.

- refactor to separate 
    a) board state & move creation
    b) seerch & evaluation
    c) UCI style communication / command parsing
    d) UI for interactive use.

History:
2026-02-17..19: quite efficient version (30M nodes/sec) created on GitHub code space.
    Unfortunately, billing issues made the codespace unavailable.
2026-02-20: "Recreated" the code (with many differences: mailbox, ...) on local machine
2026-02-21: implemented PerfT.
2026-02-25:
- added Zobrist hashing and a simple Transposition Table (TT) implementation
2026-02-26:
- added ANSI color codes for better readable output in the terminal
- added UFT8 output for Unicode chess pieces. Unfortunately, the terminal switches fg color to opposite
  depending on BG color (light vs. dark squares), only green dark squares seem to work reasonably.
- added Zobrist best_move to move sorting
- added UCI communication. Tested to work with EnCroissant.

Initial version, with basic UCI command parsing and move making.
 */
#include <iostream>
#include <chrono>
#include <ctime>
#include <iomanip> // for timestamp
#include <cstdlib> // For std::atoi
#include <string>
#include <sstream>
#include <fstream> // for logging uci GUI interactions
#include <vector>
#include "board.h" // Board class and related functions
#include "cc_engine.cpp" // Engine
void show_help() {
    std::cout << R"(Available Commands: (many of these are also command line arguments)

- 'd[isplay]': Display the current board state. Activates interactive mode.
- 'e[val]': Evaluate the current position
- 'fl[ip]'/'r[otate]': Flip the board perspective (toggle between White and Black view)
- 'f' or "fen" or "FEN:": show the FEN of the current position, 
    or reset board from the given FEN string (see also 'k' and 's')
- 'g' or 'go<N>': Let the engine play the best move it can find, 
       using the currently set search depth (default = 6); if given, set the depth to <N>.
- 'h[elp]': Show this help message
- 'i[nteractive]': Toggle interactive mode, which allows you to enter moves in SAN or UCI format (e.g., "Nf3", "dxe5", "e2e4", etc.) and see the board after each move.
- 'k': Load the Kiwipete position, FEN r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1
- 'ml': Show the list of generated moves in the current position (for debugging)
- 'p[erft] [N]': Run perft test to depth N (e.g., 'p 5')
- 're[set]'/'rt'/'tr': Toggle ON/OFF TT reset (clear_tt()) upon restarting a game, i.e., parse_FEN().
- 's': Load the standard starting position, FEN rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1
- 'u': Undo the last move
- 't'/'z': Toggle Zobrist Transposition Table ON/OFF (for testing)
You can also enter moves in UCI format (e.g., 'e2e4') once the engine is ready.
)";
}

//const std::string STANDARD_FEN; // = FEN of standard starting position, defined in board.h
const std::string Kiwipete = "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1";
bool flipped = false,    // print the board from Black's perspective (rotated 180°, ranks & files reversed)
    interactive = false,
    msg_queue_enabled = 1;
int perft_depth = 5;
int search_depth = 8;
Board board; // Global board instance for command processing

std::vector <std::string> msg_queue; // queue of messages to be printed once we're waiting for user input.
std::vector <Board> history; // queue of user inputs to be processed once we're ready.

void ERROR(std::string msg) {
    std::cerr << "ERROR: " << msg << std::endl;
    exit(1);
}
void print_msg_queue() {
    for (const auto& msg : msg_queue)
        std::cout << msg;
    msg_queue.clear();
}
void queue_msg(std::string msg) {
    if (msg_queue_enabled)
        msg_queue.push_back(msg);
    else {
        if (msg_queue.size())print_msg_queue();
        std::cout<<msg;
    }
}
void select_starting_position(const std::string name, const std::string FEN) {
    queue_msg(name + " starting position selected:\n");//. ('d' to display)
    board.parse_fen(FEN); 
    if(interactive) board.print(flipped); // Q: should we reset flipped to false?
}
void show_move_list(Board& board) {
    MoveList list;
    board.generate_moves(list); Engine::score_moves(list, board);
    std::cout << "Generated Moves (" << list.count << "):\n";
    for (int i = 0; i < list.count; i++) {
        Move move = list.moves[i];
        std::cout <<board.san(move)  << " (uci: "<< move_to_uci(move)
            << ", $"<<std::hex<<move<<", s:"<<std::dec<<list.score[i]<<") ";
    }
    std::cout << "\n";
}
void make_move_and_check_termination(Move move) {
    history.push_back(board); // Save current board state before making the move
    board.make_move(move);
    board.print(flipped);
    if (board.game_over()) {
        std::cout << "Game is over.\n";
        std::cout << "Result: " << board.result() << "\n";
    }
}

void make_engine_move(){
    if (board.game_over()) {
        std::cout << "Game is over, no move can be made.\n";
        return;
    }
    std::cout << "Thinking... (depth = " << search_depth << ")\n";
    Engine::max_depth = search_depth;
    //std::chrono::high_resolution_clock::time_point
    Move best_move = Engine::think(board); 
    int duration = Engine::elapsed_ms;

    if (best_move) { // NOTE: must use 'san' *before* making the move !!
        std::cout << "Engine plays the move " << board.numbered_san(best_move)
            <<" (uci: "<<move_to_uci(best_move)<<", eval: "<<Engine::evaluation
            <<", "<<Engine::nodes_evaluated<<" nodes / "<<duration<<"ms = "
            <<(duration ? Engine::nodes_evaluated/duration : '-')<<"/ms).\n"
            <<"Transposition table: used "<<Engine::tt_used<<", stored "<<Engine::tt_stored<<".\n";
        make_move_and_check_termination(best_move);
    } else {
        std::cout << "Engine found no move. The game is over.\n";
    }
}
// helper function to identify a move from user input, which may be in UCI format or SAN format
// (e.g., "Nf3", "dxe5", "e8=Q", "O-O", "O-O-O")    
Move identify_move(std::string cmd) {
    MoveList list;
    board.generate_moves(list);
    // NOTE: move_from_uci() may/will be missing some flags (capture, e.p., double push)
    if (Move m = move_from_uci(cmd))
        for (int i = list.count; i;) {
            int diff = list.moves[--i] ^ m;
            if (!(diff & (1<<12) - 1) && (!(diff &= ~CAPTURE<<12) // remove capture flag
                                          || diff >> 12 >= PROMOTE_QUEEN )) 
                 // only differ in EN_PASSANT, DOUBLE_PAWN_PUSH or CASTLE flags
                // if the user omitted the promotion piece type, we assume Q promotion
                return list.moves[i];
            };
    // try to identify as a SAN move, e.g., "Nf3", "dxe5", "e8=Q", "O-O", "O-O-O"
    // remove any non-alphabetic characters and 'x' for easier matching
    auto simplify = [](std::string s) {
        std::string out; for (char c : s) { 
            c = std::toupper(c);
            if (c >= '0' && c < 'X') out.push_back(c);
        }
        return out;
    };
    std::string simplified_cmd = simplify(cmd);
    for (int i = list.count; i;)
        if (simplify(board.san(list.moves[--i])) == simplified_cmd){
            std::cerr << "(Identified move " << simplified_cmd << ")\n";
            return list.moves[i];
        }
    return 0; // not found
}
void try_to_make_move(std::string cmd){ // Attempt to parse string as a move and execute it if pseudolegal
    if (Move m = identify_move(cmd)) {
        history.push_back(board); // Save current board state before making the move
        // NOTE: this must come *before* making the move, since 'san' needs the *current* board state to identify the move correctly.
        std::cout << "Move "<<board.numbered_san(m)<<" (uci: "<<move_to_uci(m)<<") executed:\n";
        make_move_and_check_termination(m);
        if (board.king_can_be_captured()) {
            std::cout << "WARNING: Illegal move -- King can be captured!\n";
        }
    } else {
        std::cout << "Unknown command or illegal move. (Type 'ml' to show the move list, 'h' for help.)\n";
    }
}

void evaluate_position() {
    int score = Engine::evaluate(board);
    std::cout << "Engine evaluation of the current position (depth 0): " << score << "\n";
}
void undo_move() {
    if (history.size()) { board = history.back(); history.pop_back();
        std::cout<<"OK, take back one move. Position is now:\n"; board.print(flipped);
    } else {
        std::cout<<"No move to take back!\n";
    }
}
void toggle_reset_tt() {
    Engine::reset_tt = !Engine::reset_tt;
    std::cout << "Transposition Table will now "<<(Engine::reset_tt ? "" : "NOT")
    <<" BE CLEARED cleared when the board is initialized." << ".\n";
}
void toggle_tt() {
    Engine::use_tt = !Engine::use_tt;
    std::cout << "Transposition Table is now " << (Engine::use_tt ? "ON" : "OFF") << ".\n";
}
void flip_board() {
    flipped = !flipped;
    std::cout << "Board view flipped to " << (flipped ? "Black" : "White") << "'s perspective:\n";
    board.print(flipped);
}
// parsing of cmd line args -- slightly different from parsing interactive user input
void parse_args(int argc, char* argv[]) {
    for(int i=1; i < argc; ++i) {
        switch(argv[i][0]){
        case '-': argv[i]++; --i; continue;// reread arg starting with char after '-'
        case 'd': /* display */ board.print(flipped); break;
        case 'f': if (++i >= argc) {
                    ERROR("Expected FEN string after 'f' command.");
                }
            select_starting_position(std::string(argv[i]), argv[i]); break;
        case 'h': show_help(); break;
        case 'i': interactive = true; msg_queue_enabled = false; break;
        case 'k': select_starting_position("Kiwipete", Kiwipete); break;
        case 'm': show_move_list(board); break;            
        case 'p': // perft : expect numerical 'depth' argument after 'p'
            if (std::isdigit(argv[i][1])) // number follows immediately
                argv[i]++; // set pointer to next character
            else if (++i >= argc || !std::isdigit(argv[i][0])) 
                ERROR("Expected numerical value for depth after the 'p' command.");
            else if ((perft_depth = std::atoi(argv[i])) <= 0)
                ERROR("Invalid depth provided: must be > 0.");
            board.perft_divide(perft_depth);
            break;
        case 's': select_starting_position("Standard", STANDARD_FEN); break;            
        case 'z': toggle_tt(); break;            
        default: try_to_make_move(argv[i]);
        }
    }
}

std::ostream& timestamp(std::ostream& os) {
    auto now = std::chrono::system_clock::now();
    auto t   = std::chrono::system_clock::to_time_t(now);
    return os << std::put_time(std::localtime(&t), "%Y-%m-%d %H:%M:%S");
}

const char* LOGFILE = "c:\\temp\\castlechess_debug.txt";
std::ofstream open_logfile(std::string message = ""){
    std::ofstream log(LOGFILE, std::ios::app);
    if (message != "") log << timestamp << " : " << message << std::endl;
    return log;
}

void uci_loop(/* Board& board */) {
    /* we are here after receiving the uci command:
     * The GUI said hello. We reply with our info and "uciok":
     */
    // 1. Open a log file in "append" mode so it doesn't overwrite immediately
    auto log = open_logfile("--- ENGINE STARTED ('uci' command received) ---");

    std::cout << "id name CastleChess\n" << "id author MFH\n" << "uciok"<< std::endl;
    log << "ENG -> id name CastleChess | id author MFH | uciok" << std::endl;
    clear_tt(); 
    Engine::reset_tt = false; // Set reset_tt = false so that we don't clear the TT again when we parse the FEN,
    log << "ENG -> cleared transposition table; set reset_tt = false" << std::endl;

    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.empty()) continue;
        log << "GUI -> " << line << std::endl;// LOG THE INCOMING COMMAND

        std::istringstream iss(line);
        std::string command;
        while(iss >> command) {
          if (command == "uci") { // The GUI says hello. We reply with your info and "uciok"
            std::cout << "id name CastleChess\n" << "id author MFH\n" << "uciok" << std::endl;
            log << "ENG -> id name CastleChess | id author MFH | uciok" << std::endl;
        } 
        else if (command == "isready") {// are we done initializing?
            std::cout << "readyok" << std::endl;   // yes!
            log << "ENG -> readyok" << std::endl;
        }
        else if (command == "ucinewgame") { // New game is starting. We can clear the TT!
            clear_tt(); 
        }
        else if (command == "position") {
            // The GUI tells us the current board state.
            // Example 1: position startpos
            // Example 2: position startpos moves e2e4 e7e5
            // Example 3: position fen <fen_string> moves ...
            // ==> we'll parse these commands by looking for "startpos" or "fen" in the main REPL
            // We don't need a separate parser here.
        } 
        else if (command == "startpos") {
            board.parse_fen(); //STANDARD_FEN
        } 
        else if (command == "fen") {
            std::string FEN;
            // The FEN string may contain spaces, so we read the rest of the line as the FEN string
            // unless there's a "moves" keyword.
            int fen_start = line.find("fen ") + 4, 
                moves_pos = line.find(" moves ");
            if (moves_pos != std::string::npos) {
                FEN = line.substr(fen_start, moves_pos - fen_start);
                iss.seekg(moves_pos + 7); // Move the stream position to after "moves "
            } else {
                std::getline(iss, FEN);
            }
            log << "ENG -> setting up FEN: " << FEN << std::endl;
            board.parse_fen(FEN);
        }
        else if (command == "moves") {// we simply ignore this command, and subsequently parse the moves
            // as they appear, as if they were entered by the user in UCI format (e.g., "e2e4", "e8g8", etc.)
        }
        else if (command == "go") {
            // The GUI tells us to start thinking.
            // Example: go depth 8
            // Example: go movetime 5000
            // Example: go wtime 30000 btime 20000 [winc 0 binc 0]
            Engine::max_depth = 8; // default depth limit
            Engine::max_time_ms = 0; // default: no time limit
            while(iss >> command) {// parse the "go" command for options, e.g., "depth 8"
                if (command == "depth"){
                    if (!(iss >> Engine::max_depth)) {
                        std::cerr << "Expected numerical value for depth after 'go depth'.\n";
                    } else {
                        log << "ENG -> setting max depth = " << Engine::max_depth << std::endl;
                    }
                }
                else if (command == "movetime" || command == "wtime" || command == "winc"
                      || command == "btime" || command == "binc") {
                    int time_limit_ms;
                    if (!(iss >> time_limit_ms)) {
                        std::cerr<<"Expected numerical value after 'go "<<command<<"'.\n";
                        continue;
                    }
                    if(command.size()<8 && command[0] != (board.side == WHITE ? 'w' : 'b')) {
                        log<<"ENG -> ignoring command '" << command << "': does not match the side to move.\n";
                        continue;
                    }
                    if (command.size() == 4) { // winc/binc
                        // For simplicity, we add the increment to the time limit for this move.
                        // TODO: we should check whether we have enough total time left!
                        Engine::max_time_ms += time_limit_ms;
                        continue;
                    }
                    if (command.size() == 5) { // wtime or btime 
// The GUI gives us a time for the entire game. Use that to set a reasonable time limit for this move.
// Very simple time management strategy: just divide the remaining time by an arbitrary factor (e.g., 30)
// to get a per-move time limit. We can make it more sophisticated based on position, remaining time, etc.
                        time_limit_ms /= 30;
                        command += "/30";
                    } 
                    Engine::max_time_ms = time_limit_ms;
                    log<<"ENG -> setting time limit "<<command<<" = "<< time_limit_ms <<" ms"<<std::endl;
                } else {
                    std::cerr << "Unknown option in 'go' command: " << command << "\n";
                }
            }// end while (iss>>command) in "go"
            // start thinking
            Move best_move = Engine::think(board);
            // The engine may/will write "info..." lines to stdout.          
            // We MUST reply with this exact format:
            std::cout << "bestmove " << move_to_uci(best_move) << std::endl;
            log << "ENG -> bestmove " << move_to_uci(best_move) << std::endl;
        } 
        else if (command == "quit") { // The GUI is closing
            log << "GUI -> quit" << std::endl;
            return; 
        } else if (Move m = identify_move(command)) {
            // The GUI may also send us moves in UCI format (e.g., "e2e4", "e8g8", etc.)
//          log << "ENG -> input " << command << " identified as move: " << board.san(m) << " (uci: " << move_to_uci(m) << ")\n";
            board.make_move(m);
            //log << "ENG -> FEN is now: " << board.fen() << std::endl;
        }
    }}// while iss, if cmd==...
}
int main(int argc, char* argv[]) {
    //open_logfile("Starting CastleChess.").close();
    msg_queue_enabled = 1; // suppress verbose output until we know we're not connected to a GUI
    if (argc > 1) parse_args(argc, argv);

    // if not interactive, the following messages won't be displayed
    queue_msg("Welcome to Castlechess v.2.0 - (c) 2026 by MFH\nType 'h' for help.\n");
    init_tables(); // Initialize any necessary tables (like attack tables, Zobrist keys, etc.)
    if (interactive){
        queue_msg("Engine Initialized.\n");
        print_msg_queue();
        select_starting_position("Standard", STANDARD_FEN);
    }
    while (true) {
        std::string cmd;
        if (interactive) {
            print_msg_queue();
            std::cout << "Your move or command, 'h' for help: "<<board.move_number_string();
        }
        std::cin >> cmd; // Convert command to lowercase for easier parsing
        // We must check for exact matches or unambiguous prefixes.
        // e.g., 'd2d4' starts with 'd' like "display", "perft" starts with 'p' like "print"
        if (cmd == "d" || cmd.substr(0,2) == "di" || cmd.substr(0,2) == "pr") board.print(flipped); 
        // 'h2h4' starts with 'h' like "help", 
        else if (cmd == "e" || cmd == "eval") evaluate_position();
        // we have 2 commands that start with 'f': "flip" and "fen". 
        // we must take care not to "collide" with moves like "f4" or "fxe5" that start with 'f' as well.
        else if (cmd[0] == 'f' && cmd[1]=='l') flip_board(); // initial segment of "flip"
        // we allow "fen" for "show fen" (if not followed by a FEN string) or "set fen" if an argument is given
        // Anticipating interpretation of "fe" as shortcut for fxe<rank> (f-pawn takes on e-file), 
        // we require "fen" to be followed by a space or end of string.
        else if (cmd == "f" || cmd == "fen" || cmd == "FEN:") { std::string FEN; std::getline(std::cin, FEN);
            if (FEN.size() > 8) select_starting_position("Custom", FEN);
            else std::cout<<"FEN of the current position:\n"<<board.fen()<<std::endl;
        }
        else if (cmd=="g" or cmd.substr(0,2) == "go") {
            if (cmd.length() > 2 && std::isdigit(cmd[2]))
                search_depth = std::atoi(&cmd[2]);
            make_engine_move();
        } 
        else if (cmd == "h" || cmd == "help") show_help();
        else if (cmd == "k" || cmd == "kiwipete") 
            select_starting_position("Kiwipete", Kiwipete);
        else if (cmd[0] == 'm') show_move_list(board);
        else if (cmd[0] == 'p') { // PerfT (must be followed by numerical arg. = depth)
            if (cmd.length() > 1 && std::isdigit(cmd[1]))
                perft_depth = std::atoi(&cmd[1]);
            else if (!(std::cin>>perft_depth)) {
                std::cout << "Expected numerical value for perft depth.\n"; continue;
            }
            if (perft_depth > 0) board.perft_divide(perft_depth);
            else std::cout << "PerfT command must be given a depth value > 0.\n";
        }
        // 'qe2' (for 'Qe2') starts with 'q' like "quit",
        // 'e2e4' and "exd5" start with 'e' resp. "ex" like "exit"
        else if (cmd == "q" || cmd == "quit" || cmd[0]=='x' || cmd == "exit") break;
        else if (cmd == "s" || cmd.substr(0,3) == "sta" || cmd == "std") 
            select_starting_position("Standard", STANDARD_FEN);
        else if (cmd == "uci") uci_loop();
        else if (cmd[0] == 'u') undo_move();
        else if (cmd[0] == 'r' && (cmd[1]==0 || cmd[1]=='o'))// || cmd == "rot" || cmd == "rotate") 
            flip_board();
        else if (cmd == "rt" || cmd == "reset" || cmd == "tr") 
            toggle_reset_tt();// reset/clear the TT upon starting a new game (parse_fen) 
        else if (cmd[0] == 't' || cmd[0] == 'z') toggle_tt();  // use/don't use Zobrist TT
        else try_to_make_move(cmd); // Attempt to parse user input as a move
    }
    queue_msg("Bye!\n");
    print_msg_queue();
    return 0;
}
