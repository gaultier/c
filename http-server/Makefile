CFLAGS_COMMON := -g -Weverything -Wno-used-but-marked-unused -Wno-declaration-after-statement -Wno-gnu-zero-variadic-macro-arguments -Wno-disabled-macro-expansion -Wno-cast-qual -isystem .. -isystem /usr/local/include/ -std=c99
LDFLAGS += -lpthread -ldl

proc-per-req: proc-per-req.c deps.a
	$(CC) $(CFLAGS_COMMON) $(CFLAGS) $(LDFLAGS) $^ -o $@

deps.a: ../vendor/picohttpparser/picohttpparser.c
	$(CC) $(CFLAGS) $^ -g -c 
	$(AR) -r deps.a picohttpparser.o


req: req.c
	$(CC) $(CFLAGS_COMMON) $(CFLAGS) $(LDFLAGS) $^ -o $@
