#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>

#include "../pg/pg.h"

static pg_logger_t logger = {.level = PG_LOG_INFO};

typedef enum {
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
  PG_PAD(4);
  uint64_t timestamp, size;
  int64_t related_event;
  pg_array_t(stacktrace_entry_t) stacktrace;
  union {
    event_alloc_t alloc;
    event_realloc_t realloc;
  } v;
};

typedef struct {
  pg_array_t(uint64_t) keys;
  pg_array_t(uint64_t) values;
  pg_array_t(uint32_t) hashes;
} hashtable_u64_u64_t;

#define PG_HASHTABLE_LOAD_FACTOR 0.75

static void pg_hashtable_init(hashtable_u64_u64_t *hashtable, uint64_t cap,
                              pg_allocator_t allocator) {
  pg_array_init_reserve(hashtable->keys, cap, allocator);
  pg_array_init_reserve(hashtable->values, cap, allocator);
  pg_array_init_reserve(hashtable->hashes, cap, allocator);
}

static bool pg_hashtable_find(hashtable_u64_u64_t *hashtable, uint64_t key,
                              uint64_t *index) {

  assert(pg_array_capacity(hashtable->keys) ==
         pg_array_capacity(hashtable->values));
  assert(pg_array_capacity(hashtable->keys) ==
         pg_array_capacity(hashtable->hashes));
  assert(pg_array_len(hashtable->keys) == pg_array_len(hashtable->values));
  assert(pg_array_len(hashtable->keys) == pg_array_len(hashtable->hashes));

  const uint32_t hash = pg_hash((uint8_t *)&key, sizeof(uint64_t));
  *index = hash % pg_array_capacity(hashtable->keys);

  for (;;) {
    const uint32_t index_hash = hashtable->hashes[*index];
    if (index_hash == 0)
      return false; /* Not found but suitable empty slot */
    if (index_hash == hash &&
        memcmp(&key, &hashtable->keys[*index], sizeof(uint64_t)) == 0) {
      /* Found after checking for collision */

      return true;
    }
    /* Keep going to find either an empty slot or a matching hash */
    *index = (*index + 1) % pg_array_capacity(hashtable->keys);
  }

  __builtin_unreachable();
}

static void pg_hashtable_upsert(hashtable_u64_u64_t *hashtable, uint64_t key,
                                uint64_t val) {
  assert(pg_array_capacity(hashtable->keys) ==
         pg_array_capacity(hashtable->values));
  assert(pg_array_capacity(hashtable->keys) ==
         pg_array_capacity(hashtable->hashes));
  assert(pg_array_len(hashtable->keys) == pg_array_len(hashtable->values));
  assert(pg_array_len(hashtable->keys) == pg_array_len(hashtable->hashes));

  const uint64_t cap = pg_array_capacity(hashtable->keys);
  assert(cap > 0);
  const uint64_t len = pg_array_len(hashtable->keys);
  if (((double)len / (double)cap) >= PG_HASHTABLE_LOAD_FACTOR) {
    const uint64_t new_cap = (uint64_t)(1.5 * (double)cap + 8);
    pg_array_grow(hashtable->keys, new_cap);
    pg_array_grow(hashtable->values, new_cap);
    pg_array_grow(hashtable->hashes, new_cap);
    assert(pg_array_capacity(hashtable->keys) ==
           pg_array_capacity(hashtable->values));
    assert(pg_array_capacity(hashtable->keys) ==
           pg_array_capacity(hashtable->hashes));
    assert(pg_array_capacity(hashtable->keys) > cap);
    assert(pg_array_capacity(hashtable->values) > cap);
    assert(pg_array_capacity(hashtable->hashes) > cap);
  }
  uint64_t index = -1ULL;
  if (pg_hashtable_find((hashtable), key, &index)) {
    /* Update */
    hashtable->values[index] = val;
    hashtable->hashes[index] = pg_hash((uint8_t *)&key, sizeof(uint64_t));
  } else {
    hashtable->keys[index] = key;
    hashtable->hashes[index] = pg_hash((uint8_t *)&key, sizeof(uint64_t));
    hashtable->values[index] = val;
    const uint64_t new_len = pg_array_len((hashtable)->keys) + 1;
    pg_array_resize(hashtable->keys, new_len);
    pg_array_resize(hashtable->values, new_len);
    pg_array_resize(hashtable->hashes, new_len);
  }
}

