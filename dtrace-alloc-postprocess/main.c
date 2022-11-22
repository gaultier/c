#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
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

typedef struct {
  uint64_t ptr;
} event_alloc_t;

typedef struct {
  uint64_t new_ptr, old_ptr;
} event_realloc_t;

typedef struct event_t event_t;
struct event_t {
  event_kind_t kind;
  PG_PAD(7);
  uint64_t timestamp, size;
  event_t* related_event;
  pg_array_t(stacktrace_entry_t) stacktrace;
  union {
    event_alloc_t alloc;
    event_realloc_t realloc;
  } v;
};

typedef struct {
  uint64_t ptr, size, start_i, end_i;
} lifetime_t;

static char* power_of_two_string(uint64_t n) {
  static char res[50];
  if (n < 1024) {
    snprintf(res, sizeof(res) - 1, "%llu", n);
    return res;
  } else if (n < 1024ULL * 1024) {
    snprintf(res, sizeof(res) - 1, "%llu Ki", n / 1024ULL);
    return res;
  } else if (n < 1024ULL * 1024 * 1024) {
    snprintf(res, sizeof(res) - 1, "%llu Mi", n / 1024ULL / 1024);
    return res;
  } else if (n < 1024ULL * 1024 * 1024) {
    snprintf(res, sizeof(res) - 1, "%llu Gi", n / 1024ULL / 1024);
    return res;
  } else if (n < 1024ULL * 1024 * 1024 * 1024) {
    snprintf(res, sizeof(res) - 1, "%llu Ti", n / 1024ULL / 1024 / 1024);
    return res;
  } else {
    snprintf(res, sizeof(res) - 1, "%llu", n);
    return res;
  }
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

// static void event_dump(events_t* events, pg_array_t(pg_span_t) fn_names,
//                        uint64_t i) {
//   printf(
//       "Event: kind=%s timestamp=%llu arg0=%llu arg1=%llu arg2=%llu
//       stacktrace=", event_kind_to_string(events->kinds[i]),
//       events->timestamps[i], events->arg0s[i], events->arg1s[i],
//       events->arg2s[i]);
//   for (uint64_t j = 0; j < pg_array_len(events->stacktraces[i]); j++) {
//     const uint64_t fn_i = events->stacktraces[i][j].fn_i;
//     const uint64_t offset = events->stacktraces[i][j].offset;
//     const pg_span_t fn_name = fn_names[fn_i];
//     printf("%.*s+%#llx ", (int)fn_name.len, fn_name.data, offset);
//   }
//   puts("");
// }

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

static void parse_input(pg_logger_t* logger, pg_span_t input,
                        pg_array_t(event_t) * events,
                        pg_array_t(pg_span_t) * fn_names) {
  // Skip header, unneeded
  if (pg_span_starts_with(input, pg_span_make_c("CPU")))
    pg_span_skip_left_until_inclusive(&input, '\n');

  const pg_span_t malloc_span = pg_span_make_c("malloc");
  const pg_span_t realloc_span = pg_span_make_c("realloc");
  const pg_span_t calloc_span = pg_span_make_c("calloc");
  const pg_span_t free_span = pg_span_make_c("free");

  while (input.len > 0) {
    event_t event = {.kind = EK_NONE};
    pg_array_init_reserve(event.stacktrace, 20, pg_heap_allocator());

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
      event.kind = EK_ALLOC;
    else if (pg_span_contains(fn_leaf, realloc_span))
      event.kind = EK_REALLOC;
    else if (pg_span_contains(fn_leaf, free_span))
      event.kind = EK_FREE;
    else
      pg_log_fatal(logger, EINVAL, "Unkown event kind: %.*s", (int)fn_leaf.len,
                   fn_leaf.data);

    // timestamp
    pg_span_t timestamp_span = {0};
    pg_span_split_at_first(input, ' ', &timestamp_span, &input);
    pg_span_trim_left(&input);
    bool timestamp_valid = false;
    event.timestamp =
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
        event.kind == EK_FREE
            ? pg_span_parse_u64_hex(arg0_span, &arg0_valid)
            : pg_span_parse_u64_decimal(arg0_span, &arg0_valid);
    if (!arg0_valid)
      pg_log_fatal(logger, EINVAL, "Invalid arg0: %.*s", (int)arg0_span.len,
                   arg0_span.data);

    // arg1
    pg_span_t arg1_span = {0};
    uint64_t arg1 = 0;
    pg_span_split_at_first(input, ' ', &arg1_span, &input);
    pg_span_trim_left(&input);
    if (event.kind != EK_FREE) {
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
    if (event.kind != EK_FREE) {
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
        if (event.kind == EK_ALLOC) {
          event.size = arg0;
          event.v.alloc.ptr = arg1;
          pg_array_append(*events, event);
        } else if (event.kind == EK_REALLOC) {
          event.size = arg0;
          event.v.realloc.new_ptr = arg1;
          event.v.realloc.old_ptr = arg2;
          pg_array_append(*events, event);
        } else if (event.kind == EK_FREE) {
          const uint64_t ptr = arg0;
          if (ptr == 0)
            pg_log_fatal(logger, EINVAL, "Invalid arg0 in free: arg0=%llu",
                         arg0);

          pg_array_append(*events, event);
          event_t* const me = &((*events)[pg_array_len(*events) - 1]);

          for (int64_t j = pg_array_len(*events) - 2; j >= 0; j--) {
            event_t* const other = &((*events)[j]);

            if (other->kind == EK_FREE) continue;
            if (other->kind == EK_ALLOC && other->v.alloc.ptr == ptr) {
              me->related_event = other;
              me->size = other->size;
              other->related_event = me;
              break;
            } else if (other->kind == EK_REALLOC &&
                       other->v.realloc.new_ptr == ptr) {
              me->related_event = other;
              me->size = other->size;
              other->related_event = me;
              break;
            }
          }
        } else {
          __builtin_unreachable();
        }

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
      pg_array_append(event.stacktrace, stacktrace_entry);
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

  pg_array_t(event_t) events = {0};
  pg_array_init_reserve(events, 20000, pg_heap_allocator());

  pg_array_t(pg_span_t) fn_names = {0};
  pg_array_init_reserve(fn_names, 500, pg_heap_allocator());

  parse_input(&logger, input, &events, &fn_names);

#if 0
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
#endif

  // Output html

  // const uint64_t rect_w = 5;
  // const uint64_t rect_h = 7;
  // const uint64_t rect_margin_top = 1;
  // const uint64_t rect_margin_right = 3;
  const uint64_t monitoring_start = (double)events[0].timestamp;
  const uint64_t monitoring_end =
      (double)events[pg_array_len(events) - 1].timestamp;
  const uint64_t monitoring_duration = monitoring_end - monitoring_start;

  const uint64_t chart_w = 1600;
  const uint64_t chart_h = 800;
  const uint64_t chart_margin_left = 60;
  const uint64_t chart_margin_bottom = 20;
  //  const uint64_t chart_padding_w = 10;
  const uint64_t chart_padding_top = 10;
  const uint64_t chart_grid_gap = 100;
  const uint64_t font_size = 10;

  double max_log_size = 0;
  //  double min_arg0 = 0;
  for (uint64_t i = 0; i < pg_array_len(events); i++) {
    const event_t event = events[i];
    max_log_size = MAX(max_log_size, log((double)event.size));
  }

  printf(
      // clang-format off
"<!DOCTYPE html>"
"  <html>"
"    <head>"
"      <meta charset=\"UTF-8\" />"
"      <style>"
"      </style>"
"    </head>"
"    <body style=\"min-height:1200px;\">"
"        <div id=\"tooltip\" style=\"background-color: rgb(40, 40, 40); opacity:1; color:white; border-radius:8px; display:none; position: absolute;\"></div>"
"        <svg style=\"margin: 10px\" width=\"%llu\" height=\"%llu\" font-family=\"sans-serif\" font-size=\"%llu\" text-anchor=\"end\">"
"           <g><text x=\"%llu\" y=\"%llu\" font-weight=\"bold\">Time</text></g>"
"           <g><line x1=\"%llu\" y1=\"%llu\" x2=\"%llu\" y2=\"%llu\" stroke=\"black\" stroke-width=\"3\"></line></g>"
"           <g><text x=\"%llu\" y=\"%llu\" font-weight=\"bold\">Allocations</text></g>"
"           <g><line x1=\"%llu\" y1=\"%llu\" x2=\"%llu\" y2=\"%llu\" stroke=\"black\" stroke-width=\"3\"></line></g>"
      ,
      chart_margin_left + chart_w, chart_margin_bottom + chart_h, font_size, // svg
       chart_w/2, chart_margin_bottom + chart_h, // x-axis text
      chart_margin_left, 0ULL, chart_margin_left, chart_padding_top+chart_h, // y-axis
      55ULL, chart_h/2-20ULL, // y-axis text
      chart_margin_left, chart_padding_top+chart_h, chart_w, chart_padding_top+chart_h // x-axis
      );
  // clang-format on

  // horizontal lines for grid
  for (uint64_t i = 2;; i *= 2) {
    const double val = log((double)i);
    const double py = val / max_log_size;
    const uint64_t y =
        chart_padding_top +
        (chart_padding_top + chart_h - /* text height */ 3) * (1.0 - py);
    if (py > 1.0) {
      printf(
          "<g><text x=\"%llu\" y=\"%llu\">%s</text></g>"
          "<g><line x1=\"%llu\" y1=\"%llu\" x2=\"%llu\" y2=\"%llu\" "
          "stroke=\"darkgrey\" stroke-width=\"1\"></line></g>",
          50ULL, font_size, power_of_two_string(i), chart_margin_left, 0ULL,
          chart_w, 0ULL);
      break;
    }

    printf(
        "<g><text x=\"%llu\" y=\"%llu\">%s</text></g>"
        "<g><line x1=\"%llu\" y1=\"%llu\" x2=\"%llu\" y2=\"%llu\" "
        "stroke=\"darkgrey\" stroke-width=\"1\"></line></g>",
        50ULL, y, power_of_two_string(i), chart_margin_left, y, chart_w, y);
  }

  const uint64_t circle_r = 3ULL;
  for (uint64_t i = 0; i < pg_array_len(events); i++) {
    const event_t event = events[i];
    // Skip free events for which we did not record/find the related allocation
    // event since we have no useful information in that case
    if (event.kind == EK_FREE && event.related_event == NULL) continue;

    const double px =
        ((double)(event.timestamp - monitoring_start)) / monitoring_duration;
    assert(px <= 1.0);

    const uint64_t x = chart_margin_left + px * (chart_w - chart_margin_left);
    assert(x <= chart_w + chart_margin_left);

    const double py =
        event.size == 0 ? 0 : (log((double)event.size) / max_log_size);
    assert(py <= 1.0);
    const uint64_t y = chart_padding_top + (chart_h - circle_r) * (1.0 - py);
    assert(y <= chart_padding_top + (chart_h - circle_r));

    const int64_t related_event_i =
        event.related_event == NULL ? -1 : (event.related_event - events);
    printf(
        "<g class=\"datapoint\"><circle fill=\"%s\" cx=\"%llu\" cy=\"%llu\" "
        "r=\"%llu\" data-kind=\"%s\" data-id=\"%llu\" "
        "data-refid=\"%lld\" "
        "data-value=\"%llu\" data-timestamp=\"%llu\" data-stacktrace=\"",
        event.kind == EK_FREE ? "goldenrod" : "steelblue", x, y, circle_r,
        event.kind == EK_FREE ? "free" : "alloc", i, related_event_i,
        event.size, event.timestamp);

    for (uint64_t j = 0; j < pg_array_len(event.stacktrace); j++) {
      const uint64_t fn_i = event.stacktrace[j].fn_i;
      const pg_span_t fn_name = fn_names[fn_i];
      printf("%.*s%c", (int)fn_name.len, fn_name.data, 0x0a);
    }
    printf("\"></circle></g>\n");
  }

  printf(
      // clang-format off
"     </svg>"
"   </body>"
"   <script>"
      // clang-format on
  );

  printf(
      // clang-format off
"      var tooltip = document.getElementById('tooltip');\n"
"      var mouse_x= 0;\n"
"      var mouse_y=0;\n"
"      document.onmousemove = function(e){\n"
"        mouse_x = event.clientX + document.body.scrollLeft;\n"
"        mouse_y = event.clientY + document.body.scrollTop;\n"
"      };\n"
"      var circles = document.querySelectorAll('g.datapoint > circle')\n"
"      circles.forEach(function(e, i){\n"
"        e.addEventListener('mouseover', function() {\n"
"           var value = e.getAttribute('data-value');\n"
"           var stacktrace = e.getAttribute('data-stacktrace');\n"
"           tooltip.innerText = value + '\\n\\n' + stacktrace; \n"
"           tooltip.style.display = '';\n"
"           tooltip.style.padding = '5px';\n"
"           tooltip.style.left = 5 + mouse_x + 'px';\n"
"           tooltip.style.top = 5 + mouse_y + 'px';\n"

"           e.setAttribute('fill-opacity', '60%%');\n"
"           var refId = parseInt(e.getAttribute('data-refid'));\n"
"           if (refId>=0) {\n"
"             var related = document.querySelector('circle[data-id=\"' + refId + '\"]');\n"
"             var relatedStacktrace = related.getAttribute('data-stacktrace');\n"
"             e.setAttribute('r', 12);\n"
"             related.setAttribute('r', 12);\n" 
"             related.setAttribute('fill-opacity', '60%%');\n"

"             tooltip.innerText += '\\n\\nAllocated:\\n' + relatedStacktrace; \n"
"             var allocTimestamp = parseInt(related.getAttribute('data-timestamp'));\n"
"             var freeTimestamp = parseInt(e.getAttribute('data-timestamp'));\n"
"             var durationNs = (freeTimestamp-allocTimestamp);\n"
"             tooltip.innerText += '\\n\\nLifetime: ';\n"
"             if (durationNs<1000) tooltip.innerText += durationNs.toFixed(2) + ' ns';\n"
"             else if (durationNs<1000*1000) tooltip.innerText += (durationNs /1000).toFixed(2) + ' us';\n"
"             else if (durationNs<1000* 1000*1000) tooltip.innerText += (durationNs / 1000 /1000).toFixed(2) + ' ms';\n"
"             else tooltip.innerText += (durationNs / 1e9).toFixed(2) + ' s';\n"
"           }\n"
"        });\n"
"        e.addEventListener('mouseleave', function() {\n"
"          tooltip.innerText = ''; \n"
"          tooltip.style.display = 'none';\n"
"          tooltip.style.opacity = 1;\n"
""
"          e.setAttribute('fill-opacity', '100%%');\n"
"          var refId = parseInt(e.getAttribute('data-refid'));\n"
"          if (refId>=0) {\n"
"            var related = document.querySelector('circle[data-id=\"' + refId + '\"]');\n"
"            related.setAttribute('r', %llu);\n" 
"            related.setAttribute('fill-opacity', '100%%');\n"
"            related.style.opacity = 1;\n"
"            e.setAttribute('r', %llu);\n"
"          }\n"
"        });\n"
"      });\n"
"   </script>\n"
"</html>\n"
      // clang-format on
      ,
      circle_r, circle_r);
  return 0;
}
