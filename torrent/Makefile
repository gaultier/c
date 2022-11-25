CFLAGS_COMMON := -g -Weverything -Wno-used-but-marked-unused -Wno-declaration-after-statement -Wno-gnu-zero-variadic-macro-arguments -isystem /usr/local/include/ -std=c99

LDFLAGS := -L/usr/local/lib/

bencode_test: bencode_test.c
	$(CC) $(CFLAGS_COMMON) $(CFLAGS) $^ -o $@ $(LDFLAGS)

torrent_test: test.c bencode.h peer.h sha1.h tracker.h
	$(CC) $(CFLAGS_COMMON) $(CFLAGS) $< -o $@ $(LDFLAGS) -luv -lcurl -lm

bencode_dump: bencode_dump.c
	$(CC) $(CFLAGS_COMMON) $(CFLAGS) $^ -o $@ $(LDFLAGS)

torrent: main.c bencode.h peer.h sha1.h tracker.h
	$(CC) $(CFLAGS_COMMON) $(CFLAGS) $< -o $@ $(LDFLAGS) -lcurl -luv -lm 

test: bencode_test torrent_test
	./torrent_test
	./bencode_test

.PHONY: test