static uint64_t pg_hashtable_at(const hashtable_u64_u64_t *hashtable,
                                uint64_t index) {
  assert(index < pg_array_capacity(hashtable->values));
  return hashtable->values[index];
}

static char *power_of_two_string(uint64_t n) {
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
                             bool *found) {
  for (uint64_t i = 0; i < pg_array_len(fn_names); i++) {
    if (pg_span_eq(fn_names[i], name)) {
      *found = true;
      return i;
    }
  }
  return false;
}

// name is of the form:
// foo`bar+0xab
// foo`bar
// foo`+[objc_weirdness]+0xab
static stacktrace_entry_t
fn_name_to_stacktrace_entry(pg_array_t(pg_span_t) * fn_names, pg_span_t name) {
  pg_span_t left = {0}, right = {0};
  int64_t offset = 0;
  if (pg_span_split_at_last(name, '+', &left,
                            &right) &&
      right.len > 0) { // +0xab present at the end
    bool valid = false;
    offset = pg_span_parse_i64_hex(right, &valid);
    if (!valid || offset < 0)
      pg_log_fatal(&logger, EINVAL, "Invalid offset: name=%.*s offset=%.*s",
                   (int)name.len, name.data, (int)right.len, right.data);
  }

  bool found = false;
  uint64_t fn_i = fn_name_find(*fn_names, left, &found);
  if (!found) {
    pg_array_append(*fn_names, left);
    fn_i = pg_array_len(*fn_names) - 1;
  }

  return (stacktrace_entry_t){.fn_i = fn_i, .offset = (uint64_t)offset};
}

static void find_allocation_event_by_allocation_ptr(
    pg_array_t(event_t) events, hashtable_u64_u64_t *last_allocation_by_ptr,
    uint64_t i, uint64_t ptr) {

  uint64_t found_index = -1ULL;
  if (!pg_hashtable_find(last_allocation_by_ptr, ptr, &found_index))
    return;

  const uint64_t other_index =
      pg_hashtable_at(last_allocation_by_ptr, found_index);
  assert(i < pg_array_len(events));
  assert(other_index < pg_array_len(events));

  event_t *const me = &events[i];
  me->related_event = -1;
  me->related_event = (int64_t)other_index;
  event_t *const other = &events[other_index];
  me->size = other->size;
  other->related_event = (int64_t)i;
}

