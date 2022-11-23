LDFLAGS += -lpthread
eb: main.c
	$(CC) -Weverything -g $(CFLAGS) $^ -o $@ $(LDFLAGS) -std=c99

install: eb
	cp $^ /usr/local/bin/

