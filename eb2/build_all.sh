#!/bin/sh
set -e

TARGET_ARCH=arm TARGET_OS=linux TARGET_ENV=eabihf ./build.sh
TARGET_ARCH=aarch64 TARGET_OS=linux ./build.sh
TARGET_ARCH=x86_64 TARGET_OS=linux ./build.sh
TARGET_ARCH=x86_64 TARGET_OS=freebsd ./build.sh
