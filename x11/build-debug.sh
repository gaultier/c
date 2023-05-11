#!/bin/sh

musl-gcc \
  -O0 \
  main.c \
  -g3 \
  -static \
  -Wall \
  -Wcast-align \
  -Wconversion \
  -Wdouble-promotion \
  -Wextra \
  -Wno-sign-conversion \
  -Wno-unused-function \
  -Wno-unused-parameter \
  -Wnull-dereference \
  -Wwrite-strings \
  -o x11_debug.bin
