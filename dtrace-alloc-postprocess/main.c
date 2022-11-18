#include <_types/_uint64_t.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/_types/_int64_t.h>
#include <sys/errno.h>
#include <unistd.h>

#include "../pg/pg.h"

typedef enum : uint8_t {
  EK_NONE,
  EK_ALLOC,
  EK_REALLOC,
  EK_FREE,
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
  pg_array_t(uint64_t) arg2s;
  pg_array_t(bool) is_entry;
} events_t;

typedef struct {
  uint64_t ptr, size, start_i, end_i;
} lifetime_t;

static void events_init(events_t* events) {
  const uint64_t cap = 10000;
  pg_array_init_reserve(events->kinds, cap, pg_heap_allocator());
  pg_array_init_reserve(events->stacktraces, cap, pg_heap_allocator());
  pg_array_init_reserve(events->timestamps, cap, pg_heap_allocator());
  pg_array_init_reserve(events->arg0s, cap, pg_heap_allocator());
  pg_array_init_reserve(events->arg1s, cap, pg_heap_allocator());
  pg_array_init_reserve(events->arg2s, cap, pg_heap_allocator());
}

static uint64_t fn_name_find(pg_array_t(pg_span_t) fn_names, pg_span_t name,
                             bool* found) {
  for (uint64_t i = 0; i < pg_array_len(fn_names); i++) {
    if (pg_span_eq(fn_names[i], name)) {
      *found = true;
      return i;
    }
  }
  return false;
}

static const char* event_kind_to_string(event_kind_t kind) {
  switch (kind) {
    case EK_NONE:
      return "EK_NONE";
    case EK_ALLOC:
      return "EK_ALLOC";
    case EK_REALLOC:
      return "EK_REALLOC";
    case EK_FREE:
      return "EK_FREE";
    default:
      __builtin_unreachable();
  }
}

