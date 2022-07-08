CFLAGS ?= -O2
LDLIBS := -lcurl

clone-gitlab-api: main.c
	$(CC) $(CFLAGS) -Wall -Wno-unused-function -g main.c -o clone-gitlab-api $(LDFLAGS) $(LDLIBS)
