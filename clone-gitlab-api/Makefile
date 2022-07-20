.PHONY: install

CFLAGS ?= -O2
LDLIBS := -lcurl -lpthread
LDFLAGS ?= -flto

clone-gitlab-api: main.c
	$(CC) $(CFLAGS) -Wall -Wno-unused-function -g main.c -o clone-gitlab-api $(LDFLAGS) $(LDLIBS)

install: clone-gitlab-api
	cp clone-gitlab-api /usr/local/bin/
