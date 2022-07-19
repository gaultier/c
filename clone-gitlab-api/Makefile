.PHONY: install

CFLAGS ?= -O2
LDLIBS := -lcurl
LDFLAGS ?= -flto

clone-gitlab-api: main.c ../opentelemetry-jaeger/opentelemetry.h ../opentelemetry-jaeger/opentelemetry.h
	$(CC) $(CFLAGS) -Wall -Wno-unused-function -g main.c ../opentelemetry-jaeger/opentelemetry.c -o clone-gitlab-api $(LDFLAGS) $(LDLIBS)

install: clone-gitlab-api
	cp clone-gitlab-api /usr/local/bin/
