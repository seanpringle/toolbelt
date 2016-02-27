all: c

c:
	gcc -Wall -Werror -std=c99 -O0 -g -o test test.c && valgrind --leak-check=full ./test

pg:
	gcc -Wall -Werror -std=c99 -O0 -g -o pgtest pgtest.c -lpq && valgrind --leak-check=full ./pgtest