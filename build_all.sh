#!/bin/sh
set -xe

find . -name Makefile -type f -not -path './vendor/*' -execdir make -B CC=clang  \;
