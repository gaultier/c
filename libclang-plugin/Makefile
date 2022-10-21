CFLAGS += -I/usr/local/opt/llvm/include

LDFLAGS += -L/usr/local/opt/llvm/lib

detect-no-zero-initialization: detect-no-zero-initialization.c
	$(CC) $(CFLAGS) -g -std=c99 -Wall -Wextra $^ -o $@ $(LDFLAGS) -lclang


test: test.cpp
	$(CXX) $(CXXFLAGS) -g -std=c++11 -Wall -Wextra $^ -o $@ $(LDFLAGS)
