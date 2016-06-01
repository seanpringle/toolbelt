all: t

t:
	gcc -Wall -Werror -std=c99 -O0 -g -o test test.c && valgrind --leak-check=full ./test

netcdf:
	gcc -Wall -Werror -std=c99 -O0 -g -o netcdf_json netcdf_json.c -lnetcdf
