# castlechess
This project is dedicated to study the CastleChess variant, invented by our late friend Eric Angelini.

CastleChess rules are exactly like normal chess rules, except for two additional terminating conditions:
- Black wins if they manage to castle long / queenside: ...O-O-O ; Ke8-g8
- White wins if Black loses the right to castle long (e.g., if the king or the rook on a8 moves or is captured.

As a consequence, games are often short, ~ 10 - 15 moves. Material is almost irrelevant, e.g., White may sacrifice their queen on a7, which is a win for them if Black can't instantly castle long and the queen can't be taken by a piece different from the rook on a8. (Black has lost if they move the Rook, and otherwise the Queen takes the Rook on the next move.)

We provide a chess engine that can be used to play in any chess GUI supporting the UCI protocol.

Based on collected information, we will try to rigorously prove that the game is won by one of the sides, with optimal play.
