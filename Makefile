all: c

c:
	gcc -Wall -Werror -std=c99 -O0 -g -o test test.c && valgrind --leak-check=full ./test
