#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

#define GB_IMPLEMENTATION
#include "../vendor/gb/gb.h"

static struct termios original_termios;

typedef enum {
  K_NONE = 0,
  K_TAB = 9,
  K_ENTER = 13,
  K_ESC = 27,
} pg_key_t;

typedef struct {
  uint64_t start, len;
} span_t;

typedef struct {
  gbAllocator allocator;
  // Screen dimensions
  uint64_t rows, cols;
  // Cursor
  uint64_t cx, cy;

  gbString ui;
  gbString draw;
  gbString text;
  gbArray(span_t) lines;

  uint64_t line_column_width;
} editor_t;

static pg_key_t read_key() {
  char c = 0;
  if (read(STDIN_FILENO, &c, 1) < 0) {
    fprintf(stderr, "Failed to read(2): %s\n", strerror(errno));
    exit(errno);
  }
  return c;
}

static void handle_key(editor_t* e, pg_key_t key) {
  switch ((int)key) {
    case K_ESC:
      exit(0);
      break;
    case 'h':
      if (e->cx > 0) {
        e->cx--;
      }
      break;
    case 'j':
      if (e->cy < (uint64_t)gb_array_count(e->lines) - 1) {
        e->cy++;
      }
      break;
    case 'k':
      if (e->cy > 0) {
        e->cy--;
      }
      break;
    case 'l': {
      const span_t line = e->lines[e->cy];
      if (e->cx < line.len) {
        e->cx++;
      }
      break;
    }
    case '$': {
      const span_t line = e->lines[e->cy];
      e->cx = line.len - 1;
      break;
    }
    case '0':
      e->cx = 0;
      break;
    case '_': {
      const span_t line = e->lines[e->cy];

      for (uint64_t i = 0; i < line.len; i++) {
        const uint64_t offset = line.start + i;
        if (gb_char_is_space(e->text[offset])) continue;

        e->cx = i;
        break;
      }
      break;
    }
    default:
      break;
  }
}

static void screen_disable_raw_mode_and_reset() {
  write(STDOUT_FILENO, "\x1b[0m\x1b[J\x1b[H",
        10);  // Reset, Clear screen, Go home
  tcsetattr(STDIN_FILENO, TCSAFLUSH, &original_termios);
}

static void screen_enable_raw_mode() {
  if (tcgetattr(STDIN_FILENO, &original_termios) == -1) {
    fprintf(stderr, "tcgetattr failed: %s\n", strerror(errno));
    exit(errno);
  }

  struct termios raw = original_termios;
  /* input modes: no break, no CR to NL, no parity check, no strip char,
   * no start/stop output control. */
  raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
  /* output modes - disable post processing */
  raw.c_oflag &= ~(OPOST);
  /* control modes - set 8 bit chars */
  raw.c_cflag |= (CS8);
  /* local modes - choing off, canonical off, no extended functions,
   * no signal chars (^Z,^C) */
  raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
  /* control chars - set return condition: min number of bytes and timer. */
  raw.c_cc[VMIN] = 0;  /* Return each byte, or zero for timeout. */
  raw.c_cc[VTIME] = 1; /* 100 ms timeout (unit is tens of second). */

  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) {
    fprintf(stderr, "tcgetattr failed: %s\n", strerror(errno));
    exit(errno);
  }
  atexit(screen_disable_raw_mode_and_reset);
}

static uint8_t get_line_column_width(editor_t* e) {
  char tmp[25] = "";
  return snprintf(tmp, sizeof(tmp), "%td", gb_array_count(e->lines)) +
         /* border */ 1;
}

static void get_window_size(uint64_t* cols, uint64_t* rows) {
  struct winsize ws = {0};
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1) {
    fprintf(stderr, "ioctl(2) failed: %s\n", strerror(errno));
    exit(errno);
  }
  *cols = ws.ws_col;
  *rows = ws.ws_row;
}

static void editor_parse_text(editor_t* e) {
  uint64_t i = 0;
  while (i < (uint64_t)gb_string_length(e->text)) {
    char* nl = memchr(e->text + i, '\n', gb_string_length(e->text) - i);
    if (nl == NULL) {
      span_t span = {.start = i, .len = gb_string_length(e->text) - i};
      gb_array_append(e->lines, span);
      break;
    }

    const uint64_t len = nl - (e->text + i);
    assert(len > 0);
    assert(len < (uint64_t)gb_string_length(e->text));
    span_t span = {.start = i, .len = len};
    gb_array_append(e->lines, span);
    i += len + 1;
  }

  e->line_column_width = get_line_column_width(e);
}

static void draw_rgb_color_bg(editor_t* e, uint32_t rgb) {
  uint8_t r = (rgb & 0xff0000) >> 16;
  uint8_t g = (rgb & 0x00ff00) >> 8;
  uint8_t b = (rgb & 0x0000ff);
  e->draw = gb_string_append_fmt(e->draw, "\x1b[48;2;%d;%d;%dm", r, g, b);
}

