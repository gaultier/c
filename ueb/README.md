# Way too many ways to wait on a child process with a timeout

See the [blog article](https://gaultier.github.io/blog/wait_process_timeout.html).

Build: `make`.

On Linux, to build `kqueue.bin`, `libkqueue` is required e.g.:

```sh
$ make kqueue.bin CFLAGS='/path/to/libkqueue.a -I /path/to/libkqueue/include/'
```
