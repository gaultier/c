LDFLAGS += -lpthread -ldl

gitlab-url-copy: gitlab-url-copy.c
	$(CC) -Weverything -Wno-used-but-marked-unused -Wno-gnu-zero-variadic-macro-arguments -g $(CFLAGS) $^ -o $@ $(LDFLAGS) -std=c99
