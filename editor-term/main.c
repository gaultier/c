#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/termios.h>
#include <termios.h>
#include <unistd.h>

static struct termios original_termios;

static void disable_raw_mode() {
  tcsetattr(STDIN_FILENO, TCSAFLUSH, &original_termios);
}

static void enable_raw_mode() {
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
  atexit(disable_raw_mode);
}

static int get_window_size(int* cols, int* rows) {
  struct winsize ws = {0};
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1) {
    fprintf(stderr, "ioctl(2) failed: %s\n", strerror(errno));
    exit(errno);
  }
  *cols = ws.ws_col;
  *rows = ws.ws_row;

  return 0;
}

#define BUF_CAP (20 * 1024)
static char buf[BUF_CAP];
static int buf_len = 0;

static void buf_append(char* s, int size) {
  assert(buf_len + size < BUF_CAP);

  memcpy(buf + buf_len, s, size);
  buf_len += size;
}

int main() {
  enable_raw_mode();
  int cols = 0, rows = 0;
  get_window_size(&cols, &rows);
  /* printf("%d %d\n", cols, rows); */

  buf_append("\x1b[J", 1);     // Clear screen
  buf_append("\x1b[?25l", 6);  // Hide cursor
  buf_append("\x1b[H", 3);     // Go home

  buf_append("\x1b[44m\x1b[0Khello\x1b[44m\r\n", 21);
  // Padding
  buf_append("\x1b[41m", 5);
  for (int i = 1; i < rows - 1; i++) {
    buf_append("\x1b[0K", 4);
    for (int j = 0; j < cols; j++) {
      /* buf_append(" ", 1); */
    }
    buf_append("\r\n", 2);
  }
  /* \x1b[0m */
  /* char buf[] = ; */

  buf_append("\x1b[?25h", 6);  // Show cursor

  write(STDOUT_FILENO, buf, buf_len);
}
