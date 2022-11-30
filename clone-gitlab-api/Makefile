.PHONY: install

CFLAGS_COMMON := -g -Weverything -Wno-used-but-marked-unused -Wno-declaration-after-statement -Wno-gnu-zero-variadic-macro-arguments -Wno-disabled-macro-expansion -isystem /usr/local/include/ -isystem .. -std=c99
CFLAGS := -march=native -O2
LDLIBS := -lcurl -lpthread
LDFLAGS :=

PREFIX = /usr/local

clone-gitlab-api: main.c
	$(CC) $(CFLAGS_COMMON) $(CFLAGS) $< -o $@ $(LDFLAGS) $(LDLIBS)

install: clone-gitlab-api
	mkdir -p $(PREFIX)/bin
	cp -f $< $(PREFIX)/bin/
