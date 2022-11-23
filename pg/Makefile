pg_test: pg_test.c
	$(CC) -Weverything -g $(CFLAGS) $^ -o $@ $(LDFLAGS) -lm -Wno-used-but-marked-unused

test: pg_test
	./$^

.PHONY: test
