/* main.cpp for castlechess - A C++ chess engine
    (c) 2026 by MFH

    Initial version, with basic UCI command parsing and move making.

    Currently, the program starts in non-interactive mode (use cmd line option 'i' to change this)
    and remains silent waiting for the GUI's "uci" command.
    It understands most basic UCI commands, works well e.g. with EnCroissant.
    If you switch to interactive mode, it becomes more verbose and displays positions
    with colors and Unicode characters for chess pieces.

    Currently the code is a bit messy with parts duplicated in the interactive vs the UCI REPL
    (and the CLI argument parsing...). The two loops mainly differ in parsing the "go" command:
    - in UCI mode, it is followed by "depth" and/or "movetime" arguments, starts
      the thinking process after reading these, writes out "info" lines and finally "bestmove xxx".
    - in interactive mode, depth/movetime will be set before (if needed) and the "go" command
      will *make* the best move found on the board.
    It should be possible to "unify" the commands...: 
    * If in interactive move, *make* the move once the thinking process returned one, 
      instead of printing "bestmove xxx".
    * If in UCI mode, start the thinking process when arriving at the end of the current command line
      (before reading a new line from the terminal), if the current line started with "go".
    But I'm not sure whether it would make the code more compact to unify the two REPLs,
    since although the commands are largely the same, the action (parsing & setting variables)
    as well as the output after the action are very different.
    
TODO: 
- add quiscence search (to avoid horizon effect and improve move ordering)
- add time management (e.g., allocate more time for complex positions, less for simple ones)

- in the UI, provide options for choosing display style (ASCII vs Unicode, maybe HTML) and colors.

- refactor to separate 
    a) board state & move creation
    b) seerch & evaluation
    c) UCI style communication / command parsing
    d) UI for interactive use: board.print() doesn't really belong to the engine part. (Is it a problem?)


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

2026-02-27: added iterative deepening (to improve move ordering and time management) DONE
2026-03-03: added Unicode/UTF8 fix for PowerShell in board.cpp:Board.print()
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

std::string VERSION_STRING = "CastleChess v.2.0 - (c) 2026 by MFH",
    WELCOME_MESSAGE = "Welcome to "+VERSION_STRING+"\n";

void show_help() {
    std::cout << WELCOME_MESSAGE << R"(
Available Commands: (many of these are also command line arguments)
- 'd[isplay]': Display the current board state. Activates interactive mode.
- 'e[val]': Evaluate the current position ("depth 0 evaluation")
- 'fl[ip]'/'r[otate]': Flip the board perspective (toggle between White and Black view)
- 'f' or "fen" or "FEN:": show the FEN of the current position, 
    or reset board from the given FEN string (see also 'k' and 's')
- 'g' or 'go<N>': Let the engine play the best move it can find, 
    using the currently set search depth (default = 6); if given, set the depth to <N>.
    NOTE: g6 is a move ('g7g6') in SAN, therefore obviously not possible instead of go6!
- 'h[elp]': Show this help message
- 'i[nteractive]': Choose interactive mode. Allows you to enter moves in SAN or UCI format 
   (e.g., "Nf3", "dxe5", "e2e4", etc.) and display the board after each move, etc.
- 'k': Load the Kiwipete position, FEN r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1
- 'l[og][=<file>]': activate logging to the default file (PATH+"castlechess-debug.log", PATH="c:\temp\" or "/tmp/")
   or to the specified file. If 'l' is given a second time, or 'l0' or 'l-', logging is disabled.  
- 'ml': Show the list of pseudolegal moves in the current position.
- 'p[erft] [N]': Run perft test to depth N (e.g., 'p 5')
- 're[set]'/'rt'/'tr': Toggle ON/OFF TT reset (clear_tt()) upon restarting a game, i.e., parse_FEN().
- rules: CastleChess is like normal chess, but Black wins if they castle queenside ...O-O-O# (Ke8-g8)
   and White wins if Black loses the right to castle queenside (e.g., capture on a8) before they do.)
- 's': Load the standard starting position, FEN rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1
- 't': set the move time in milliseconds
- 'u': Undo the last move.
- 'uci': Enter UCI mode. In this mode not all commands are recognized.
- 'v[ersion]': show the VERSION_STRING.
- 'z': Toggle Zobrist Transposition Table ON/OFF (for testing)
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

std::ostream& timestamp(std::ostream& os) {
    auto now = std::chrono::system_clock::now();
    auto t   = std::chrono::system_clock::to_time_t(now);
    return os << std::put_time(std::localtime(&t), "%Y-%m-%d %H:%M:%S");
}

const char* DEFAULT_LOGFILE = "c:\\temp\\castlechess-debug.log";
std::ofstream logfile;
std::ostream* log = nullptr; // or &logfile or &std::cerr; or &nullstream, where
/*struct NullBuffer : std::streambuf {
    int overflow(int c) override { return c; }
};
NullBuffer nullbuf;
std::ostream nullstream(&nullbuf); */

