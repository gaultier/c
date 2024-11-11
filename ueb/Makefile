all: process_descriptor.bin self_pipe.bin self_pipe2.bin sigtimedwait.bin kqueue.bin io_uring.bin

process_descriptor.bin: process_descriptor.c
	$(CC) $^ -o $@ $(CFLAGS) $(LDFLAGS)

self_pipe.bin: self_pipe.c
	$(CC) $^ -o $@ $(CFLAGS) $(LDFLAGS)

self_pipe2.bin: self_pipe2.c
	$(CC) $^ -o $@ $(CFLAGS) $(LDFLAGS)

sigtimedwait.bin: sigtimedwait.c
	$(CC) $^ -o $@ $(CFLAGS) $(LDFLAGS)

kqueue.bin: kqueue.c
	$(CC) $^ -o $@ $(CFLAGS) $(LDFLAGS)

io_uring.bin: io_uring.c
	$(CC) $^ -o $@ $(CFLAGS) $(LDFLAGS) -luring

clean:
	rm ./*.bin

.PHONY: all clean
