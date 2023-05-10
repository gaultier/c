#!/bin/sh

cc -O2 main.c -Wl,--gc-sections -g -nostdlib -e start -static  -nodefaultlibs -nostartfiles -o x11_release.bin
