.PHONY: install

CFLAGS ?= -O2 -I/usr/local/include/
LDLIBS := -lcurl -lpthread
LDFLAGS ?= -flto -L/usr/local/lib/

clone-gitlab-api: main.c
	$(CC) $(CFLAGS) -Wall -Wno-unused-function -g main.c -o clone-gitlab-api $(LDFLAGS) $(LDLIBS)

install: clone-gitlab-api
	cp clone-gitlab-api /usr/local/bin/
