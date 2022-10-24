LDFLAGS += -lpthread
eb: main.c
	$(CC) -Wall -Wextra -g $(CFLAGS) main.c -o eb $(LDFLAGS)

install: eb
	cp $^ /usr/local/bin/

