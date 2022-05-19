#pragma once
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/event.h>
#include <sys/fcntl.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>

#include "./error.h"
#include "vendor/gb.h"

#define MAX_EVENT 1

static bool path_is_directory(char* path) {
  struct stat path_stat;
  stat(path, &path_stat);
  return S_ISDIR(path_stat.st_mode);
}

static error* fs_watch_file(gbAllocator allocator, gbString* path) {
  GB_ASSERT(path != NULL);

  const int fd = open(*path, O_RDONLY);
  if (fd == -1) {
    error* err = NULL;
    error_record(allocator, err, "Failed to open the file %s: %s\n", *path,
                 strerror(errno));
  }

  const int queue = kqueue();
  if (queue == -1) {
    fprintf(stderr, "Failed to create queue with kqueue(): %s\n",
            strerror(errno));
    return NULL;  // FIXME
  }

  int event_count = 0;
  struct kevent change_list[MAX_EVENT] = {};
  EV_SET(&change_list[0], fd, EVFILT_VNODE, EV_ADD | EV_CLEAR,
         NOTE_WRITE | NOTE_DELETE | NOTE_RENAME | NOTE_REVOKE, 0, 0);
  struct kevent event_list[MAX_EVENT] = {};

  printf("Watching %s\n", *path);
  while (1) {
    event_count =
        kevent(queue, change_list, MAX_EVENT, event_list, MAX_EVENT, NULL);

    if (event_count == -1) {
      fprintf(stderr, "Failed to get the events with kevent(): %s\n",
              strerror(errno));
      return NULL;  // FIXME
    }

    for (int i = 0; i < MAX_EVENT; i++) {
      struct kevent* e = &event_list[i];
      if ((e->flags & EVFILT_VNODE) && (e->fflags & NOTE_DELETE)) {
        printf("Deleted\n");
      }
      if ((e->flags & EVFILT_VNODE) && (e->fflags & NOTE_WRITE)) {
        printf("Written to. Data=%p\n", e->data);
      }
      if ((e->flags & EVFILT_VNODE) && (e->fflags & NOTE_RENAME)) {
        printf("Renamed\n");
      }
      if ((e->flags & EVFILT_VNODE) && (e->fflags & NOTE_EXTEND)) {
        printf("Extended\n");
      }
      if ((e->flags & EVFILT_VNODE) && (e->fflags & NOTE_REVOKE)) {
        printf("Revoked\n");
      }
    }
  }
  return NULL;
}
