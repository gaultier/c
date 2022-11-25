LDFLAGS += -lpthread -ldl

proc-per-req: proc-per-req.c deps.a
	$(CC) $(CFLAGS) $(LDFLAGS) $^ -Wall -Wextra -Wno-unused-function -g -o $@

deps.a: ../vendor/picohttpparser/picohttpparser.c
	$(CC) $(CFLAGS) $^ -Wall -Wextra -Wno-unused-function -g -c 
	$(AR) -r deps.a picohttpparser.o


req: req.c
