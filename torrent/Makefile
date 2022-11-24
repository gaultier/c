bencode_test: bencode_test.c
	$(CC) -Wall -Wextra -g $(CFLAGS) $^ -o $@ $(LDFLAGS)

torrent_test: test.c bencode.h peer.h sha1.h tracker.h
	$(CC) -Wall -Wextra -g $(CFLAGS) $< -o $@ $(LDFLAGS) -luv -lcurl

bencode_dump: bencode_dump.c
	$(CC) -Wall -Wextra -g $(CFLAGS) $^ -o $@ $(LDFLAGS)

torrent: main.c bencode.h peer.h sha1.h tracker.h
	$(CC) -Weverything -Wno-used-but-marked-unused -g $(CFLAGS) $< -o $@ $(LDFLAGS) -lcurl -luv -lm -isystem /usr/local/include/ -std=c99 

test: bencode_test torrent_test
	./torrent_test
	./bencode_test

.PHONY: test
