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
  gbString ui;
  gbString draw;
  gbString text;
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
      if (editor->cx > 0) {
        editor->cx--;
      }
      break;
    case 'j':
      if (editor->cy < editor->rows - 1) {
        editor->cy++;
      }
      break;
    case 'k':
      if (editor->cy > 0) {
        editor->cy--;
      }
      break;
    case 'l':
      if (editor->cx < editor->cols - 1) {
        editor->cx++;
      }
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

  e->draw = gb_string_append_length(e->draw, "\x1b[J", 3);  // Clear screen
  e->draw = gb_string_append_length(e->draw, "\x1b[H", 3);  // Go home

  e->draw = gb_string_append_fmt(e->draw, "\x1b[0K\x1b[48;2;%d;%d;%dm", 0xE1,
                                 0xF5, 0xFE);

  e->draw = gb_string_append(e->draw, e->text);
  for (uint64_t i = gb_string_length(e->text); i < e->cols; i++) {
    e->draw = gb_string_append_length(e->draw, " ", 1);
  }

  for (uint64_t y = 1; y < e->rows - 1; y++) {
    for (uint64_t x = 0; x < e->cols; x++) {
      e->draw = gb_string_append_length(e->draw, " ", 1);
    }
    e->draw = gb_string_append_length(e->draw, "\r\n", 2);
  }

  e->draw = gb_string_append_fmt(e->draw, "\x1b[0K\x1b[48;2;%d;%d;%dm", 0x29,
                                 0xB6, 0xF6);
  e->draw = gb_string_append(e->draw, e->ui);
  for (uint64_t i = gb_string_length(e->ui); i < e->cols; i++) {
    e->draw = gb_string_append_length(e->draw, " ", 1);
  }

  e->draw = gb_string_append_fmt(e->draw, "\x1b[%d;%dH", e->cy + 1,
                                 e->cx + 1);                   // Go to (cx, cy)
  e->draw = gb_string_append_length(e->draw, "\x1b[?25h", 6);  // Show cursor

  write(STDOUT_FILENO, e->draw, gb_string_length(e->draw));
  gb_string_clear(e->draw);
  gb_string_clear(e->ui);
}

int main() {
  screen_enable_raw_mode();
  uint16_t cols = 0, rows = 0;
  get_window_size(&cols, &rows);
  assert(cols > 0);
  assert(rows > 0);

  const uint64_t mem_draw_len = cols * rows * 100;
  const uint64_t mem_ui_len = cols * rows * sizeof(uint32_t);
  const uint64_t mem_len = mem_draw_len + mem_ui_len;
  uint8_t* mem = malloc(mem_len);
  uint8_t* mem_draw = mem;
  uint8_t* mem_ui = mem + mem_draw_len;

  gbArena arena_draw = {0};
  gb_arena_init_from_memory(&arena_draw, mem_draw, mem_draw_len);
  gbArena arena_ui = {0};
  gb_arena_init_from_memory(&arena_ui, mem_ui, mem_ui_len);

  gbAllocator allocator_draw = gb_arena_allocator(&arena_draw);
  gbAllocator allocator_ui = gb_arena_allocator(&arena_ui);

  editor_t e = {
      .draw = gb_string_make_reserve(allocator_draw, cols * rows),
      .cols = cols,
      .rows = rows,
      .ui = gb_string_make_reserve(allocator_ui, cols * rows),
      .text = gb_string_make_reserve(gb_heap_allocator(), 0),
  };
  e.text = gb_string_append_length(e.text, "hello world!", 12);

  while (1) {
    e.ui = gb_string_append_fmt(e.ui,
                                "cols=%d | rows=%d | cx=%d | cy=%d | mem=%td",
                                e.cols, e.rows, e.cx, e.cy, mem_len);

    draw(&e);

    const pg_key_t key = read_key();
    if (key == 0) {
      usleep(50);
      continue;
    }

    handle_key(&e, key);
  }
}
