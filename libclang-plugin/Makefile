CFLAGS_COMMON := -g -Weverything -Wno-used-but-marked-unused -Wno-declaration-after-statement -Wno-gnu-zero-variadic-macro-arguments -Wno-disabled-macro-expansion -isystem /usr/local/opt/llvm/include -isystem .. -std=c99
CFLAGS := -march=native -O2

LDFLAGS += -L/usr/local/opt/llvm/lib

detect-no-zero-initialization: detect-no-zero-initialization.c
	$(CC) $(CFLAGS_COMMON) $(CFLAGS) $< -o $@ $(LDFLAGS) -lclang

test: test.cpp
	$(CXX) $(CXXFLAGS) -g -std=c++11 -Wall -Wextra $^ -o $@ $(LDFLAGS)

simple: simple.cpp
	$(CXX) $(CXXFLAGS) -g -std=c++11 -Wall -Wextra $^ -o $@ $(LDFLAGS)
