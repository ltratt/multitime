CC = gcc
CFLAGS = -Wall -Wextra -O0 -fprofile-arcs -ftest-coverage
LIBS = -lcheck -lm -lpthread -lrt

.PHONY: all clean

all: test_stats

test: test_stats
	./test_stats

test_stats: test_stats.c ../statistics.o
	$(CC) $(CFLAGS) -o test_stats ../statistics.o test_stats.c $(LIBS)

test_stats.c: test_stats.check
	checkmk test_stats.check > test_stats.c

clean:
	rm -f test_stats.c test_stats.o test_stats.gcno test_stats.check.gcov test_stats.gcda test_stats
