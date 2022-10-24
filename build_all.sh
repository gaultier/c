#/bin/sh
set -xe

fd Makefile --exclude 'vendor' -x make -C {//} -B
