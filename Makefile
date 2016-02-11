all: c

c:
	gcc -Wall -Werror -O0 -g -o test test.c && valgrind --leak-check=full ./test