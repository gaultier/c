#pragma once
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/event.h>
#include <sys/fcntl.h>
#include <sys/time.h>
#include <sys/types.h>

#define GB_STRING_IMPLEMENTATION
// FIXME
#define GB_ALLOC malloc
#define GB_FREE free

#include "./vendor/gb_string.h"

#define MAX_EVENT 1

void fs_watch_file(gbString* path) {
  assert(path != NULL);

  const int fd = open(*path, O_RDONLY);
  if (fd == -1) {
    fprintf(stderr, "Failed to open the file %s: %s\n", *path, strerror(errno));
    return;
  }

  const int queue = kqueue();
  if (queue == -1) {
    fprintf(stderr, "Failed to create queue with kqueue(): %s\n",
            strerror(errno));
    return;
  }

  int event_count = 0;
  struct kevent change_list[MAX_EVENT] = {};
  EV_SET(&change_list[0], fd, EVFILT_VNODE, EV_ADD | EV_CLEAR,
         NOTE_WRITE | NOTE_DELETE | NOTE_RENAME | NOTE_REVOKE, 0, 0);
  struct kevent event_list[MAX_EVENT] = {};

  printf("Watching file %s\n", *path);
  while (1) {
    event_count =
        kevent(queue, change_list, MAX_EVENT, event_list, MAX_EVENT, NULL);

    if (event_count == -1) {
      fprintf(stderr, "Failed to get the events with kevent(): %s\n",
              strerror(errno));
      return;
    }

    for (int i = 0; i < MAX_EVENT; i++) {
      struct kevent* e = &event_list[i];
      if (e->fflags & NOTE_DELETE) {
        printf("Deleted\n");
      }
      if (e->fflags & NOTE_WRITE) {
        printf("Written to\n");
      }
      if (e->fflags & NOTE_RENAME) {
        printf("Renamed\n");
      }
      if (e->fflags & NOTE_EXTEND) {
        printf("Extended\n");
      }
      if (e->fflags & NOTE_REVOKE) {
        printf("Revoked\n");
      }
    }
  }
}
