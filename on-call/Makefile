LDFLAGS += -lpthread -ldl

on-call: main.c
	$(CC) $(CFLAGS) -Weverything -g main.c -o on-call $(LDFLAGS) -Wno-used-but-marked-unused -lm
