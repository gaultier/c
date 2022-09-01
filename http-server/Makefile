CFLAGS:=-Wall -Wextra -Wno-unused-function

proc-per-req: proc-per-req.c deps.a
	$(CC) $(CFLAGS) $(LDFLAGS) $^ -o $@

deps.a: vendor/picohttpparser/picohttpparser.c ../vendor/lmdb/libraries/liblmdb/mdb.c ../vendor/lmdb/libraries/liblmdb/midl.c
	$(CC) $(CFLAGS) $^ -c 
	$(AR) -r deps.a picohttpparser.o mdb.o midl.o


req: req.c