static void parse_input(pg_span_t input, pg_array_t(event_t) * events,
                        pg_array_t(pg_span_t) * fn_names) {
  hashtable_u64_u64_t last_allocation_by_ptr = {0};
  pg_hashtable_init(&last_allocation_by_ptr,  pg_array_capacity(*events) / 2,
                    pg_heap_allocator());

  // Skip empty lines at the start
  while (pg_span_starts_with(input, pg_span_make_c("\n")))
    pg_span_skip_left_until_inclusive(&input, '\n');

  // Skip header, unneeded
  if (pg_span_starts_with(input, pg_span_make_c("CPU")))
    pg_span_skip_left_until_inclusive(&input, '\n');

  const pg_span_t malloc_span = pg_span_make_c("malloc");
  const pg_span_t realloc_span = pg_span_make_c("realloc");
  const pg_span_t calloc_span = pg_span_make_c("calloc");
  const pg_span_t free_span = pg_span_make_c("free");

  while (input.len > 0) {
    event_t event = {.kind = EK_NONE, .related_event = -1};
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
      pg_log_fatal(&logger, EINVAL, "Unkown event kind: %.*s", (int)fn_leaf.len,
                   fn_leaf.data);

    // timestamp
    pg_span_t timestamp_span = {0};
    pg_span_split_at_first(input, ' ', &timestamp_span, &input);
    pg_span_trim_left(&input);
    bool timestamp_valid = false;
    event.timestamp =
        (uint64_t)(pg_span_parse_i64_decimal(timestamp_span, &timestamp_valid));
    if (!timestamp_valid)
      pg_log_fatal(&logger, EINVAL, "Invalid timestamp: %.*s",
                   (int)timestamp_span.len, timestamp_span.data);

    // arg0
    pg_span_t arg0_span = {0};
    pg_span_split_at_first(input, ' ', &arg0_span, &input);
    pg_span_trim_left(&input);
    bool arg0_valid = false;
    const int64_t arg0 =
        event.kind == EK_FREE
            ? pg_span_parse_i64_hex(arg0_span, &arg0_valid)
            : (pg_span_parse_i64_decimal(arg0_span, &arg0_valid));
    if (!arg0_valid || arg0 < 0)
      pg_log_fatal(&logger, EINVAL, "Invalid arg0: %.*s", (int)arg0_span.len,
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
          pg_log_fatal(&logger, EINVAL,
                       "Invalid arg1: arg1_span=%.*s arg1=%lld valid=%d",
                       (int)arg1_span.len, arg1_span.data, arg1, arg1_valid);
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
          pg_log_fatal(&logger, EINVAL,
                       "Invalid arg2: arg2_span=%.*s arg2=%lld valid=%d",
                       (int)arg2_span.len, arg2_span.data, arg2, arg2_valid);
      }
    }

    // Rest of stacktrace
    while (true) {
      bool more_chars = false;
      char c = pg_span_peek_left(input, &more_chars);
      if (!more_chars || pg_char_is_digit(c)) { // The End / New frame
        if (event.kind == EK_ALLOC) {
          event.size = (uint64_t)arg0;
          event.v.alloc.ptr = (uint64_t)arg1;
          pg_array_append(*events, event);
          pg_hashtable_upsert(&last_allocation_by_ptr, event.v.alloc.ptr,
                              pg_array_len(*events) - 1);
        } else if (event.kind == EK_REALLOC) {
          event.size = (uint64_t)arg0;
          event.v.realloc.new_ptr = (uint64_t)arg1;
          event.v.realloc.old_ptr = (uint64_t)arg2;
          pg_array_append(*events, event);

          pg_hashtable_upsert(&last_allocation_by_ptr, event.v.realloc.new_ptr,
                              pg_array_len(*events) - 1);
        } else if (event.kind == EK_FREE) {
          const uint64_t ptr = (uint64_t)arg0;
          if (ptr == 0)
            pg_log_fatal(&logger, EINVAL, "Invalid arg0 in free: arg0=%llu",
                         (uint64_t)arg0);

          pg_array_append(*events, event);

          find_allocation_event_by_allocation_ptr(
              *events, &last_allocation_by_ptr, pg_array_len(*events) - 1, ptr);
        } else {
          __builtin_unreachable();
        }
        break;
      }

      pg_span_trim_left(&input);
      pg_span_t fn = input;
      pg_span_split_at_first(input, '\n', &fn, &input);
      pg_span_trim_left(&input);
      pg_span_trim(&fn);

      const stacktrace_entry_t stacktrace_entry =
          fn_name_to_stacktrace_entry(fn_names, fn);
      pg_array_append(event.stacktrace, stacktrace_entry);
    }
  }
}

static uint64_t event_ptr(const pg_array_t(event_t) events,
                          const event_t *event) {
  if (event->kind == EK_ALLOC)
    return event->v.alloc.ptr;
  if (event->kind == EK_REALLOC)
    return event->v.realloc.new_ptr;

  assert(event->kind == EK_FREE);
  if (event->related_event == -1)
    return 0ULL;
  return event_ptr(events, &events[event->related_event]);
}

