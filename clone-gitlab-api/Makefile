.PHONY: install

CFLAGS_COMMON := -g -Weverything -Wno-used-but-marked-unused -Wno-declaration-after-statement -Wno-gnu-zero-variadic-macro-arguments -Wno-disabled-macro-expansion -isystem /usr/local/include/ -std=c99
LDLIBS := -lcurl -lpthread
LDFLAGS := -flto -L /usr/local/lib/

clone-gitlab-api: main.c
	$(CC) $(CFLAGS_COMMON) $(CFLAGS) $< -o $@ $(LDFLAGS) $(LDLIBS)

install: clone-gitlab-api
	cp $< /usr/local/bin/
