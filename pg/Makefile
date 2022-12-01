.PHONY: all test

CFLAGS_COMMON := -g -Weverything -Wno-used-but-marked-unused -Wno-declaration-after-statement -Wno-gnu-zero-variadic-macro-arguments -Wno-disabled-macro-expansion -isystem /usr/local/include/ -isystem .. -std=c99
CFLAGS := -march=native -O2

pg_test: pg_test.c
	$(CC) $(CFLAGS_COMMON) $(CFLAGS) $^ -o $@ $(LDFLAGS) -lm

test: pg_test
	./$^

all: pg_test
