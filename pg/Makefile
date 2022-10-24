pg_test: pg_test.c
	$(CC) -Wall -Wextra -g $(CFLAGS) $^ -o $@ $(LDFLAGS)

test: pg_test
	./$^

.PHONY: test
