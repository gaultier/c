# C tools

A diverse collection of useful (to me!) software written in C. Memory profiling, http server, editor, crash reporter, clang plugin, torrent client, etc.

*This is experimental code. Probably don't use it!*

## Quick start

**Make sure you have the git submodules locally: git submodule update --init --recursive**

Requirements: make, a C99 compiler.

```sh
$ ./build_vendors.sh
$ ./build_all.sh
```

## Organisation

Each subdirectory is a separate project. A project only depends on the common utility header `./pg/pg.h` and perhaps some vendored libraries under `vendor/`.

Third party libraries are added as a git submodule under `vendor/` so: `git submodule add https://github.com/curl/curl.git vendor/curl`.

They are then built from source in `./build_vendors.sh` to control exactly the build flags and feature flags.

Static linking, C99, posix tools and not depending on what's installed in the current environment are highly preferred (but there are exceptions).

## LICENSE 

BSD-3