static void event_dump(events_t* events, pg_array_t(pg_span_t) fn_names,
                       uint64_t i) {
  printf(
      "Event: kind=%s timestamp=%llu arg0=%llu arg1=%llu arg2=%llu stacktrace=",
      event_kind_to_string(events->kinds[i]), events->timestamps[i],
      events->arg0s[i], events->arg1s[i], events->arg2s[i]);
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
// foo`+[objc_weirdness]+0xab
static stacktrace_entry_t fn_name_to_stacktrace_entry(
    pg_logger_t* logger, pg_array_t(pg_span_t) * fn_names, pg_span_t name) {
  pg_span_t left = {0}, right = {0};
  uint64_t offset = 0;
  if (pg_span_split_at_last(name, '+', &left,
                            &right) &&
      right.len > 0) {  // +0xab present at the end
    bool valid = false;
    offset = pg_span_parse_u64_hex(right, &valid);
    if (!valid)
      pg_log_fatal(logger, EINVAL, "Invalid offset: name=%.*s offset=%.*s",
                   (int)name.len, name.data, (int)right.len, right.data);
  }

  bool found = false;
  uint64_t fn_i = fn_name_find(*fn_names, left, &found);
  if (!found) {
    pg_array_append(*fn_names, left);
    fn_i = pg_array_len(*fn_names) - 1;
  }

  return (stacktrace_entry_t){.fn_i = fn_i, .offset = offset};
}

static void parse_input(pg_logger_t* logger, pg_span_t input, events_t* events,
                        pg_array_t(pg_span_t) * fn_names) {
  // Skip header, unneeded
  if (pg_span_starts_with(input, pg_span_make_c("CPU")))
    pg_span_skip_left_until_inclusive(&input, '\n');

  const pg_span_t malloc_span = pg_span_make_c("malloc");
  const pg_span_t realloc_span = pg_span_make_c("realloc");
  const pg_span_t calloc_span = pg_span_make_c("calloc");
  const pg_span_t free_span = pg_span_make_c("free");

  while (input.len > 0) {
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
    pg_span_split_at_first(input, ' ', &fn_leaf, &input);
    pg_span_trim_left(&input);
    if (pg_span_contains(fn_leaf, malloc_span) ||
        pg_span_contains(fn_leaf, calloc_span))
      kind = EK_ALLOC;
    else if (pg_span_contains(fn_leaf, realloc_span))
      kind = EK_REALLOC;
    else if (pg_span_contains(fn_leaf, free_span))
      kind = EK_FREE;
    else
      pg_log_fatal(logger, EINVAL, "Unkown event kind: %.*s", (int)fn_leaf.len,
                   fn_leaf.data);

    // timestamp
    pg_span_t timestamp_span = {0};
    pg_span_split_at_first(input, ' ', &timestamp_span, &input);
    pg_span_trim_left(&input);
    bool timestamp_valid = false;
    const uint64_t timestamp =
        pg_span_parse_u64_decimal(timestamp_span, &timestamp_valid);
    if (!timestamp_valid)
      pg_log_fatal(logger, EINVAL, "Invalid timestamp: %.*s",
                   (int)timestamp_span.len, timestamp_span.data);

    // arg0
    pg_span_t arg0_span = {0};
    pg_span_split_at_first(input, ' ', &arg0_span, &input);
    pg_span_trim_left(&input);
    bool arg0_valid = false;
    const uint64_t arg0 =
        kind == EK_FREE ? pg_span_parse_u64_hex(arg0_span, &arg0_valid)
                        : pg_span_parse_u64_decimal(arg0_span, &arg0_valid);
    if (!arg0_valid)
      pg_log_fatal(logger, EINVAL, "Invalid arg0: %.*s", (int)arg0_span.len,
                   arg0_span.data);

    // arg1
    pg_span_t arg1_span = {0};
    uint64_t arg1 = 0;
    if (kind != EK_FREE) {
      pg_span_split_at_first(input, ' ', &arg1_span, &input);
      pg_span_trim_left(&input);
      if (arg1_span.len != 0) {
        bool arg1_valid = false;
        arg1 = pg_span_parse_u64_hex(arg1_span, &arg1_valid);
        if (!arg1_valid)
          pg_log_fatal(logger, EINVAL, "Invalid arg1: %.*s", (int)arg1_span.len,
                       arg1_span.data);
      }
    }

    // arg2
    pg_span_t arg2_span = {0};
    uint64_t arg2 = 0;
    if (kind != EK_FREE) {
      pg_span_split_at_first(input, '\n', &arg2_span, &input);
      pg_span_trim_left(&input);
      if (arg2_span.len != 0) {
        bool arg2_valid = false;
        arg2 = pg_span_parse_u64_hex(arg1_span, &arg2_valid);
        if (!arg2_valid)
          pg_log_fatal(logger, EINVAL, "Invalid arg2: %.*s", (int)arg2_span.len,
                       arg2_span.data);
      }
    }

    // Rest of stacktrace
    while (true) {
      bool more_chars = false;
      char c = pg_span_peek_left(input, &more_chars);
      if (!more_chars || pg_char_is_digit(c)) {  // The End / New frame
        pg_array_append(events->kinds, kind);
        pg_array_append(events->stacktraces, stacktrace);
        pg_array_append(events->timestamps, timestamp);
        pg_array_append(events->arg0s, arg0);
        pg_array_append(events->arg1s, arg1);
        pg_array_append(events->arg2s, arg2);

        // event_dump(events, *fn_names, pg_array_len(events->kinds) - 1);
        break;
      }

      pg_span_trim_left(&input);
      pg_span_t fn = input;
      pg_span_split_at_first(input, '\n', &fn, &input);
      pg_span_trim_left(&input);
      pg_span_trim(&fn);

      const stacktrace_entry_t stacktrace_entry =
          fn_name_to_stacktrace_entry(logger, fn_names, fn);
      pg_array_append(stacktrace, stacktrace_entry);
    }
  }
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

  parse_input(&logger, input, &events, &fn_names);

  pg_array_t(lifetime_t) lifetimes = {0};
  pg_array_init_reserve(lifetimes, pg_array_len(events.kinds),
                        pg_heap_allocator());
  for (uint64_t i = 0; i < pg_array_len(events.kinds); i++) {
    const event_kind_t kind = events.kinds[i];
    if (kind == EK_ALLOC) {
      pg_array_append(lifetimes, ((lifetime_t){.ptr = events.arg1s[i],
                                               .start_i = i,
                                               .size = events.arg0s[i]}));
    } else if (kind == EK_FREE) {
      const uint64_t ptr = events.arg0s[i];
      for (int64_t j = pg_array_len(lifetimes) - 1; j >= 0; j--) {
        if (lifetimes[j].ptr == ptr && lifetimes[j].end_i == 0) {
          lifetimes[j].end_i = i;
          break;
        }
      }
    } else if (kind == EK_REALLOC) {
      const uint64_t old_ptr = events.arg2s[i];
      if (old_ptr == 0) {  // Same as malloc
        pg_array_append(lifetimes, ((lifetime_t){.ptr = events.arg1s[i],
                                                 .start_i = i,
                                                 .size = events.arg0s[i]}));
      } else {  // Same as free + malloc
        for (int64_t j = pg_array_len(lifetimes) - 1; j >= 0; j--) {
          if (lifetimes[j].ptr == old_ptr && lifetimes[j].end_i == 0) {
            lifetimes[j].end_i = i;
            break;
          }
        }
        pg_array_append(lifetimes, ((lifetime_t){.ptr = events.arg1s[i],
                                                 .start_i = i,
                                                 .size = events.arg0s[i]}));
      }
    } else
      __builtin_unreachable();
  }

  // Output html
  printf(
      // clang-format off
"<!DOCTYPE html>"
"  <html>"
"    <head>"
"      <meta charset=\"UTF-8\" />"
"      <style>"
"      </style>"
"    </head>"
"    <body>"
"       <div style=\"width: 1600px;\"><canvas id=\"chart\"></canvas></div>"
"   </body>"
"   <script src=\"chart.umd.min.js\"></script>"
"   <script>"
"     var allocations = ["
      // clang-format on
  );

  const uint64_t START = 0;
  const uint64_t COUNT = 1700;

  for (uint64_t i = START; i < MIN(COUNT, pg_array_len(events.arg0s)); i++) {
    if (events.kinds[i] == EK_FREE) continue;
    printf("{x:%llu, y:%llu},", events.timestamps[i], events.arg0s[i]);
  }

  printf(
      "];\n"
      "var allocation_stacktraces=[");
  for (uint64_t i = START; i < MIN(COUNT, pg_array_len(events.stacktraces));
       i++) {
    if (events.kinds[i] == EK_FREE) continue;
    printf("['mem: %llu',", events.arg0s[i]);

    const stacktrace_t st = events.stacktraces[i];
    for (uint64_t j = 0; j < pg_array_len(st); j++) {
      const uint64_t fn_i = st[j].fn_i;
      const pg_span_t fn_name = fn_names[fn_i];
      printf("'%.*s',", (int)fn_name.len, fn_name.data);
    }
    printf("],");
  }

  printf(
      "];\n"
      "var frees=[");
  for (uint64_t i = START; i < MIN(COUNT, pg_array_len(events.stacktraces));
       i++) {
    if (events.kinds[i] != EK_FREE) continue;

    const uint64_t ptr = events.arg0s[i];

    bool found = false;
    for (int64_t j = i - 1; j >= 0; j--) {
      if (events.kinds[j] != EK_FREE && events.arg1s[j] == ptr) {
        printf("{x:%llu,y:%llu},", events.timestamps[i], events.arg0s[j]);
        found = true;
        assert(events.arg1s[i] == 0);
        events.arg1s[i] = events.arg0s[j];
        break;
      }
    }
    if (!found) printf("{x:%llu,y:0},", events.timestamps[i]);
  }
  printf(
      "];\n"
      "var free_stacktraces=[");
  for (uint64_t i = START; i < MIN(COUNT, pg_array_len(events.stacktraces));
       i++) {
    if (events.kinds[i] != EK_FREE) continue;
    printf("['mem: %llu',", events.arg1s[i]);

    const stacktrace_t st = events.stacktraces[i];
    for (uint64_t j = 0; j < pg_array_len(st); j++) {
      const uint64_t fn_i = st[j].fn_i;
      const pg_span_t fn_name = fn_names[fn_i];
      printf("'%.*s',", (int)fn_name.len, fn_name.data);
    }
    printf("],");
  }

  printf(
      "];\n"
      "var lifetimes=[");
  // FIXME
  for (uint64_t i = START; i < MIN(COUNT, pg_array_len(lifetimes)); i++) {
    const lifetime_t l = lifetimes[i];
    if (l.end_i == 0) continue;
    printf("{x:%llu,y:%llu},", events.timestamps[l.start_i], l.size);
    printf("{x:%llu,y:%llu},", events.timestamps[l.end_i], l.size);
    break;
  }

  printf(
      // clang-format off
      "];\n"
"      const chart = new Chart(document.getElementById('chart'), {"
"        options: {"
"           animation: false,"
"          plugins: {"
"            tooltip: {"
"              callbacks: {"
"                 label: function(ctx) {"
"                    if(ctx.datasetIndex==0) return allocation_stacktraces[ctx.dataIndex];"
"                    else if (ctx.dataIndex == 1) return free_stacktraces[ctx.dataIndex];"
"                    else {"
"                      return /* FIXME*/ '';"
"                    }"
"                 },"
"              }"
"            },"
//"            decimation: {"
//"              algorithm: 'ttb',"
//"              enabled: true,"
//"            }, "
"          },"
"          scales: {"
"            x: {"
"              display: true,"
"            },"
"            y: {"
"              display: true,"
"              type: 'logarithmic',"
"            },"
"          },"
"        },"
"        data: {"
"          datasets: ["
"            {type: 'scatter', label: 'Allocations', data: allocations},"
"            {type: 'scatter', label: 'Frees', data: frees},"
"            {type: 'line', label: 'Lifetimes', data: lifetimes},"
"          ],"
"        },"
"      });"
"   </script>"
"</html>"
      // clang-format on
  );
  return 0;
}
