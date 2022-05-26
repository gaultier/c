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
#include "vendor/gb.h"

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

static char* extensions_to_watch[] = {"kt", "kts"};
static const isize extensions_to_watch_count =
    sizeof(extensions_to_watch) / sizeof(extensions_to_watch[0]);

static void path_directory_walk(gbAllocator allocator, gbString path,
                                gbArray(file_info) * files) {
  GB_ASSERT_NOT_NULL(path);
  GB_ASSERT_NOT_NULL(files);

  const mode_t mode = path_get_mode(path);
  if (S_ISREG(mode)) {
    const char* ext = gb_path_extension(path);
    if (pg_string_array_contains(extensions_to_watch, extensions_to_watch_count,
                                 ext)) {
      gb_array_append(*files,
                      ((file_info){.absolute_path = path, .kind = FK_FILE}));
    }
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
    const int fd = open(path, O_EVTONLY);
    if (fd == -1) {
      fprintf(stderr, "%s:%d:Failed to open the file %s: %s\n", __FILE__,
              __LINE__, path, strerror(errno));
      return;
    }

    gb_array_append(fds, fd);
    printf("[%d] (%s) Watching %s\n", i, file_kind_str[files[i].kind],
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
    event.udata = i;
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
    fprintf(stderr, "[D010] event_count=%d\n", event_count);

    for (int i = 0; i < event_count; i++) {
      struct kevent* e = &event_list[i];
      if (e->flags & EV_ERROR) {
        // TODO: inspect e->data
        fprintf(stderr, "Event failed: %s\n", strerror(errno));
        return;
      }

      // Skip unrelated events (should not ever happen?)
      if ((e->flags & EVFILT_VNODE) == 0) continue;

      i64 f_i = (i64)e->udata;
      GB_ASSERT(f_i >= 0);
      GB_ASSERT(f_i < gb_array_count(files));
      file_info* f = &files[f_i];

      fprintf(stderr,
              "[D011] [%d] data=%ld udata=%lld filter=%d fflags=%d f_i=%lld "
              "absolute_path=%s\n",
              i, e->data, (i64)e->udata, e->filter, e->fflags, f_i,
              f->absolute_path);
      if (e->fflags & NOTE_DELETE) {
        printf("(%s) %s Deleted\n", file_kind_str[f->kind], f->absolute_path);
        close(fds[i]);
        gb_free(allocator, f->absolute_path);
        pg_array_swap_remove_at_index(files, sizeof(file_info),
                                      &gb_array_count(files), i);
        pg_array_swap_remove_at_index(fds, sizeof(int), &gb_array_count(fds),
                                      i);
        pg_array_swap_remove_at_index(change_list, sizeof(struct kevent),
                                      &gb_array_count(change_list), i);
      }
      if (e->fflags & NOTE_WRITE) {
        printf("(%s) %s Written to\n", file_kind_str[f->kind],
               f->absolute_path);
        // TODO: if directory and new files created in it then add then to the
        // watch list
      }

      if (e->fflags & NOTE_RENAME) {
        printf("(%s) %s Renamed to:", file_kind_str[f->kind], f->absolute_path);
        if (fcntl(fds[f_i], F_GETPATH, f->absolute_path) == -1) {
          fprintf(
              stderr,
              "Failed to get new file name from fd for renamed file: err=%s\n",
              strerror(errno));
        }
        printf("%s\n", f->absolute_path);
      }

      if (e->fflags & NOTE_EXTEND) {
        printf("(%s) %s Extended\n", file_kind_str[f->kind], f->absolute_path);
      }

      if (e->fflags & NOTE_REVOKE) {
        printf("(%s) %s Revoked\n", file_kind_str[f->kind], f->absolute_path);
        // TODO: rm
      }
    }
  }
}
