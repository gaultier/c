#!/bin/sh
set -e

CFLAGS="${CFLAGS}"
WARNINGS="$(tr -s '\n' ' ' < compile_flags.txt)"

clang eb.c -nostdlib  -nostartfiles $WARNINGS -nobuiltininc -fuse-ld=lld -e _start -static $CFLAGS
