CFLAGS_COMMON := -g -Weverything -Wno-used-but-marked-unused -Wno-declaration-after-statement -Wno-gnu-zero-variadic-macro-arguments -Wno-disabled-macro-expansion -Wno-fixed-enum-extension -Wno-switch-enum -Wno-covered-switch-default -isystem /usr/local/include/ -isystem .. -std=c99
CFLAGS := -march=native -O2

crash-reporter: main.c
	$(CC) $(CFLAGS_COMMON) $(CFLAGS) $< -o $@ $(LDFLAGS)
