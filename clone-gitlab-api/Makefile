CFLAGS ?= -O2
CFLAGS += -Wall -Wno-unused-function -g
LDLIBS := -lcurl

clone-gitlab-api: main.c
	$(CC) $(CFLAGS) main.c -o clone-gitlab-api $(LDFLAGS) $(LDLIBS)
