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

#include "../pg/pg.h"

static struct termios original_termios;

typedef enum {
  K_NONE = 0,
  K_TAB = 9,
  K_ENTER = 13,
  K_ESC = 27,
} pg_key_t;

typedef struct {
  pg_allocator_t allocator;
  // Screen dimensions
  uint64_t rows, cols;
  // Cursor
  uint64_t cx, cy, coffset;

  pg_string_t ui;
  pg_string_t draw;
  pg_string_t text;
  pg_array_t(pg_span_t) lines;

  uint64_t line_column_width;
} editor_t;

static pg_key_t term_read_key(void) {
  char c = 0;
  if (read(STDIN_FILENO, &c, 1) < 0) {
    fprintf(stderr, "Failed to read(2): %s\n", strerror(errno));
    exit(errno);
  }
  return (pg_key_t)c;
}

static uint8_t editor_get_line_column_width(editor_t *e) {
  char tmp[25] = "";
  return (uint8_t)snprintf(tmp, sizeof(tmp), "%llu", pg_array_len(e->lines)) +
         /* border */ 1;
}

static void editor_parse_text(editor_t *e) {
  pg_array_clear(e->lines);

  pg_span_t text = pg_span_make(e->text);
  while (true) {
    pg_span_t left = {0}, right = {0};
    if (!pg_span_split_at_first(text, '\n', &left, &right))
      break;

    pg_array_append(e->lines, left);
    pg_span_consume_left(&right, 1); // `\n`
    text = right;
  }

  e->line_column_width = editor_get_line_column_width(e);
}

static void editor_del_char(editor_t *e, uint64_t pos) {
  pg_span_t line = e->lines[e->cy];
  if (line.len == 0)
    return;

  assert(pos < (uint64_t)pg_string_len(e->text));

  memmove(e->text + pos, e->text + pos + 1, pg_string_len(e->text) - pos - 1);
  e->text[pg_string_len(e->text) - 1] = '?'; // For debuggability
  pg__set_string_len(e->text, pg_string_len(e->text) - 1);

  editor_parse_text(e);
}

static void editor_handle_key(editor_t *e, pg_key_t key) {
  switch ((int)key) {
  case K_ESC:
    exit(0);
  case 'x':
    editor_del_char(e, e->coffset);
    break;
  case 'h':
    if (e->cx > 0) {
      e->cx--;
      e->coffset--;
    }
    break;
  case 'j': {
    if (e->cy < pg_array_len(e->lines) - 1) {
      pg_span_t prev_line = e->lines[e->cy];
      e->cy++;

      e->coffset += prev_line.len;
    }
    break;
  }
  case 'k': {
    if (e->cy > 0) {
      e->cy--;
      pg_span_t cur_line = e->lines[e->cy];

      assert(e->coffset >= cur_line.len);
      e->coffset -= cur_line.len;
    }
    break;
  }
  case 'l': {
    const pg_span_t line = e->lines[e->cy];
    if (e->cx < line.len) {
      e->cx++;
      e->coffset++;
    }
    break;
  }
  case '$': {
    const pg_span_t line = e->lines[e->cy];
    const uint64_t prev_cx = e->cx;
    e->cx = line.len - 1;
    e->coffset += e->cx - prev_cx;
    break;
  }
  case '0':
    e->coffset -= e->cx;
    e->cx = 0;
    break;
  case '_': {
    const pg_span_t line = e->lines[e->cy];

    for (uint64_t i = 0; i < line.len; i++) {
      const uint64_t offset = (uint64_t)(line.data - e->text) + i;
      if (pg_char_is_space(e->text[offset]))
        continue;

      e->cx = i;
      e->coffset = offset;
      break;
    }
    break;
  }
  default:
    break;
  }
}

static void term_disable_raw_mode_and_reset(void) {
  write(STDOUT_FILENO, "\x1b[0m\x1b[J\x1b[H",
        10); // Reset, Clear screen, Go home
  tcsetattr(STDIN_FILENO, TCSAFLUSH, &original_termios);
}

static void term_enable_raw_mode(void) {
  if (tcgetattr(STDIN_FILENO, &original_termios) == -1) {
    fprintf(stderr, "tcgetattr failed: %s\n", strerror(errno));
    exit(errno);
  }

  struct termios raw = original_termios;
  /* input modes: no break, no CR to NL, no parity check, no strip char,
   * no start/stop output control. */
  raw.c_iflag &= ~((uint64_t)BRKINT | ICRNL | INPCK | ISTRIP | IXON);
  /* output modes - disable post processing */
  raw.c_oflag &= ~((uint64_t)OPOST);
  /* control modes - set 8 bit chars */
  raw.c_cflag |= (CS8);
  /* local modes - choing off, canonical off, no extended functions,
   * no signal chars (^Z,^C) */
  raw.c_lflag &= ~((uint64_t)ECHO | ICANON | IEXTEN | ISIG);
  /* control chars - set return condition: min number of bytes and timer. */
  raw.c_cc[VMIN] = 0;  /* Return each byte, or zero for timeout. */
  raw.c_cc[VTIME] = 1; /* 100 ms timeout (unit is tens of second). */

  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) {
    fprintf(stderr, "tcgetattr failed: %s\n", strerror(errno));
    exit(errno);
  }
  atexit(term_disable_raw_mode_and_reset);
}

static void term_get_window_size(uint64_t *cols, uint64_t *rows) {
  struct winsize ws = {0};
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1) {
    fprintf(stderr, "ioctl(2) failed: %s\n", strerror(errno));
    exit(errno);
  }
  *cols = ws.ws_col;
  *rows = ws.ws_row;
}

