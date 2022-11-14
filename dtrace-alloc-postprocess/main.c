#include <_types/_uint64_t.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/errno.h>
#include <unistd.h>

#include "../pg/pg.h"

typedef enum : uint8_t {
  EK_NONE,
  EK_MALLOC_ENTRY,
  EK_REALLOC_ENTRY,
  EK_CALLOC_ENTRY,
  EK_MALLOC_RETURN,
  EK_REALLOC_RETURN,
  EK_CALLOC_RETURN,
  EK_FREE_ENTRY,
} event_kind_t;

typedef struct {
  uint64_t fn_i;
  uint64_t offset;
} stacktrace_entry_t;

typedef pg_array_t(stacktrace_entry_t) stacktrace_t;
typedef struct {
  pg_array_t(event_kind_t) kinds;
  pg_array_t(stacktrace_t) stacktraces;
  pg_array_t(uint64_t) timestamps;
  pg_array_t(uint64_t) arg0s;
  pg_array_t(uint64_t) arg1s;
  pg_array_t(bool) is_entry;
} events_t;

void events_init(events_t* events) {
  const uint64_t cap = 10000;
  pg_array_init_reserve(events->kinds, cap, pg_heap_allocator());
  pg_array_init_reserve(events->stacktraces, cap, pg_heap_allocator());
  pg_array_init_reserve(events->timestamps, cap, pg_heap_allocator());
  pg_array_init_reserve(events->arg0s, cap, pg_heap_allocator());
  pg_array_init_reserve(events->arg1s, cap, pg_heap_allocator());
}

uint64_t fn_name_find(pg_array_t(pg_span_t) fn_names, pg_span_t name,
                      bool* found) {
  for (uint64_t i = 0; i < pg_array_len(fn_names); i++) {
    if (pg_span_eq(fn_names[i], name)) {
      *found = true;
      return i;
    }
  }
  return false;
}

const char* event_kind_to_string(event_kind_t kind) {
  switch (kind) {
    case EK_NONE:
      return "EK_NONE";
    case EK_MALLOC_ENTRY:
      return "EK_MALLOC_ENTRY";
    case EK_REALLOC_ENTRY:
      return "EK_REALLOC_ENTRY";
    case EK_CALLOC_ENTRY:
      return "EK_CALLOC_ENTRY";
    case EK_MALLOC_RETURN:
      return "EK_MALLOC_RETURN";
    case EK_REALLOC_RETURN:
      return "EK_REALLOC_RETURN";
    case EK_CALLOC_RETURN:
      return "EK_CALLOC_RETURN";
    case EK_FREE_ENTRY:
      return "EK_FREE_ENTRY";
    default:
      __builtin_unreachable();
  }
}

void event_dump(events_t* events, pg_array_t(pg_span_t) fn_names, uint64_t i) {
  printf("Event: kind=%s timestamp=%llu arg0=%llu arg1=%llu stacktrace=",
         event_kind_to_string(events->kinds[i]), events->timestamps[i],
         events->arg0s[i], events->arg1s[i]);
  for (uint64_t j = 0; j < pg_array_len(events->stacktraces[i]); j++) {
    const uint64_t fn_i = events->stacktraces[i][j].fn_i;
    const uint64_t offset = events->stacktraces[i][j].offset;
    const pg_span_t fn_name = fn_names[fn_i];
    printf("%.*s+%#llx ", (int)fn_name.len, fn_name.data, offset);
  }
  puts("");
}

// name is of the form:
// foo`bar+0xab
// foo`bar
stacktrace_entry_t fn_name_to_stacktrace_entry(pg_array_t(pg_span_t) * fn_names,
                                               pg_span_t name) {
  pg_span_t left = {0}, right = {0};
  uint64_t offset = 0;
  if (pg_span_split(name, '+', &left, &right)) {  // +0xab present
    bool valid = false;
    offset = pg_span_parse_u64_hex(right, &valid);
    assert(valid);  // TODO better error handling
  }

  bool found = false;
  uint64_t fn_i = fn_name_find(*fn_names, left, &found);
  if (!found) {
    pg_array_append(*fn_names, left);
    fn_i = pg_array_len(*fn_names) - 1;
  }

  return (stacktrace_entry_t){.fn_i = fn_i, .offset = offset};
}

