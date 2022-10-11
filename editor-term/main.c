#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <malloc/_malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/termios.h>
#include <termios.h>
#include <unistd.h>

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
} key_t;

typedef struct {
  char* s;
  uint32_t cap, len;
} buf_t;

typedef struct {
  // Screen dimensions
  uint16_t rows, cols;
  // Cursor
  uint64_t cx, cy;
  buf_t draw_buf;
} editor_t;

static buf_t buf_make(uint32_t cap) {
  buf_t buf = {
      .s = calloc(cap, 1),
      .cap = cap,
  };
  return buf;
}

static void buf_append(buf_t* buf, char* s, uint32_t size) {
  assert(buf->len + size < buf->cap);

  memcpy(buf->s + buf->len, s, size);
  buf->len += size;
}

static void buf_reset(buf_t* buf) { buf->len = 0; }

static key_t read_key() {
  char c = 0;
  if (read(STDIN_FILENO, &c, 1) < 0) {
    fprintf(stderr, "Failed to read(2): %s\n", strerror(errno));
    exit(errno);
  }
  return c;
}

static void handle_key(editor_t* editor, key_t key) {
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

  buf_append(&e->draw_buf, "\x1b[J", 1);     // Clear screen
  buf_append(&e->draw_buf, "\x1b[?25l", 6);  // Hide cursor
  buf_append(&e->draw_buf, "\x1b[H", 3);     // Go home

  char debug[80] = "";
  uint32_t debug_len = snprintf(debug, sizeof(debug) - 1,
                                "\x1b[0Kcols=%d rows=%d cx=%llu cy=%llu\r\n",
                                e->cols, e->rows, e->cx, e->cy);
  buf_append(&e->draw_buf, debug, debug_len);
  // Padding
  buf_append(&e->draw_buf, "\x1b[41m", 5);
  for (uint16_t i = 1; i < e->rows - 1; i++) {
    buf_append(&e->draw_buf, "\x1b[0K", 4);
    buf_append(&e->draw_buf, "\r\n", 2);
  }
  buf_append(&e->draw_buf, "\x1b[?25h", 6);  // Show cursor

  write(STDOUT_FILENO, e->draw_buf.s, e->draw_buf.len);
}

int main() {
  screen_enable_raw_mode();
  uint16_t cols = 0, rows = 0;
  get_window_size(&cols, &rows);
  editor_t editor = {
      .draw_buf = buf_make(cols * rows * 5), .cols = cols, .rows = rows};

  while (1) {
    const key_t key = read_key();
    if (key == 0) {
      usleep(50);
    }

    handle_key(&editor, key);
    draw(&editor);
    buf_reset(&editor.draw_buf);
  }
}