__attribute__((unused)) static void editor_draw_rgb_color_bg(editor_t *e,
                                                             uint32_t rgb) {
  uint8_t r = (rgb & 0xff0000) >> 16;
  uint8_t g = (rgb & 0x00ff00) >> 8;
  uint8_t b = (rgb & 0x0000ff);
  e->draw = pg_string_appendc(e->draw, "\x1b[48;2;");
  char tmp[15] = "";
  snprintf(tmp, sizeof(tmp) - 1, "%d;%d;%dm", r, g, b);
  e->draw = pg_string_appendc(e->draw, tmp);
}

__attribute__((unused)) static void editor_draw_rgb_color_fg(editor_t *e,
                                                             uint32_t rgb) {
  uint8_t r = (rgb & 0xff0000) >> 16;
  uint8_t g = (rgb & 0x00ff00) >> 8;
  uint8_t b = (rgb & 0x0000ff);

  e->draw = pg_string_appendc(e->draw, "\x1b[38;2;");
  char tmp[15] = "";
  snprintf(tmp, sizeof(tmp) - 1, "%d;%d;%dm", r, g, b);
  e->draw = pg_string_appendc(e->draw, tmp);
}

static uint64_t editor_draw_line_number(editor_t *e, uint64_t line_i) {
  char tmp[27] = "";
  const uint64_t width =
      (uint64_t)snprintf(tmp, sizeof(tmp) - 1, "%llu ", line_i + 1);
  e->draw = pg_string_appendc(e->draw, tmp);
  return width;
}

static void editor_draw_line(editor_t *e, uint64_t line_i) {
  const pg_span_t span = e->lines[line_i];

  e->draw = pg_string_append_length(e->draw, "\x1b[0K", 4);

  uint64_t rem_space_on_line = e->cols;
  const uint64_t line_number_col_width = editor_draw_line_number(e, line_i);
  // Viewport with a width too small unsupported. Avoid overflowing!
  assert(rem_space_on_line > line_number_col_width);

  rem_space_on_line -= line_number_col_width;
  rem_space_on_line -= 1; // trailing newline

  const uint64_t line_draw_count =
      MIN(span.len, e->cols - /* line num col */ 1);
  e->draw = pg_string_append_length(e->draw, span.data, line_draw_count);
  // TODO: line overflow

  for (uint64_t i = line_draw_count; i < rem_space_on_line; i++)
    e->draw = pg_string_appendc(e->draw, " ");

  e->draw = pg_string_appendc(e->draw, "\r\n");
}

static void editor_draw_lines(editor_t *e) {
  for (uint64_t i = 0;
       i < MIN(pg_array_len(e->lines), e->rows - /* debug line */ 1); i++) {
    editor_draw_line(e, i);
  }
}

static void editor_draw_debug_ui(editor_t *e) {
  char tmp[150] = "";
  snprintf(tmp, sizeof(tmp) - 1,
           "cols=%llu | rows=%llu | cx=%llu | cy=%llu | coffset=%llu ", e->cols,
           e->rows, e->cx, e->cy, e->coffset);
  e->ui = pg_string_appendc(e->ui, tmp);
  e->draw = pg_string_append(e->draw, e->ui);
}

static void editor_draw_vert_padding(editor_t *e) {
  if (pg_array_len(e->lines) >= e->rows - /* debug line */ 1)
    return; // nothing to do

  const uint64_t vert_padding_rows =
      e->rows - pg_array_len(e->lines) - /* debug line */ 1;
  for (uint64_t i = 0; i < vert_padding_rows; i++) {
    e->draw = pg_string_append_length(e->draw, "\r\n", 2);
  }
}

static void editor_draw_cursor(editor_t *e) {
  char tmp[100] = "";
  snprintf(tmp, sizeof(tmp) - 1, "\x1b[%llu;%lluH", e->cy + 1,
           e->line_column_width + e->cx + 1); // Go to (cx, cy)
  e->draw = pg_string_appendc(e->draw, tmp);
  e->draw = pg_string_append_length(e->draw, "\x1b[?25h", 6); // Show cursor
}

static void editor_draw(editor_t *e) {
  assert(e->rows > 0);
  assert(e->cols > 0);

  e->draw = pg_string_append_length(e->draw, "\x1b[J", 3); // Clear screen
  e->draw = pg_string_append_length(e->draw, "\x1b[H", 3); // Go home

  editor_draw_lines(e);
  editor_draw_vert_padding(e);
  editor_draw_debug_ui(e);
  editor_draw_cursor(e);

  write(STDOUT_FILENO, e->draw, pg_string_len(e->draw));
  pg_string_clear(e->draw);
  pg_string_clear(e->ui);
}

static editor_t editor_make(uint64_t rows, uint64_t cols) {
  // Can happen if inside a debugger
  // In that case allocate a reasonable size to be able to debug meaningfully
  if (cols == 0)
    cols = 100;
  if (rows == 0)
    rows = 100;

  pg_allocator_t allocator = pg_heap_allocator();
  editor_t e = {
      .allocator = allocator,
      .draw = pg_string_make_reserve(allocator, cols * rows),
      .cols = cols,
      .rows = rows,
      .ui = pg_string_make_reserve(allocator, cols * rows),
      .text = pg_string_make_reserve(pg_heap_allocator(), 0),
  };
  pg_array_init(e.lines, pg_heap_allocator());

  return e;
}

static void editor_ingest_text(editor_t *e, const char *text, uint64_t len) {
  e->text = pg_string_append_length(e->text, text, len);
  editor_parse_text(e);
}

int main(void) {
  term_enable_raw_mode();
  uint64_t cols = 0, rows = 0;
  term_get_window_size(&cols, &rows);
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
    editor_draw(&e);

    const pg_key_t key = term_read_key();
    if (key == 0) {
      usleep(50);
      continue;
    }

    editor_handle_key(&e, key);
  }
}