int main(int argc, char* argv[]) {
  pg_logger_t logger = {.level = PG_LOG_INFO};
  pg_array_t(uint8_t) file_data = {0};
  if (argc == 1) {
    // int fd = STDIN_FILENO;
    pg_log_fatal(&logger, EINVAL, "TODO read from stdin");
  } else if (argc == 2) {
    int fd = open(argv[1], O_RDONLY);
    if (fd == -1) {
      pg_log_fatal(&logger, errno, "Failed to open file %s: %s", argv[1],
                   strerror(errno));
    }
    int64_t ret = 0;
    if ((ret = pg_read_file(pg_heap_allocator(), argv[1], &file_data)) != 0) {
      pg_log_fatal(&logger, ret, "Failed to read file %s: %s", argv[1],
                   strerror(ret));
    }
  }

  pg_span_t input = {.data = (char*)file_data, .len = pg_array_len(file_data)};

  events_t events = {0};
  events_init(&events);

  pg_array_t(pg_span_t) fn_names = {0};
  pg_array_init_reserve(fn_names, 500, pg_heap_allocator());

  // Skip header, unneeded
  if (pg_span_starts_with(input, pg_span_make_c("CPU")))
    pg_span_skip_left_until_inclusive(&input, '\n');

  const pg_span_t malloc_span = pg_span_make_c("malloc");
  const pg_span_t realloc_span = pg_span_make_c("realloc");
  const pg_span_t calloc_span = pg_span_make_c("calloc");
  const pg_span_t free_span = pg_span_make_c("free");
  const pg_span_t entry_span = pg_span_make_c(":entry");

  while (true) {
    event_kind_t kind = EK_NONE;
    stacktrace_t stacktrace = {0};
    pg_array_init_reserve(stacktrace, 10, pg_heap_allocator());

    pg_span_trim_left(&input);

    // Skip CPU id column
    {
      pg_span_trim_left(&input);
      pg_span_skip_left_until_inclusive(&input, ' ');
    }

    // Skip ID column
    {
      pg_span_trim_left(&input);
      pg_span_skip_left_until_inclusive(&input, ' ');
    }

    // malloc/realloc/calloc/free:entry
    pg_span_trim_left(&input);
    pg_span_t fn_leaf = input;
    pg_span_split(input, ' ', &fn_leaf, &input);
    pg_span_trim_left(&input);
    const bool is_entry = pg_span_ends_with(fn_leaf, entry_span);
    if (pg_span_contains(fn_leaf, malloc_span))
      kind = is_entry ? EK_MALLOC_ENTRY : EK_MALLOC_RETURN;
    else if (pg_span_contains(fn_leaf, realloc_span))
      kind = is_entry ? EK_REALLOC_ENTRY : EK_REALLOC_RETURN;
    else if (pg_span_contains(fn_leaf, calloc_span))
      kind = is_entry ? EK_CALLOC_ENTRY : EK_CALLOC_RETURN;
    else if (pg_span_contains(fn_leaf, free_span))
      kind = EK_FREE_ENTRY;
    else
      pg_log_fatal(&logger, EINVAL, "Unkown event kind: %.*s", (int)fn_leaf.len,
                   fn_leaf.data);

    // timestamp
    pg_span_t timestamp_span = {0};
    pg_span_split(input, ' ', &timestamp_span, &input);
    pg_span_trim_left(&input);
    bool timestamp_valid = false;
    const uint64_t timestamp =
        pg_span_parse_u64_decimal(timestamp_span, &timestamp_valid);
    if (!timestamp_valid)
      pg_log_fatal(&logger, EINVAL, "Invalid timestamp: %.*s",
                   (int)timestamp_span.len, timestamp_span.data);

    // arg0
    pg_span_t arg0_span = {0};
    pg_span_split(input, ' ', &arg0_span, &input);
    pg_span_trim_left(&input);
    bool arg0_valid = false;
    uint64_t arg0 = 0;
    if (!is_entry || kind == EK_REALLOC_ENTRY)
      arg0 = pg_span_parse_u64_hex(arg0_span, &arg0_valid);
    else
      arg0 = pg_span_parse_u64_decimal(arg0_span, &arg0_valid);
    if (!arg0_valid)
      pg_log_fatal(&logger, EINVAL, "Invalid arg0: %.*s", (int)arg0_span.len,
                   arg0_span.data);

    // arg1
    pg_span_t arg1_span = {0};
    uint64_t arg1 = 0;
    bool more_chars = false;
    char c = pg_span_peek_left(input, &more_chars);
    if (!more_chars) break;
    if (c != '\n') {
      pg_span_split(input, '\n', &arg1_span, &input);
      pg_span_trim_left(&input);
    } else {
      bool arg1_valid = false;
      if (arg1_span.len != 0) {
        if (kind == EK_REALLOC_ENTRY)
          arg1 = pg_span_parse_u64_hex(arg1_span, &arg1_valid);
        else if (kind == EK_CALLOC_ENTRY)
          arg1 = pg_span_parse_u64_decimal(arg1_span, &arg1_valid);
        else
          pg_log_fatal(&logger, EINVAL, "Unexpected arg1 for %s: %.*s",
                       event_kind_to_string(kind), (int)arg1_span.len,
                       arg1_span.data);
      }
      if (!arg1_valid)
        pg_log_fatal(&logger, EINVAL, "Invalid arg1: %.*s", (int)arg1_span.len,
                     arg1_span.data);
    }

    // Rest of stacktrace
    while (true) {
      bool more_chars = false;
      char c = pg_span_peek_left(input, &more_chars);
      if (!more_chars) break;
      if (pg_char_is_digit(c)) {  // New frame
        pg_array_append(events.kinds, kind);
        pg_array_append(events.stacktraces, stacktrace);
        pg_array_append(events.timestamps, timestamp);
        pg_array_append(events.arg0s, arg0);
        pg_array_append(events.arg1s, arg1);

        event_dump(&events, fn_names, pg_array_len(events.kinds) - 1);
        break;
      }

      pg_span_trim_left(&input);
      pg_span_t fn = input;
      pg_span_split(input, ' ', &fn, &input);
      pg_span_trim_left(&input);
      pg_span_trim_right(&fn);

      const stacktrace_entry_t stacktrace_entry =
          fn_name_to_stacktrace_entry(&fn_names, fn);
      pg_array_append(stacktrace, stacktrace_entry);
    }
  }
  return 0;
}