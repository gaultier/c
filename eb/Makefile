LDFLAGS += -lpthread
eb: main.c
	$(CC) -Weverything -g $(CFLAGS) $^ -o $@ $(LDFLAGS)

install: eb
	cp $^ /usr/local/bin/