void disable_log() { 
    if (log == &logfile && logfile) logfile.close();
    log = nullptr; 
}
std::ostream* enable_log(const char *log_filename = DEFAULT_LOGFILE) {
    if (log) {
        *log<<"ERROR: Tried to enable logging when already active.\n";
        return log;
    }
    logfile.open(log_filename, std::ios::app); // open for "append"
    if(logfile) return log = &logfile;
    std::cerr<<"ERROR: couldn't open log file '"<<log_filename<<"'.\n";
    return log = nullptr; // or std::err
}
std::ostream* open_logfile(const std::string message = ""){
    if(enable_log()){
        if (message.size()) *log << timestamp << " : " << message << std::endl;
    }
    else if(message.size()) std::cerr<<"Message was: '<<message<<'.\n";
    return log;
}


void select_starting_position(const std::string name, const std::string FEN) {
    queue_msg(name + " starting position selected:\n");//. ('d' to display)
    board.parse_fen(FEN); 
    if(interactive) board.print(flipped); // Q: should we reset flipped to false?
}
void show_move_list(Board& board) {
    MoveList list;
    board.generate_moves(list); 
    if (!list.count) {std::cout<<"No legal moves possible.\n"; return;}
    Engine::score_moves(list, board);
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
    if (interactive) {
        board.print(flipped);
        if (board.game_over()) {
            std::cout << "Game is over.\n";
            std::cout << "Result: " << board.result() << "\n";
        }
    }
}

