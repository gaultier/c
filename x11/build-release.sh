#!/bin/sh

musl-gcc -O2 main.c -Wl,--gc-sections -g -static -o x11_release.bin -Wall -Wextra
