CFLAGS_COMMON := -g -Weverything -Wno-used-but-marked-unused -Wno-declaration-after-statement -Wno-gnu-zero-variadic-macro-arguments -isystem /usr/local/include/ -std=c99

pg_test: pg_test.c
	$(CC) $(CFLAGS_COMMON) $(CFLAGS) $^ -o $@ $(LDFLAGS) -lm

test: pg_test
	./$^

.PHONY: test
