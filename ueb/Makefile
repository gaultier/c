all: process_descriptor.bin self_pipe.bin sigtimedwait.bin kqueue.bin

process_descriptor.bin: process_descriptor.c
	$(CC) $^ -o $@ $(CFLAGS) $(LDFLAGS)

self_pipe.bin: self_pipe.c
	$(CC) $^ -o $@ $(CFLAGS) $(LDFLAGS)

sigtimedwait.bin: sigtimedwait.c
	$(CC) $^ -o $@ $(CFLAGS) $(LDFLAGS)

kqueue.bin: kqueue.c
	$(CC) $^ -o $@ $(CFLAGS) $(LDFLAGS)

clean:
	rm ./*.bin

.PHONY: all clean
