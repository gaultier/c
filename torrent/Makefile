bencode_test: bencode_test.c
	$(CC) -Wall -Wextra -g $(CFLAGS) $^ -o $@ $(LDFLAGS)

bencode_dump: bencode_dump.c
	$(CC) -Wall -Wextra -g $(CFLAGS) $^ -o $@ $(LDFLAGS)

test: bencode_test
	./$^

.PHONY: test
