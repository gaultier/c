LDFLAGS += -lpthread
eb: main.c
	$(CC) -Wall -Wextra -g $(CFLAGS) $^ -o $@ $(LDFLAGS)

install: eb
	cp $^ /usr/local/bin/

