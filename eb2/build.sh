#!/bin/sh
set -e

CFLAGS="${CFLAGS}"
WARNINGS="$(tr -s '\n' ' ' < compile_flags.txt)"
TARGET_OS="${TARGET_OS:-$(uname | tr '[:upper:]' '[:lower:]')}"
TARGET_ARCH="${TARGET_ARCH:-$(uname -m)}"
TARGET_ENV="${TARGET_ENV:-unknown}"

clang eb.c -nostdlib  -nostartfiles $WARNINGS -nobuiltininc -fuse-ld=lld -e _start -static $CFLAGS --target="$TARGET_ARCH"-"$TARGET_OS"-"$TARGET_ENV"
