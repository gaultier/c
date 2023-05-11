#!/bin/sh

musl-gcc \
  -O0 \
  main.c \
  -g3 \
  -static \
  -Wall \
  -Wconversion \
  -Wdouble-promotion \
  -Wextra \
  -Wno-sign-conversion \
  -Wno-unused-function \
  -Wno-unused-parameter \
  -o x11_debug.bin
