bencode_test: bencode_test.c
	$(CC) -Wall -Wextra -g $(CFLAGS) $^ -o $@ $(LDFLAGS)

bencode_dump: bencode_dump.c
	$(CC) -Wall -Wextra -g $(CFLAGS) $^ -o $@ $(LDFLAGS) -lcurl

torrent: main.c bencode.h peer.h sha1.h tracker.h
	$(CC) -Wall -Wextra -g $(CFLAGS) $< -o $@ $(LDFLAGS) -lcurl -luv

test: bencode_test
	./$^

.PHONY: test