void make_engine_move(){
    if (board.game_over()) {
        std::cout << "Game is over, no move can be made.\n";
        return;
    }
    if (interactive) std::cout << "Thinking... (depth = " << search_depth << ")\n";
    Engine::max_depth = search_depth;
    //std::chrono::high_resolution_clock::time_point
    if (Move bestmove = Engine::think(board)) {
        auto uci = move_to_uci(bestmove);
        // NOTE: must use 'san(...)' *before* making the move !!
        if (interactive) {
            int duration = Engine::elapsed_ms;
            std::cout << "Engine plays the move " << board.numbered_san(bestmove)
                <<" (uci: "<< uci << ", eval: "<<Engine::evaluation
                <<", "<<Engine::nodes_evaluated<<" nodes / "<<duration<<"ms = "
                <<(duration ? Engine::nodes_evaluated/duration : '-')<<"/ms).\n"
                <<"Transposition table: used "<<Engine::tt_used<<", stored "<<Engine::tt_stored<<".\n";
        } else {
            std::cout << "bestmove " << uci << std::endl;
            if (log) *log << "ENG -> bestmove " << uci << std::endl;
        }
        make_move_and_check_termination(bestmove);
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
        // NOTE: this must come *before* making the move, since 'san' needs the *current* board state to identify the move correctly.
        if (interactive)
            std::cout << "Move "<<board.numbered_san(m)<<" (uci: "<<move_to_uci(m)<<") executed:\n";
        make_move_and_check_termination(m);
        if (board.king_can_be_captured()) { // should only happen in interactive mode but who knows...
            std::cerr << "WARNING: Illegal move -- King can be captured! (Enter 'undo' to take back.)\n";
        }
    } else if (interactive) {
        std::cout << "Unknown command or illegal move '"<<cmd
            <<"'. (Type 'ml' to show the move list, 'h' for help.)\n";
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
    <<" BE CLEARED when the board is initialized." << ".\n";
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
void enable_interactive(){
    interactive = true; msg_queue_enabled = false;
    std::cerr<<"Switched to interactive mode.\n";
}
void parse_log_switch(const char *cmd){// cmd points to char AFTER 'l' (or maybe 'log')
    if(*cmd=='0' || *cmd=='-' || *cmd==0 && log) disable_log();
    else if (*cmd=='=') enable_log(cmd+1);
    else if (*cmd=='o' && cmd[1]=='g' && cmd[2]=='=') enable_log(cmd+3);
    else enable_log();
}
// parsing of cmd line args -- slightly different from parsing interactive user input
void parse_args(int argc, char* argv[]) {
    for(int i=1; i < argc; ++i) {
        switch(argv[i][0]){
        case '-': argv[i]++; --i; continue;// reread arg starting with char after '-'
        case 'd': /* display */ board.print(flipped); enable_interactive(); break;
        case 'f': if (++i >= argc) {
                    ERROR("Expected FEN string after 'f' command.");
                }
            select_starting_position(std::string(argv[i]), argv[i]); break;
        case 'g': // go = make engine move. check whether numerical arg. (depth) follows
            if (std::isdigit(argv[i][1]) && argv[i]++ // number follows immediately
             || i+1 < argc && std::isdigit(argv[i+1][0]) && ++i)
                search_depth = std::atoi(argv[i]);
            make_engine_move(); break;
        case 'h': case '?': show_help(); break;
        case 'i': enable_interactive(); break;
        case 'k': select_starting_position("Kiwipete", Kiwipete); break;
        case 'l': parse_log_switch(argv[i]); break;
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
        case 'u': /* 'undo' or enter UCI mode (if history is empty or 'uci' is given)*/
            if (history.empty()|| argv[i][1]=='c') interactive=false;
            else undo_move(); break;
        case 'v': std::cout<<VERSION_STRING; exit(1);            
        case 'z': toggle_tt(); break;            
        default: try_to_make_move(argv[i]);
        }
    }
}

void not_yet_implemented(std::string cmd){
    if (log) *log<<"ENG -> ignoring '"<<cmd<<"' - not yet implemented\n";
}

/* read & parse one line of input. return 0 if program should quit. */
int read_and_parse_command(std::istream* input){
    std::string line, cmd; std::getline(*input, line); 
    if (line.empty()) return 1; // shouldn't happen?
    if (log) *log << "GUI -> " << line << std::endl;// LOG THE INCOMING COMMAND
    std::istringstream iss(line);
    bool parsing_go_arguments = false; // will be set TRUE if 'go' is given but we have to read further args
    while(iss >> cmd) { // read from (this!) input until exhausted
        //if (log) *log << "GUI -> " << cmd << std::endl;// LOG THE INCOMING COMMAND
        // We must check for exact matches or unambiguous prefixes.
        // e.g., 'd2d4' starts with 'd' like "display", "perft" starts with 'p' like "print"
        if (cmd == "d" || cmd.substr(0,2) == "di" || cmd.substr(0,2) == "pr") {
            enable_interactive(); board.print(flipped); 
        }
        else if (cmd == "depth"){
            if (!(iss >> search_depth))
                std::cerr << "Expected numerical value for depth after 'go depth ...'.\n";
            else
                if (log) *log << "ENG -> setting search depth = " << search_depth << std::endl;
        }
        // 'e2e4' starts with 'e' like "eval" 
        else if (cmd == "e" || cmd == "eval") evaluate_position();
        // There are 2 commands that start with 'f': "flip" and "fen". 
        // We must take care not to "collide" with moves like "f4" or "fxe5" that start with 'f' as well.
        else if (cmd[0] == 'f' && cmd[1]=='l') flip_board(); // initial segment of "flip"
        // We allow "fen" for "show fen" (if not followed by a FEN string) or "set fen" if an argument is given
        // Anticipating interpretation of "fe" as shortcut for fxe<rank> (f-pawn takes on e-file), 
        // we require "fen" to be followed by a space or end of string.
        else if (cmd == "f" || cmd == "fen" || cmd == "FEN:") {
            // The FEN string may contain spaces, so we must read the rest of the line as the FEN string
            int fen_pos = line.find(cmd) + cmd.size()+1, // first character of FEN (if given)
                fen_end = line.find(" moves "); // are there moves or does FEN extend to end of line?
            // Note that in UCI mode, 'fen ...' is usually followed by 'moves...'
            iss.seekg(fen_end =      // Move the stream position to after "moves "
                fen_end == std::string::npos ? line.size() : fen_end + 7);
            auto FEN = line.substr(fen_pos, fen_end - fen_pos);
            if (fen_end - fen_pos < 15) { // A regular FEN can't be shorter than "8/8/8/8/8/8/8/8 ..."
                // So, in this case *nothing* should follow the 'fen' command
                // This should only happen in interacive, not in UCI mode.
                if (fen_end > fen_pos)
                    std::cerr<<"WARNING: Ignoring '"<<line.substr(fen_pos, fen_end - fen_pos)
                            <<"' following the 'fen' command.\n";
                std::cout << "FEN of the current position:\n" << board.fen() << std::endl;
                continue;
            }
            if (interactive) select_starting_position("Custom", FEN);
            else {
                if (log) *log << "ENG -> setting up FEN: " << FEN << std::endl;
                board.parse_fen(FEN);
            }
        }
        else if (cmd[0]=='g' && (cmd[1]==0 || cmd[1]=='o' && (
                                 cmd[2]==0 || std::isdigit(cmd[2]) ))) { // GO !
            // The user or the GUI tells us to start thinking.
            // The user may type just 'g' or 'go', or 'go6' for depth=6 
            // The GUI may say: go depth 8
            //              or: go movetime 5000
            //              or: go wtime 30000 btime 20000 [winc 0 binc 0]
            /* Core UCI go arguments (official specification):
These are the arguments defined in the original UCI protocol and supported by every compliant engine.
⏱ Time‑control parameters
- wtime <ms> — White’s remaining time in milliseconds
- btime <ms> — Black’s remaining time in milliseconds
- winc <ms> — White’s increment per move
- binc <ms> — Black’s increment per move
- movestogo <n> — Moves until next time control (optional)
🎯 Search‑limit parameters
- depth <n> — Search to a fixed depth
- nodes <n> — Search until a node limit is reached
- movetime <ms> — Search for exactly this many milliseconds
- mate <n> — Search for a forced mate in n plies
- infinite — Search until the GUI sends stop
🎯 Move‑restriction parameters
- searchmoves <move1> <move2> ...
Restrict the search to a subset of legal moves.

Additional widely‑supported arguments (de facto standard)
These are not in the original UCI document but are supported by Stockfish, Lc0, Komodo, Ethereal, Berserk, etc.
🧠 Multi‑PV and analysis features
- multipv <n> — Return n principal variations
- ponder — Start pondering (engine thinks during opponent’s time)
♟ Chess‑variant or engine‑specific extensions
Different engines add their own extensions. Common ones include:
- syzygy50, syzygyprobe, syzygypath — Tablebase probing controls
- evalfile, nnue, use_nnue — NNUE‑related toggles
- nodeslimit, timelimit — Alternative search limits
- searchtype — Some engines allow specifying aspiration windows, quiescence‑only search, etc.
These are not part of UCI itself; they vary by engine.
*/
            // We allow the shortcut 'go7' for 'go depth 7':
            if (cmd.size()>2) // we already checked that next arg is numeric
                search_depth = std::atoi(&cmd[2]); // user might *want* depth = 0 ...
            //Engine::max_time_ms = 0; // default: no time limit (engine may change this to a reasonable upper limit)
            parsing_go_arguments = true;
                // We might allow for a numerical arg. to be given as search depth
                //read_and_parse_command(cmd);
                // parse remaining args like 'depth', 'movetime' etc
        } 
        else if (cmd == "h" || cmd == "help" || cmd == "?") {
            show_help();
            if(!interactive)
                std::cout<<"Currently in UCI mode. Type 'i' to enter interactive mode.\n";
        }
        else if (cmd == "infinite") not_yet_implemented(cmd);
        else if (cmd == "isready") {// UCI: are we done initializing?
            std::cout << "readyok" << std::endl;   // yes!
            if (log) *log << "ENG -> readyok" << std::endl;
        }
        else if (cmd[0] == 'i') enable_interactive();
        else if (cmd == "k" || cmd == "kiwipete") select_starting_position("Kiwipete", Kiwipete);
        else if (cmd[0] == 'l') parse_log_switch(&cmd[1]);
        else if (cmd == "moves") continue; // We simply ignore this command, and subsequently
            // parse the moves as they appear, as if they were entered by the user.
//- movetime <ms> — Search for exactly this many milliseconds
        else if (cmd == "movetime" || cmd == "wtime" || cmd == "winc" || cmd == "btime" || cmd == "binc") {
            int time_limit_ms;
            if (!(iss >> time_limit_ms)) {
                std::cerr<<"Expected numerical value after '"<<cmd<<"'.\n";
                continue;
            }// one of 'w/btime' or 'w/binc': ignore if not side to move
            if(cmd.size()<8 && cmd[0] != (board.side == WHITE ? 'w' : 'b')) {
                if (log) *log<<"ENG -> ignoring command '" << cmd << "': not the side to move.\n";
                continue;
            }
            if (cmd.size() == 4) { // winc/binc
                // For simplicity, we add the increment to the time limit for this move.
                // TODO: we should check whether we have enough total time left!
                Engine::max_time_ms += time_limit_ms;
                continue;
            }
            if (cmd.size() == 5) { // wtime or btime 
// The GUI gives us a time for the entire game. Use that to set a reasonable time limit for this move.
// Very simple time management strategy: just divide the remaining time by an arbitrary factor (e.g., 30)
// to get a per-move time limit. We can make it more sophisticated based on position, remaining time, etc.
                time_limit_ms /= 30;
                cmd += "/30";
            } 
            Engine::max_time_ms = time_limit_ms;
            if (log) *log<<"ENG -> setting time limit "<<cmd<<" = "<< time_limit_ms <<" ms"<<std::endl;
            if (interactive) std::cout << "Setting time limit to "<< time_limit_ms <<" ms.\n";
        }
        else if (cmd == "mate"||cmd == "movestogo"||cmd == "multipv"||cmd == "nodes"
            ||cmd == "nodeslimit"||cmd == "timelimit") {
/* Optional args of 'go':
- movestogo <n> = Moves until next time control 
- nodes <n> — Search until a node limit is reached
- mate <n> — Search for a forced mate in n plies
- multipv <n> — Return n principal variations
- infinite — Search until the GUI sends stop
- ponder — Start pondering (engine thinks during opponent’s time)
*/
            std::string dummy; iss>>dummy;// ignore this and the following numerical argument
            if(!std::isdigit(dummy[0])) std::cerr<<"Skipping '"<<dummy
                <<"' following '"<<cmd<<"'. Should have been a numerical argument!\n";
            else not_yet_implemented(cmd+" <n>");
        }
        else if (cmd[0] == 'm') show_move_list(board);
        else if (cmd == "ponder") not_yet_implemented(cmd);
        else if (cmd == "position") continue; // We simply ignore this,
            // the GUI will tell us the current board state with "position startpos moves e2e4 e7e5 ..."
            // or "position fen <fen_string> moves ...".
            // We'll parse these commands by looking for "startpos" or "fen" in the main REPL
            // We don't need a specific parser here and can simply ignore this.
        else if (cmd[0] == 'p') { // PerfT (must be followed by numerical arg. = depth)
            if (cmd.length() > 1 && std::isdigit(cmd[1]))
                perft_depth = std::atoi(&cmd[1]);
            else if (!(iss>>perft_depth)) {
                std::cout << "Expected numerical value for perft depth.\n"; continue;
            }
            if (perft_depth > 0) board.perft_divide(perft_depth);
            else std::cout << "PerfT command must be given a depth value > 0.\n";
        }
        // 'qe2' (for 'Qe2') starts with 'q' like "quit",
        // "exd5" starts with "ex..." like "exit"
        else if (cmd == "q" || cmd == "quit" || cmd[0]=='x' || cmd == "exit") return 0;
        else if (cmd == "reset" || cmd == "rt" || cmd == "tr") 
            toggle_reset_tt();// reset/clear the TT upon starting a new game (parse_fen) 
        else if (cmd[0] == 'r' && (cmd[1]==0 || cmd[1]=='o'))// || cmd == "rot" || cmd == "rotate") 
            flip_board();
        else if (cmd == "s" || cmd.substr(0,3) == "sta") { // incl. UCI (command == "startpos")
            if (interactive) select_starting_position("Standard", STANDARD_FEN);
            else board.parse_fen(); 
        }
        else if (cmd == "uci") { // The GUI says hello. We reply with our info and "uciok"
            if(interactive){
                std::cerr<<"Entering non-interactive UCI mode. ('i' to switch back.)\n";
                interactive = false;
            }
            if (log) *log<<"GUI -> uci : entering UCI mode" << std::endl;
            std::cout << "id name " << VERSION_STRING << "\nid author MFH\nuciok" << std::endl;
            if (log) *log << "ENG -> id name CastleChess | id author MFH | uciok" << std::endl;

            // TODO:check whether TT's are initialized at startup 
            // clear_tt(); // we no NOT clear the Zobrist TT upon 'uci'. Do this explicitly!
            Engine::reset_tt = false; // don't clear the TT again/each time when we parse the FEN
            if (log) *log << "ENG -> cleared transposition table; set reset_tt = false" << std::endl;
        } 
        else if (cmd == "ucinewgame") { // New game is starting. We should clear the TT.
            clear_tt(); // board.parse_fen(); //STANDARD_FEN // only later, upon "position startpos".
        }
        else if (cmd[0] == 'u') undo_move();
        // SUBJECT TO CHANGE : we may want "t" for "time".
        else if (cmd[0] == 't' || cmd[0] == 'z') toggle_tt();  // use/don't use Zobrist TT
        else if (cmd[0] == 'v') // version
            std::cout<<"You are playing "<<VERSION_STRING<<"\nHave fun!\n";
        else try_to_make_move(cmd); // Attempt to parse input as a move
    }
    // input line is exhausted.
    if (parsing_go_arguments) make_engine_move();
    return 1; // don't quit
}
int main(int argc, char* argv[]) {
    // Write this to stderr, not stdout : the chess GUI would be confused by this,
    // but a user should know how they can switch to interactive mode.
    std::cerr << WELCOME_MESSAGE << "Type 'h' for help.\n";
    //open_logfile("Starting CastleChess.").close();
    msg_queue_enabled = 1; // suppress verbose output until we know we're not connected to a GUI
    init_tables(); // Initialize any necessary tables (like attack tables, Zobrist keys, etc.)
    if (argc > 1) parse_args(argc, argv);
    if (interactive){
        queue_msg("Engine Initialized.\n");// this actually empties the msg queue
        select_starting_position("Standard", STANDARD_FEN);
    } else {
        std::cerr << "Now in UCI mode, type 'i' to enter interactive mode.\n";
    }        
    do {
        if (interactive) {
            print_msg_queue();
            std::cout << "Your move or command, 'h' for help: "<<board.move_number_string();
        }
    } while (read_and_parse_command(&std::cin));

    queue_msg("Bye!\n");
    print_msg_queue();
    return 0;
}
