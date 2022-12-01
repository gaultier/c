.PHONY: all

CFLAGS_COMMON := -g -Weverything -Wno-used-but-marked-unused -Wno-declaration-after-statement -Wno-gnu-zero-variadic-macro-arguments -Wno-disabled-macro-expansion -isystem /usr/local/include/ -isystem .. -std=c99
CFLAGS := -march=native -O2
LDFLAGS += -lpthread -ldl

gitlab-url-copy: gitlab-url-copy.c
	$(CC) $(CFLAGS_COMMON) $(CFLAGS) $< -o $@ $(LDFLAGS) -lm

all: gitlab-url-copy
