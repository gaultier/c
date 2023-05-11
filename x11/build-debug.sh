#!/bin/sh

musl-gcc -O0 main.c -g -static -o x11_debug.bin -Wall -Wextra
