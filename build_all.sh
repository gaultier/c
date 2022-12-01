#!/bin/sh
set -xe

find . -name Makefile -type f -not -path './vendor/*' -execdir make CC="${CC:-clang}" all $MAKE_OPTS \;
