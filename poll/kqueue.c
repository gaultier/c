#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/errno.h>
#include <sys/event.h>
#include <sys/socket.h>
#include <unistd.h>

int main() {
    /* const servers = [['87.148.1.40', 6881], ['72.235.131.120', 6881],
     * ['80.78.21.2', 35676]]; */
    struct sockaddr_in addresses[] = {
        {
            .sin_family = AF_INET,
            .sin_port = htons(6881),
            .sin_addr = {.s_addr = inet_addr("87.148.1.40")},
        },
        {
            .sin_family = AF_INET,
            .sin_port = htons(6881),
            .sin_addr = {.s_addr = inet_addr("72.235.131.120")},
        },
        {
            .sin_family = AF_INET,
            .sin_port = htons(35676),
            .sin_addr = {.s_addr = inet_addr("80.78.21.2")},
        }};

    int queue = kqueue();
    assert(queue != -1);
    for (int i = 0; i < sizeof(addresses) / sizeof(addresses[0]); i++) {
        int fd = socket(AF_INET, SOCK_STREAM, 0);
        assert(fd != -1);
        assert(fcntl(fd, F_SETFL, O_NONBLOCK) != -1);

        int res = connect(fd, (void*)&addresses[i], sizeof(addresses[i]));
        if (res == -1 && errno != EINPROGRESS) {
            fprintf(stderr, "Failed connect(2): %s\n", strerror(errno));
        }

        struct kevent events[] = {{
            .ident = fd,
            .filter = EVFILT_READ,
            .flags = EV_ADD,
            .udata = &addresses[i],
        }};
        res = kevent(queue, events, sizeof(events) / sizeof(events[0]), NULL, 0,
                     NULL);
        if (res == -1) {
            fprintf(stderr, "Failed kevent(2): %s\n", strerror(errno));
        }
    }

    for (;;) {
        struct kevent events[3] = {0};
        int res = kevent(queue, NULL, 0, events,
                         sizeof(events) / sizeof(events[0]), NULL);
        if (res == -1) {
            fprintf(stderr, "Failed kevent(2): %s\n", strerror(errno));
        }

        printf("kevent: %d\n", res);

        for (int i = 0; i < res; i++) {
            struct kevent* event = &events[i];
            if (event->filter == EVFILT_READ && event->data == 0) {
                printf("Nothing to read, skipping\n");
                sleep(1);
                continue;
            }
            __builtin_dump_struct(event, &printf);

            if (event->filter == EVFILT_READ && (event->fflags & EV_EOF)) {
                fprintf(stderr, "EOF: i=%d\n", i);

                struct kevent events[] = {{
                    .ident = event->ident,
                    .filter = EVFILT_READ,
                    .flags = EV_DELETE,
                }};
                res = kevent(queue, events, sizeof(events) / sizeof(events[0]),
                             NULL, 0, NULL);
                if (res == -1) {
                    fprintf(stderr, "Failed kevent(2): %s\n", strerror(errno));
                }
                continue;
            } else if (event->filter == EVFILT_READ) {
                int err = 0;
                socklen_t err_size = sizeof(err);
                getsockopt(event->ident, SOL_SOCKET, SO_ERROR, &err, &err_size);
                if (err == 0) {
                    printf("Connected: i=%d data=%zu\n", i,
                           (size_t)event->data);

                    static char buf[1 << 14];
                    bzero(buf, sizeof(buf));
                    res = recv(event->ident, buf, event->data, 0);
                    if (res == -1) {
                        fprintf(stderr, "Failed recv(2): i=%d err=%s\n", i,
                                strerror(errno));
                        continue;
                    } else if (res == 0) {
                        fprintf(stderr, "Failed recv(2): i=%d EOF\n", i);

                        struct kevent events[] = {{
                            .ident = event->ident,
                            .filter = EVFILT_READ,
                            .flags = EV_DELETE,
                        }};
                        res = kevent(queue, events,
                                     sizeof(events) / sizeof(events[0]), NULL,
                                     0, NULL);
                        if (res == -1) {
                            fprintf(stderr, "Failed kevent(2): %s\n",
                                    strerror(errno));
                        }
                        continue;
                    }
                } else {
                    fprintf(stderr, "Failed async connect(2): i=%d err=%s\n", i,
                            strerror(err));
                    struct kevent events[] = {{
                        .ident = event->ident,
                        .filter = EVFILT_READ,
                        .flags = EV_DELETE,
                    }};
                    res = kevent(queue, events,
                                 sizeof(events) / sizeof(events[0]), NULL, 0,
                                 NULL);
                    if (res == -1) {
                        fprintf(stderr, "Failed kevent(2): %s\n",
                                strerror(errno));
                    }
                    continue;
                }
            }
        }
    }

    //    int err = 0;
    //    char buf[256] = {0};
    //    while ((err = poll(fds, 1, 0)) != -1) {
    //        if (fds[0].revents & POLLIN) {
    //            puts("Something to read");
    //            err = recv(sock, buf, sizeof(buf), 0);
    //            if (err <= 0) {
    //                fprintf(stderr, "Read err: %s\n", strerror(errno));
    //            } else {
    //                printf("Read %d\n", err);
    //            }
    //        } else if (fds[0].revents & POLLOUT) {
    //            puts("Something to write");
    //        }
    //    }
    //    if (err == -1) {
    //        fprintf(stderr, "Poll err: %s\n", strerror(errno));
    //    }
}
