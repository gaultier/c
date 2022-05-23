#pragma once
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/event.h>
#include <sys/fcntl.h>
#include <sys/stat.h>
#include <sys/syslimits.h>
#include <sys/time.h>
#include <sys/types.h>

#include "./util.h"

static mode_t path_get_mode(char* path) {
  struct stat path_stat;
  stat(path, &path_stat);
  return path_stat.st_mode;
}

static bool path_is_directory(char* path) {
  return S_ISDIR(path_get_mode(path));
}

typedef enum {
  FK_FILE = 0,
  FK_DIR = 1,
} file_kind;

static const char* file_kind_str[2] = {
    [FK_FILE] = "file",
    [FK_DIR] = "directory",
};

typedef struct {
  gbString absolute_path;
  file_kind kind;
} file_info;

typedef void (*dir_walk_fn)(gbString, usize, void*);

static void path_directory_walk(gbAllocator allocator, gbString path,
                                gbArray(file_info) * files) {
  GB_ASSERT_NOT_NULL(path);
  GB_ASSERT_NOT_NULL(files);

  const mode_t mode = path_get_mode(path);
  if (S_ISREG(mode)) {
    gb_array_append(*files,
                    ((file_info){.absolute_path = path, .kind = FK_FILE}));
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
  gb_array_append(*files, ((file_info){.absolute_path = path, .kind = FK_DIR}));

  struct dirent* entry;

  while ((entry = readdir(dirp)) != NULL) {
    // Skip the special `.` and `..`
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
      continue;

    gbString absolute_path_file =
        gb_string_append_rune(path, GB_PATH_SEPARATOR);
    absolute_path_file = gb_string_appendc(absolute_path_file, entry->d_name);
    path_directory_walk(allocator, absolute_path_file, files);
  }

  closedir(dirp);
}

static void fs_watch_file(gbAllocator allocator, gbArray(file_info) files) {
  GB_ASSERT(files != NULL);

  gbArray(int) fds = NULL;
  gb_array_init_reserve(fds, allocator, gb_array_count(files));
  for (int i = 0; i < gb_array_count(files); i++) {
    gbString path = files[i].absolute_path;
    const int fd = open(path, O_RDONLY);
    if (fd == -1) {
      fprintf(stderr, "%s:%d:Failed to open the file %s: %s\n", __FILE__,
              __LINE__, path, strerror(errno));
    }

    gb_array_append(fds, fd);
    printf("(%s) Watching %s\n", file_kind_str[files[i].kind],
           files[i].absolute_path);
  }

  const int queue = kqueue();
  if (queue == -1) {
    fprintf(stderr, "%s:%d:Failed to create queue with kqueue(): %s\n",
            __FILE__, __LINE__, strerror(errno));
    return;
  }

  int event_count = 0;
  gbArray(struct kevent) change_list = NULL;
  gb_array_init_reserve(change_list, allocator, gb_array_count(fds));
  for (int i = 0; i < gb_array_count(fds); i++) {
    struct kevent event = {};
    EV_SET(&event, fds[i], EVFILT_VNODE, EV_ADD | EV_CLEAR,
           NOTE_WRITE | NOTE_DELETE | NOTE_RENAME | NOTE_REVOKE, 0, 0);
    gb_array_append(change_list, event);
  }

  gbArray(struct kevent) event_list = NULL;
  gb_array_init_reserve(event_list, allocator, gb_array_count(change_list));

  while (1) {
    event_count = kevent(queue, change_list, gb_array_count(change_list),
                         event_list, gb_array_capacity(event_list), NULL);

    if (event_count == -1) {
      fprintf(stderr, "%s:%d:Failed to get the events with kevent(): %s\n",
              __FILE__, __LINE__, strerror(errno));
      return;
    }

    for (int i = 0; i < gb_array_count(change_list); i++) {
      struct kevent* e = &event_list[i];
      if ((e->flags & EVFILT_VNODE) && (e->fflags & NOTE_DELETE)) {
        printf("(%s) %s Deleted\n", file_kind_str[files[i].kind],
               files[i].absolute_path);
      }
      if ((e->flags & EVFILT_VNODE) && (e->fflags & NOTE_WRITE)) {
        printf("(%s) %s Written to\n", file_kind_str[files[i].kind],
               files[i].absolute_path);
      }

      if ((e->flags & EVFILT_VNODE) && (e->fflags & NOTE_RENAME)) {
        printf("(%s) %s Renamed\n", file_kind_str[files[i].kind],
               files[i].absolute_path);
      }

      if ((e->flags & EVFILT_VNODE) && (e->fflags & NOTE_EXTEND)) {
        printf("(%s) %s Extended\n", file_kind_str[files[i].kind],
               files[i].absolute_path);
      }

      if ((e->flags & EVFILT_VNODE) && (e->fflags & NOTE_REVOKE)) {
        printf("(%s) %s Revoked\n", file_kind_str[files[i].kind],
               files[i].absolute_path);
      }
    }
  }
}
