CFLAGS_COMMON := -g -Weverything -Wno-used-but-marked-unused -Wno-declaration-after-statement -Wno-gnu-zero-variadic-macro-arguments -Wno-disabled-macro-expansion -isystem /usr/local/include/ -std=c99
LDFLAGS += -lpthread -ldl
CFLAGS := -march=native -O2

editor: main.c
	$(CC) $(CFLAGS_COMMON) $(CFLAGS) $^ -o $@ $(LDFLAGS)
