#pragma once
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/event.h>
#include <sys/fcntl.h>
#include <sys/stat.h>
#include <sys/syslimits.h>
#include <sys/time.h>
#include <sys/types.h>

#include "./error.h"
#include "vendor/gb.h"

#define MAX_EVENT 1

static mode_t path_get_mode(char* path) {
  struct stat path_stat;
  stat(path, &path_stat);
  return path_stat.st_mode;
}

static bool path_is_directory(char* path) {
  return S_ISDIR(path_get_mode(path));
}

typedef void (*dir_walk_fn)(gbString, usize, void*);

static void path_directory_walk(gbString path, dir_walk_fn fn, void* arg) {
  GB_ASSERT_NOT_NULL(arg);
  gbAllocator* allocator = arg;

  const mode_t mode = path_get_mode(path);
  if (S_ISREG(mode)) {
    fn(path, /* unused */ 0, allocator);
    return;
  }

  if (!S_ISDIR(mode)) {
    return;
  }

  DIR* dirp = opendir(path);
  if (dirp == NULL) {
    fprintf(stderr, "%s:%d:Could not open `%s`: %s. Skipping.\n", __FILE__,
            __LINE__, path, strerror(errno));
    return;
  }

  struct dirent* entry;

  while ((entry = readdir(dirp)) != NULL) {
    // Skip the special `.` and `..`
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
      continue;

    printf("dir walk: seen file %s\n", entry->d_name);

    gbString absolute_path_file =
        gb_string_append_rune(path, GB_PATH_SEPARATOR);
    absolute_path_file = gb_string_appendc(absolute_path_file, entry->d_name);
    path_directory_walk(absolute_path_file, fn, arg);
  }

  closedir(dirp);
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
        printf("Written to\n");
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
