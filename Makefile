castlechess: main.cpp board.cpp
	g++ -O3 main.cpp board.cpp -lfmt -o castlechess

clean:
	rm -f castlechess
