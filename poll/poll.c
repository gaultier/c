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
#include <sys/poll.h>
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
            .sin_port = htons(8081),
            .sin_addr = {.s_addr = inet_addr("127.0.0.1")},
        }};

    struct pollfd fds[3] = {0};
    int fds_len = 0;

    for (int i = 0; i < sizeof(addresses) / sizeof(addresses[0]); i++) {
        int fd = socket(AF_INET, SOCK_STREAM, 0);
        assert(fd != -1);
        assert(fcntl(fd, F_SETFL, O_NONBLOCK) != -1);

        int res = connect(fd, (void*)&addresses[i], sizeof(addresses[i]));
        if (res == -1 && errno != EINPROGRESS) {
            fprintf(stderr, "Failed connect(2): %s\n", strerror(errno));
        }

        fds[i].fd = fd;
        fds[i].events = POLLERR | POLLIN | POLLHUP;
        fds_len++;
    }

    for (;;) {
        int res = poll(fds, fds_len, -1);
        if (res == -1) {
            fprintf(stderr, "Failed poll(2): %s\n", strerror(errno));
            exit(errno);
        }
        printf("poll: %d\n", res);

        for (int i = 0; i < res; i++) {
            struct pollfd* event = &fds[i];
            __builtin_dump_struct(event, &printf);

            if (event->revents & POLLERR) {
                fprintf(stderr, "POLLERR: i=%d\n", i);

                for (int j = 0; j < fds_len; j++) {
                    if (fds[j].fd == event->fd) {
                        memcpy(&fds[j], &fds[fds_len - 1],
                               sizeof(struct pollfd));
                        fds_len--;
                        break;
                    }
                }
                i = res;
                continue;
            } else if (event->revents & POLLIN) {
                int err = 0;
                socklen_t err_size = sizeof(err);
                getsockopt(event->fd, SOL_SOCKET, SO_ERROR, &err, &err_size);
                if (err == 0) {
                    printf("Connected: i=%d\n", i);

                    static char buf[1 << 14];
                    bzero(buf, sizeof(buf));
                    res = recv(event->fd, buf, sizeof(buf), 0);
                    if (res == -1) {
                        fprintf(stderr, "Failed recv(2): i=%d err=%s\n", i,
                                strerror(errno));
                        continue;
                    } else if (res == 0) {
                        fprintf(stderr,
                                "Failed recv(2): i=%d res=%d errno=%d\n", i,
                                res, errno);

                        // for (int j = 0; j < fds_len; j++) {
                        //     if (fds[j].fd == event->fd) {
                        //         memcpy(&fds[j], &fds[fds_len - 1],
                        //                sizeof(struct pollfd));
                        //         fds_len--;
                        //         break;
                        //     }
                        // }
                        continue;
                    }
                } else {
                    fprintf(stderr, "Failed async connect(2): i=%d err=%s\n", i,
                            strerror(err));
                    for (int j = 0; j < fds_len; j++) {
                        if (fds[j].fd == event->fd) {
                            memcpy(&fds[j], &fds[fds_len - 1],
                                   sizeof(struct pollfd));
                            fds_len--;
                            break;
                        }
                    }
                    continue;
                }
            }
        }
    }
}
