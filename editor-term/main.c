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

#define CLAMP(min, n, max) \
  do {                     \
    if ((n) < (min))       \
      (n) = (min);         \
    else if ((n) > (max))  \
      (n) = (max);         \
  } while (0)

static struct termios original_termios;

typedef enum {
  K_NONE = 0,
  K_TAB = 9,
  K_ENTER = 13,
  K_ESC = 27,
} pg_key_t;

typedef struct {
  char* s;
  uint32_t cap, len;
} buf_t;

typedef struct {
  uint32_t start, len;
} span_t;

typedef struct {
  span_t span;
  uint32_t color;
} text_style_t;

typedef struct {
  // Screen dimensions
  uint16_t rows, cols;
  // Cursor
  uint16_t cx, cy;
  gbString text;
  gbArray(text_style_t) text_styles;
  gbString draw;
} editor_t;

static pg_key_t read_key() {
  char c = 0;
  if (read(STDIN_FILENO, &c, 1) < 0) {
    fprintf(stderr, "Failed to read(2): %s\n", strerror(errno));
    exit(errno);
  }
  return c;
}

static void handle_key(editor_t* editor, pg_key_t key) {
  switch ((int)key) {
    case K_ESC:
      exit(0);
      break;
    case 'h':
      editor->cx--;
      CLAMP(0, editor->cx, editor->cols);
      break;
    case 'j':
      editor->cy++;
      CLAMP(0, editor->cy, editor->rows);
      break;
    case 'k':
      editor->cy--;
      CLAMP(0, editor->cy, editor->rows);
      break;
    case 'l':
      editor->cx++;
      CLAMP(0, editor->cx, editor->cols);
      break;
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
  //__builtin_dump_struct(&original_termios, &printf);

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

static void get_window_size(uint16_t* cols, uint16_t* rows) {
  struct winsize ws = {0};
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1) {
    fprintf(stderr, "ioctl(2) failed: %s\n", strerror(errno));
    exit(errno);
  }
  *cols = ws.ws_col;
  *rows = ws.ws_row;
}

static void draw(editor_t* e) {
  assert(e->rows > 0);
  assert(e->cols > 0);

  e->draw = gb_string_append_length(e->draw, "\x1b[J", 3);     // Clear screen
  e->draw = gb_string_append_length(e->draw, "\x1b[?25l", 6);  // Hide cursor
  e->draw = gb_string_append_length(e->draw, "\x1b[H", 3);     // Go home

  text_style_t text_style = e->text_styles[0];  // FIXME
  e->draw = gb_string_append_fmt(
      e->draw, "\x1b[0K\x1b[48;2;%d;%d;%dm", text_style.color >> 16,
      (text_style.color & 0x00ff00) >> 8, text_style.color & 0xff);

  e->draw = gb_string_append(e->draw, e->text);
  for (uint16_t i = gb_string_length(e->text); i < e->cols; i++) {
    e->draw = gb_string_append_length(e->draw, " ", 1);
  }
  e->draw = gb_string_append_length(e->draw, "\r\n", 2);

  e->draw = gb_string_append_length(e->draw, "\x1b[41m", 5);

  e->draw = gb_string_append_length(e->draw, "\x1b[?25h", 6);  // Show cursor
  write(STDOUT_FILENO, e->draw, gb_string_length(e->draw));
}

int main() {
  screen_enable_raw_mode();
  uint16_t cols = 0, rows = 0;
  get_window_size(&cols, &rows);

  const uint64_t mem_draw_len = cols * rows * 30;
  const uint64_t mem_text_len = cols * rows * sizeof(uint32_t);
  const uint64_t mem_len = mem_draw_len + mem_text_len;
  uint8_t* mem = malloc(mem_len);
  uint8_t* mem_draw = mem;
  uint8_t* mem_text = mem + mem_draw_len;

  gbArena arena_draw = {0};
  gb_arena_init_from_memory(&arena_draw, mem_draw, mem_draw_len);
  gbArena arena_text = {0};
  gb_arena_init_from_memory(&arena_text, mem_text, mem_text_len);

  gbAllocator allocator_draw = gb_arena_allocator(&arena_draw);
  gbAllocator allocator_text = gb_arena_allocator(&arena_text);

  editor_t e = {
      .draw = gb_string_make_reserve(allocator_draw, cols * rows),
      .cols = cols,
      .rows = rows,
      .text = gb_string_make_reserve(allocator_text, cols * rows),
  };
  gb_array_init(e.text_styles, allocator_text);

  while (1) {
    const pg_key_t key = read_key();
    if (key == 0) {
      usleep(50);
    }

    handle_key(&e, key);

    e.text = gb_string_append_fmt(e.text,
                                  "cols=%d | rows=%d | cx=%d | cy=%d | mem=%td",
                                  e.cols, e.rows, e.cx, e.cy, mem_len);

    text_style_t debug_style = {
        .span = {.start = 0, .len = gb_string_length(e.text)},
        .color = 0x29B6F6};
    gb_array_append(e.text_styles, debug_style);

    draw(&e);
    gb_string_clear(e.text);
    gb_string_clear(e.draw);
  }
}
