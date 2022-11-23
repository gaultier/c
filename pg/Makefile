pg_test: pg_test.c
	$(CC) -Weverything -Wno-used-but-marked-unused -g $(CFLAGS) $^ -o $@ $(LDFLAGS) -lm -std=c99

test: pg_test
	./$^

.PHONY: test
