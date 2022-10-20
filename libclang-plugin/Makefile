CFLAGS += -I/usr/local/opt/llvm/include

LDFLAGS += -L/usr/local/opt/llvm/lib

detect-ub-pod-cpp: detect-ub-pod-cpp.c
	$(CC) $(CFLAGS) -g -std=c99 -Wall -Wextra $^ -o $@ $(LDFLAGS) -lclang