static void print_html(const pg_array_t(event_t) events,
                       pg_array_t(pg_span_t) fn_names) {

  const uint64_t monitoring_start = events[0].timestamp;
  const uint64_t monitoring_end = events[pg_array_len(events) - 1].timestamp;
  const uint64_t monitoring_duration = monitoring_end - monitoring_start;

  const uint64_t chart_w = 1600;
  const uint64_t chart_h = 800;
  const uint64_t chart_margin_left = 60;
  const uint64_t chart_margin_bottom = 20;
  const uint64_t chart_padding_top = 10;
  const uint64_t font_size = 10;

  double max_log_size = 0;
  for (uint64_t i = 0; i < pg_array_len(events); i++) {
    const event_t event = events[i];
    max_log_size = MAX(max_log_size, log((double)event.size));
  }
  printf(
      // clang-format off
"<!DOCTYPE html>\n"
"  <html>\n"
"    <head>\n"
"      <meta charset=\"UTF-8\" />\n"
"      <style>\n"
"      </style>\n"
"    </head>\n"
"    <body>\n"
"        <div id=\"tooltip\" style=\"background-color: rgb(40, 40, 40); opacity:1; color:white; border-radius:8px; display:none; position: absolute;\"></div>\n"
"   <fieldset>\n"
"       <legend>Show:</legend>\n"
"\n"
"       <div>\n"
"         <input type=\"checkbox\" id=\"allocations\" name=\"allocations\" checked onchange=\"document.querySelectorAll('circle[data-kind=alloc]').forEach(function(e) {e.setAttribute('fill-opacity', checked | 0)})\">\n"
"         <label for=\"allocations\">Allocations</label>\n"
"       </div>\n"
"\n"
"       <div>\n"
"         <input type=\"checkbox\" id=\"frees\" name=\"frees\" checked onchange=\"document.querySelectorAll('circle[data-kind=free]').forEach(function(e) {e.setAttribute('fill-opacity', checked | 0)})\">\n"
"\n"
"         <label for=\"frees\">Frees</label>\n"
"       </div>\n"
"\n"
"       <div>\n"
"         <input type=\"checkbox\" id=\"failed_allocations\" name=\"failed_allocations\" checked onchange=\"toggleFailedAllocations(this.checked)\">\n"
"         <label for=\"failed_allocations\">Failed allocations</label>\n"
"       </div>\n"
"\n"
"       <div>\n"
"         <input type=\"checkbox\" id=\"zero_allocations\" name=\"zero_allocations\" checked onchange=\"toggleZeroSizedAllocations(this.checked)\">\n"
"         <label for=\"zero_allocations\">Zero size allocations</label>\n"
"       </div>\n"
"   </fieldset>\n"
"        <svg style=\"margin: 10px\" width=\"%llu\" height=\"%llu\" font-family=\"sans-serif\" font-size=\"%llu\" text-anchor=\"end\">\n"
"           <g><text x=\"%llu\" y=\"%llu\" font-weight=\"bold\">Time</text></g>\n"
"           <g><line x1=\"%llu\" y1=\"%llu\" x2=\"%llu\" y2=\"%llu\" stroke=\"black\" stroke-width=\"3\"></line></g>\n"
"           <g><text x=\"%llu\" y=\"%llu\" font-weight=\"bold\">Allocations</text></g>\n"
"           <g><line x1=\"%llu\" y1=\"%llu\" x2=\"%llu\" y2=\"%llu\" stroke=\"black\" stroke-width=\"3\"></line></g>\n"
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
        (uint64_t)(chart_padding_top +
                   (chart_padding_top + chart_h - ((double)font_size / 2)) *
                       (1.0 - py));
    if (py > 1.0) {
      printf("<g><text x=\"%llu\" y=\"%llu\">%s</text></g>"
             "<g><line x1=\"%llu\" y1=\"%llu\" x2=\"%llu\" y2=\"%llu\" "
             "stroke=\"darkgrey\" stroke-width=\"1\"></line></g>",
             50ULL, font_size, power_of_two_string(i), chart_margin_left, 0ULL,
             chart_w, 0ULL);
      break;
    }

    printf("<g><text x=\"%llu\" y=\"%llu\">%s</text></g>"
           "<g><line x1=\"%llu\" y1=\"%llu\" x2=\"%llu\" y2=\"%llu\" "
           "stroke=\"darkgrey\" stroke-width=\"1\"></line></g>",
           50ULL, y, power_of_two_string(i), chart_margin_left, y, chart_w, y);
  }

  const uint64_t circle_r = 3ULL;
  for (uint64_t i = 0; i < pg_array_len(events); i++) {
    const event_t event = events[i];
    // Skip free events for which we did not record/find the related allocation
    // event since we have no useful information in that case
    if (event.kind == EK_FREE && event.related_event == -1)
      continue;

    const double px = (((double)event.timestamp - (double)monitoring_start)) /
                      (double)monitoring_duration;
    assert(px <= 1.0);

    const uint64_t x =
        (uint64_t)(chart_margin_left + px * (chart_w - chart_margin_left));
    assert(x <= chart_w + chart_margin_left);

    const double py =
        event.size == 0 ? 0 : (log((double)event.size) / max_log_size);
    assert(py <= 1.0);
    const uint64_t y = (uint64_t)(chart_padding_top +
                                  (double)(chart_h - circle_r) * (1.0 - py));
    assert(y <= chart_padding_top + (chart_h - circle_r));

    const uint64_t ptr = event_ptr(events, &event);
    printf("<g class=\"datapoint\"><circle fill=\"%s\" cx=\"%llu\" cy=\"%llu\" "
           "r=\"%llu\" data-kind=\"%s\" data-id=\"%llu\" "
           "data-refid=\"%lld\" "
           "data-size=\"%llu\" data-ptr=\"%#llx\" data-timestamp=\"%llu\" "
           "data-stacktrace=\"",
           event.kind == EK_FREE ? "goldenrod"
                                 : (ptr == 0 ? "crimson" : "steelblue"),
           x, y, circle_r, event.kind == EK_FREE ? "free" : "alloc", i,
           event.related_event, event.size, ptr, event.timestamp);

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
"      function toggleFailedAllocations(checked) {"
"        document.querySelectorAll('circle[data-ptr=\"0\"]').forEach(function(e) {e.setAttribute('fill-opacity', checked | 0)});"
"      }"
"      function toggleZeroSizedAllocations(checked) {"
"        document.querySelectorAll('circle[data-size=\"0\"]').forEach(function(e) {e.setAttribute('fill-opacity', checked | 0)});"
"      }"
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
"           var value = e.getAttribute('data-size');\n"
"           var ptr = e.getAttribute('data-ptr');\n"
"           var timestamp = parseInt(e.getAttribute('data-timestamp'));\n"
"           var stacktrace = e.getAttribute('data-stacktrace');\n"
"           tooltip.innerText = 'Size: ' + value + '\\nTimestamp:' + timestamp + '\\nPointer: ' + ptr + '\\nStacktrace:' + stacktrace; \n"
"           tooltip.style.display = '';\n"
"           tooltip.style.padding = '5px';\n"
"           tooltip.style.left = 5 + mouse_x + 'px';\n"
"           tooltip.style.top = 5 + mouse_y + 'px';\n"

"           e.setAttribute('fill-opacity', '60%%');\n"
"           var refId = parseInt(e.getAttribute('data-refid'));\n"
"           if (refId>=0) {\n"
"             var related = document.querySelector('circle[data-id=\"' + refId + '\"]');\n"
"             var stracktraceRelated = related.getAttribute('data-stacktrace');\n"
"             e.setAttribute('r', 12);\n"
"             related.setAttribute('r', 12);\n" 
"             related.setAttribute('fill-opacity', '60%%');\n"

"             var timestampRelated = parseInt(related.getAttribute('data-timestamp'));\n"
"             tooltip.innerText += '\\n\\nRelates to:\\nTimestamp: ' + timestampRelated + '\\nStacktrace:' + stracktraceRelated; \n"
"             var durationNs = Math.abs(timestamp - timestampRelated);\n"
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
}

int main(int argc, char *argv[]) {
  pg_array_t(uint8_t) file_data = {0};
  pg_array_init_reserve(file_data, 0, pg_heap_allocator());
  if (argc == 1) {
    pg_log_fatal(&logger, EINVAL, "TODO read from stdin");
  } else if (argc == 2) {
    int fd = open(argv[1], O_RDONLY);
    if (fd == -1) {
      pg_log_fatal(&logger, errno, "Failed to open file %s: %s", argv[1],
                   strerror(errno));
    }
    if ((!pg_read_file(argv[1], &file_data)) != 0) {
      pg_log_fatal(&logger, errno, "Failed to read file %s: %s", argv[1],
                   strerror(errno));
    }
  } else {
    return EINVAL;
  }
  assert(file_data != NULL);

  pg_span_t input = {.data = (char *)file_data, .len = pg_array_len(file_data)};

  pg_array_t(event_t) events = {0};
  pg_array_init_reserve(events, input.len / 400, pg_heap_allocator());

  pg_array_t(pg_span_t) fn_names = {0};
  pg_array_init_reserve(fn_names, pg_array_capacity(events) / 10,
                        pg_heap_allocator());

  parse_input(input, &events, &fn_names);

  print_html(events, fn_names);
  return 0;
}
