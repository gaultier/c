#!/bin/sh

musl-gcc \
  -O2 \
  main.c \
  -static \
  -Wl,--gc-sections \
  -Wl,-s \
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
  -o x11_release.bin