static void draw_rgb_color_fg(editor_t* e, uint32_t rgb) {
  uint8_t r = (rgb & 0xff0000) >> 16;
  uint8_t g = (rgb & 0x00ff00) >> 8;
  uint8_t b = (rgb & 0x0000ff);
  e->draw = gb_string_append_fmt(e->draw, "\x1b[38;2;%d;%d;%dm", r, g, b);
}

static void draw_line_number(editor_t* e, uint64_t line_i) {
  e->draw = gb_string_append_fmt(e->draw, "%d ", line_i + 1);
}

static void draw_line_trailing_padding(editor_t* e, uint64_t line_i) {
  const span_t span = e->lines[line_i];
  for (uint64_t i = 0; i < e->cols - e->line_column_width - span.len; i++) {
    e->draw = gb_string_append_length(e->draw, " ", 1);
  }
}

static void draw_line(editor_t* e, uint64_t line_i) {
  const span_t span = e->lines[line_i];
  const char* const line = e->text + span.start;

  e->draw = gb_string_append_length(e->draw, "\x1b[0K", 4);
  draw_line_number(e, line_i);

  e->draw = gb_string_append_length(e->draw, line, span.len);

  draw_line_trailing_padding(e, line_i);
}

static void draw_lines(editor_t* e) {
  for (uint64_t i = 0; i < MIN((uint64_t)gb_array_count(e->lines),
                               e->rows - /* debug line */ 1);
       i++) {
    draw_line(e, i);
  }
}

static void editor_draw_debug_ui(editor_t* e) {
  const uint64_t mem_len = 0;  // FIXME
  e->ui =
      gb_string_append_fmt(e->ui, "cols=%d | rows=%d | cx=%d | cy=%d | mem=%td",
                           e->cols, e->rows, e->cx, e->cy, mem_len);
  e->draw = gb_string_append(e->draw, e->ui);
}

static void editor_draw_vert_padding(editor_t* e) {
  if ((uint64_t)gb_array_count(e->lines) >= e->rows - /* debug line */ 1)
    return;  // nothing to do

  const uint64_t vert_padding_rows =
      e->rows - (uint64_t)gb_array_count(e->lines) - /* debug line */ 1;
  for (uint64_t i = 0; i < vert_padding_rows; i++) {
    e->draw = gb_string_append_length(e->draw, "\r\n", 2);
  }
}

static void editor_draw_cursor(editor_t* e) {
  e->draw =
      gb_string_append_fmt(e->draw, "\x1b[%d;%dH", e->cy + 1,
                           e->line_column_width + e->cx + 1);  // Go to (cx, cy)
  e->draw = gb_string_append_length(e->draw, "\x1b[?25h", 6);  // Show cursor
}

static void draw(editor_t* e) {
  assert(e->rows > 0);
  assert(e->cols > 0);

  e->draw = gb_string_append_length(e->draw, "\x1b[J", 3);  // Clear screen
  assert(e->draw != NULL);
  e->draw = gb_string_append_length(e->draw, "\x1b[H", 3);  // Go home
  assert(e->draw != NULL);

  draw_lines(e);
  editor_draw_vert_padding(e);

  editor_draw_debug_ui(e);

  editor_draw_cursor(e);

  write(STDOUT_FILENO, e->draw, gb_string_length(e->draw));
  gb_string_clear(e->draw);
  gb_string_clear(e->ui);
}

static editor_t editor_make(uint64_t rows, uint64_t cols) {
  // Can happen if inside a debugger
  // In that case allocate a reasonable size to be able to debug meaningfully
  if (cols == 0) cols = 100;
  if (rows == 0) rows = 100;

  gbAllocator allocator = gb_heap_allocator();
  editor_t e = {
      .allocator = allocator,
      .draw = gb_string_make_reserve(allocator, cols * rows),
      .cols = cols,
      .rows = rows,
      .ui = gb_string_make_reserve(allocator, cols * rows),
      .text = gb_string_make_reserve(gb_heap_allocator(), 0),
  };
  gb_array_init(e.lines, gb_heap_allocator());

  return e;
}

static void editor_ingest_text(editor_t* e, const char* text, uint64_t len) {
  e->text = gb_string_append_length(e->text, text, len);
  editor_parse_text(e);
}

int main() {
  screen_enable_raw_mode();
  uint64_t cols = 0, rows = 0;
  get_window_size(&cols, &rows);
  editor_t e = editor_make(rows, cols);

  const char text[] =
      "hello my darling \nhello my duck \nI don't know what I'm doing please "
      "help! \n"
      "Some more text that I'm typing \nuntil it reaches the end, "
      "\n hopefully "
      "that "
      "will trigger some\n interesting behaviour...";

  editor_ingest_text(&e, text, sizeof(text) - 1);

  while (1) {
    draw(&e);

    const pg_key_t key = read_key();
    if (key == 0) {
      usleep(50);
      continue;
    }

    handle_key(&e, key);
  }
}
