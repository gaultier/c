#include <fcntl.h>
#include <poll.h>
#include <pty.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#define u64 uint64_t
#define i64 int64_t
#define u32 uint32_t
#define i32 int32_t
#define u16 uint16_t
#define i16 int16_t
#define u8 uint8_t
#define i8 int8_t

#define PG_ARRAY_SIZE(array) (sizeof(array) / sizeof((array)[0]))

#define pg_assert(condition)                                                   \
  do {                                                                         \
    if (!(condition))                                                          \
      __builtin_trap();                                                        \
  } while (0)

#define X11_OP_REQ_CREATE_WINDOW 0x01
#define X11_OP_REQ_MAP_WINDOW 0x08
#define X11_OP_REQ_CREATE_PIX 0x35
#define X11_OP_REQ_CREATE_GC 0x37
#define X11_OP_REQ_OPEN_FONT 0x2d
#define X11_OP_REQ_IMAGE_TEXT8 0x4c
#define X11_OP_REQ_QUERY_TEXT_EXTENTS 0x30

#define X11_FLAG_GC_FUNC 0x00000001
#define X11_FLAG_GC_PLANE 0x00000002
#define X11_FLAG_GC_BG 0x00000004
#define X11_FLAG_GC_FG 0x00000008
#define X11_FLAG_GC_LINE_WIDTH 0x00000010
#define X11_FLAG_GC_LINE_STYLE 0x00000020
#define X11_FLAG_GC_FONT 0x00004000
#define X11_FLAG_GC_EXPOSE 0x00010000

#define X11_FLAG_WIN_BG_IMG 0x00000001
#define X11_FLAG_WIN_BG_COLOR 0x00000002
#define X11_FLAG_WIN_BORDER_IMG 0x00000004
#define X11_FLAG_WIN_BORDER_COLOR 0x00000008
#define X11_FLAG_WIN_EVENT 0x00000800

#define X11_EVENT_FLAG_KEY_RELEASE 0x0002
#define X11_EVENT_FLAG_EXPOSURE 0x8000

#define X11_EVENT_KEY_RELEASE 0x3
#define X11_EVENT_EXPOSURE 0xc

#define MY_COLOR_ARGB 0x00ffffff

// TODO: Use X11's GetKeyboardMapping instead?
static const char keycode_to_keysym[255] = {
    [24] = 'q', [25] = 'w', [26] = 'e', [27] = 'r', [28] = 't',
    [29] = 'y', [30] = 'u', [31] = 'i', [32] = 'o', [33] = 'p',
    [38] = 'a', [39] = 's', [40] = 'd', [41] = 'f', [42] = 'g',
    [43] = 'h', [44] = 'j', [45] = 'k', [46] = 'l', [52] = 'z',
    [53] = 'x', [54] = 'c', [55] = 'v', [56] = 'b', [58] = 'm',
};

typedef struct {
  u8 order;
  u8 pad1;
  u16 major, minor;
  u16 auth_proto, auth_data;
  u16 pad2;
} x11_connection_req_t;

typedef struct {
  u8 success;
  u8 pad1;
  u16 major, minor;
  u16 length;
} x11_connection_reply_t;

typedef struct {
  u32 release;
  u32 id_base, id_mask;
  u32 motion_buffer_size;
  u16 vendor_length;
  u16 request_max;
  u8 roots;
  u8 formats;
  u8 image_order;
  u8 bitmap_order;
  u8 scanline_unit, scanline_pad;
  u8 keycode_min, keycode_max;
  u32 pad;
} x11_connection_setup_t;

typedef struct {
  u8 depth;
  u8 bpp;
  u8 scanline_pad;
  u8 pad1;
  u32 pad2;
} x11_pixmap_format_t;

typedef struct {
  u32 id;
  u32 colormap;
  u32 white, black;
  u32 input_mask;
  u16 width, height;
  u16 width_mm, height_mm;
  u16 maps_min, maps_max;
  u32 root_visual_id;
  u8 backing_store;
  u8 save_unders;
  u8 depth;
  u8 depths;
} x11_root_window_t;

typedef struct {
  u8 depth;
  u8 pad1;
  u16 visuals;
  u32 pad2;
} x11_depth_t;

typedef struct {
  u8 group;
  u8 bits;
  u16 colormap_entries;
  u32 mask_red, mask_green, mask_blue;
  u32 pad;
} x11_visual_t;

typedef struct {
  x11_connection_reply_t header;
  x11_connection_setup_t *setup;
  x11_pixmap_format_t *format;
  x11_root_window_t *root;
  x11_depth_t *depth;
  x11_visual_t *visual;
} x11_connection_t;

typedef struct {
  u8 success;
  u8 code;
  u16 seq;
  u32 id;
  u16 op_major;
  u8 op_minor;
  u8 pad[21];
} x11_error_t;

typedef struct {
  u16 sun_family;
  char sun_path[108];
} sockaddr_un;

#define eprint(s) write(STDERR_FILENO, s, sizeof(s))

typedef struct {
  u8 *base;
  u64 current_offset;
  u64 capacity;
} arena_t;

static void arena_init(arena_t *arena, u64 capacity) {
  pg_assert(arena != NULL);

  arena->base = mmap(NULL, capacity, PROT_READ | PROT_WRITE,
                     MAP_ANON | MAP_PRIVATE, -1, 0);
  pg_assert(arena->base != NULL);
  arena->capacity = capacity;
  arena->current_offset = 0;
}

static u64 align_forward_16(u64 n) {
  const u64 modulo = n & (16 - 1);
  if (modulo != 0)
    n += 16 - modulo;

  pg_assert((n % 16) == 0);
  return n;
}

static void *arena_alloc(arena_t *arena, u64 len) {
  pg_assert(arena != NULL);
  pg_assert(arena->current_offset < arena->capacity);
  pg_assert(arena->current_offset + len < arena->capacity);

  // TODO: align?
  arena->current_offset = align_forward_16(arena->current_offset + len);
  pg_assert((arena->current_offset % 16) == 0);

  return arena->base + arena->current_offset - len;
}

static void arena_reset_at(arena_t *arena, u64 offset) {
  pg_assert(arena != NULL);
  pg_assert(arena->current_offset < arena->capacity);
  pg_assert((arena->current_offset % 16) == 0);

  arena->current_offset = offset;
}

static void set_fd_non_blocking(i32 fd) {
  i64 res = fcntl(fd, F_GETFL, 0);
  if (res < 0) {
    exit((i32)-res);
  }

  res = fcntl(fd, F_SETFL, (i32)((u64)res | (u64)O_NONBLOCK));
  if (res != 0) {
    exit((i32)-res);
  }
}

typedef enum {
  TK_NONE,
  TK_BACKSPACE = 0x08,
  TK_TAB = 0x09,
  TK_LINE_FEED = 0x0a,       // '\n'
  TK_CARRIAGE_RETURN = 0x0d, // '\r'
  TK_DEL = 0x7f,
  TK_TEXT = 0x100,
} token_kind_t;

typedef struct {
  uint64_t start;
  uint64_t end_excl;
  token_kind_t kind;
} token_t;

static void parse_input(const u8 *s, u64 s_byte_count, token_t *tokens,
                        u64 *tokens_count) {
  pg_assert(s != NULL);
  pg_assert(tokens != NULL);
  pg_assert(tokens_count != NULL);

  static void find_special_char(const u8 *s, u64 s_byte_count,
                                u64 *consumed_byte_count, token_t *token) {
    pg_assert(s != NULL);
    pg_assert(consumed_byte_count != NULL);
    pg_assert(token != NULL);

#if 0
    u64 i = 0;
    while (i < s_byte_count) {
      {
        const u8 *const bs = __builtin_memchr(s, 0x08, s_byte_count);
        if (bs != NULL) {
          tokens[*tokens_count] =
              (token_t){.kind = TK_BACKSPACE, .start = i, .end_excl = i + 1};
          *tokens_count += 1;
          i += (tab - s);

          continue;
        }
      }

      {
        const u8 *const tab = __builtin_memchr(s, 0x09, s_byte_count);
        if (tab != NULL) {
          tokens[*tokens_count] =
              (token_t){.kind = TK_TAB, .start = i, .end_excl = i + 1};
          *tokens_count += 1;
          i += (tab - s);

          continue;
        }
      }
      {
        const u8 *const tab = __builtin_memchr(s, 0x0a, s_byte_count);
        if (tab != NULL) {
          tokens[*tokens_count] =
              (token_t){.kind = TK_LINE_FEED, .start = i, .end_excl = i + 1};
          *tokens_count += 1;
          i += (tab - s);

          continue;
        }
      }
      {
        const u8 *const tab = __builtin_memchr(s, 0x0d, s_byte_count);
        if (tab != NULL) {
          tokens[*tokens_count] = (token_t){
              .kind = TK_CARRIAGE_RETURN, .start = i, .end_excl = i + 1};
          *tokens_count += 1;
          i += (tab - s);

          continue;
        }
      }
      
          tokens[*tokens_count] = (token_t){
              .kind = TK_CARRIAGE_RETURN, .start = i, .end_excl = i + 1};
          *tokens_count += 1;
    }
#endif

    for (u64 i = 0; i < s_byte_count; i++) {
      switch (s[i]) {
      case 0x08: // BS
        tokens[*tokens_count] =
            (token_t){.kind = TK_BACKSPACE, .start = i, .end_excl = i + 1};
        *tokens_count += 1;
        break;

      case 0x09: // TAB
        tokens[*tokens_count] =
            (token_t){.kind = TK_TAB, .start = i, .end_excl = i + 1};
        *tokens_count += 1;
        break;

      case 0x0a: // LF
        tokens[*tokens_count] =
            (token_t){.kind = TK_LINE_FEED, .start = i, .end_excl = i + 1};
        *tokens_count += 1;
        break;

      case 0x0d: // CR
        tokens[*tokens_count] = (token_t){
            .kind = TK_CARRIAGE_RETURN, .start = i, .end_excl = i + 1};
        *tokens_count += 1;
        break;

      default:
        tokens[*tokens_count] = (token_t){.kind = TK_TEXT, .start = i};
        *tokens_count += 1;

        while (i < s_byte_count &&
               !(s[i] == 0x08 || s[i] == 0x09 || s[i] == 0x0a || s[i] == 0x0d))
          i++;

        tokens[*tokens_count - 1].end_excl = i;
      }
    }
  }
}

static u64 x11_read_response(i32 fd, u8 *read_buffer, u64 read_buffer_length) {

  const i64 res = read(fd, read_buffer, read_buffer_length);
  if (res <= 0) {
    exit(1);
  }
  return (u64)res;
}

static void x11_open_font(i32 fd, u32 font_id) {
#define OPEN_FONT_NAME "fixed"
#define OPEN_FONT_NAME_BYTE_COUNT 5
#define PADDING ((4 - (OPEN_FONT_NAME_BYTE_COUNT % 4)) % 4)
#define OPEN_FONT_PACKET_U32_COUNT                                             \
  (3 + (OPEN_FONT_NAME_BYTE_COUNT + PADDING) / 4)

  u32 packet[OPEN_FONT_PACKET_U32_COUNT] = {
      [0] = X11_OP_REQ_OPEN_FONT | (OPEN_FONT_NAME_BYTE_COUNT << 16),
      [1] = font_id,
      [2] = OPEN_FONT_NAME_BYTE_COUNT,
  };
  __builtin_memcpy(&packet[3], OPEN_FONT_NAME, OPEN_FONT_NAME_BYTE_COUNT);

  const i64 res = write(fd, (const void *)packet, sizeof(packet));
  if (res != sizeof(packet)) {
    exit(1);
  }

#undef PADDING
#undef OPEN_FONT_PACKET_U32_COUNT
#undef OPEN_FONT_NAME
}

static void x11_draw_text(i32 fd, u32 window_id, u32 gc_id, const u8 *text,
                          u8 text_byte_count, u16 x, u16 y, arena_t *arena) {
  pg_assert(fd > 0);
  pg_assert(text != NULL);
  pg_assert(arena != NULL);

  const u32 padding = (4 - (text_byte_count % 4)) % 4;
  const u32 packet_u32_count = 4 + ((text_byte_count + padding) / 4);

  const u64 arena_offset = arena->current_offset;
  u32 *const packet = arena_alloc(arena, packet_u32_count);
  pg_assert(packet != NULL);

  packet[0] = X11_OP_REQ_IMAGE_TEXT8 | ((u32)text_byte_count << 8) |
              (packet_u32_count << 16);
  packet[1] = window_id;
  packet[2] = gc_id;
  packet[3] = (u32)x | ((u32)y << 16);
  __builtin_memcpy(&packet[4], text, text_byte_count);

  const i64 res = write(fd, packet, packet_u32_count * 4);
  if (res != packet_u32_count * 4) {
    exit(1);
  }

  arena_reset_at(arena, arena_offset);
}

static void x11_handshake(i32 fd, x11_connection_t *connection, u8 *read_buffer,
                          u64 read_buffer_length) {
  pg_assert(fd > 0);
  pg_assert(connection != NULL);
  pg_assert(read_buffer != NULL);
  pg_assert(read_buffer_length > 0);

  x11_connection_req_t req = {.order = 'l', .major = 11};

  i64 res = write(fd, &req, sizeof(req));
  if (res != sizeof(req)) {
    exit((i32)-res);
  }

  x11_connection_reply_t header = {0};
  res = read(fd, &header, sizeof(header));
  if (res != sizeof(header)) {
    exit((i32)-res);
  }

  if (!header.success) {
    exit(1);
  }

  pg_assert(header.length * (u64)sizeof(i32) < read_buffer_length);
  const u64 expected_read_length = header.length * (u64)sizeof(i32);
  res = read(fd, read_buffer, expected_read_length);
  if (res != (i64)expected_read_length) {
    exit(1);
  }

  // TODO: better deserializing.
  connection->setup = (x11_connection_setup_t *)read_buffer;
  void *p = read_buffer + sizeof(x11_connection_setup_t) +
            connection->setup->vendor_length;
  pg_assert((u8 *)p < (u8 *)read_buffer + read_buffer_length);

  p += sizeof(x11_pixmap_format_t) * connection->setup->formats;
  pg_assert((u8 *)p < (u8 *)read_buffer + read_buffer_length);

  connection->root = (x11_root_window_t *)p;
  p += sizeof(x11_root_window_t) * connection->setup->roots;
  pg_assert((u8 *)p < (u8 *)read_buffer + read_buffer_length);
  connection->depth = (x11_depth_t *)p;
  connection->visual = (x11_visual_t *)p;
}

static u32 x11_generate_id(x11_connection_t const *conn) {
  static u32 id = 0;
  return ((conn->setup->id_mask & id++) | conn->setup->id_base);
}

static void x11_create_gc(i32 fd, u32 gc_id, u32 root_id, u32 font_id) {
  pg_assert(fd > 0);
  pg_assert(gc_id > 0);
  pg_assert(root_id > 0);

  const u32 flags = X11_FLAG_GC_BG | X11_FLAG_GC_FG | X11_FLAG_GC_FONT;

#define CREATE_GC_FLAG_COUNT 3
#define CREATE_GC_PACKET_U32_COUNT (4 + CREATE_GC_FLAG_COUNT)

  const u32 packet[CREATE_GC_PACKET_U32_COUNT] = {
      [0] = X11_OP_REQ_CREATE_GC | (CREATE_GC_PACKET_U32_COUNT << 16),
      [1] = gc_id,
      [2] = root_id,
      [3] = flags,
      [4] = MY_COLOR_ARGB,
      [5] = 0,
      [6] = font_id,
  };

  const i64 res = write(fd, (const void *)packet, sizeof(packet));
  if (res != sizeof(packet)) {
    exit(1);
  }

#undef CREATE_GC_PACKET_U32_COUNT
}

static void x11_create_window(i32 fd, u32 window_id, u32 root_id, u16 x, u16 y,
                              u16 w, u16 h, u32 root_visual_id) {

  const u32 flags = X11_FLAG_WIN_BG_COLOR | X11_FLAG_WIN_EVENT;
#define CREATE_WINDOW_FLAG_COUNT 2
#define CREATE_WINDOW_PACKET_U32_COUNT (8 + CREATE_WINDOW_FLAG_COUNT)

  const u16 border = 1, group = 1;

  const u32 packet[CREATE_WINDOW_PACKET_U32_COUNT] = {
      [0] = X11_OP_REQ_CREATE_WINDOW | (CREATE_WINDOW_PACKET_U32_COUNT << 16),
      [1] = window_id,
      [2] = root_id,
      [3] = x | ((u32)y << 16),
      [4] = w | ((u32)h << 16),
      [5] = group | (border << 16),
      [6] = root_visual_id,
      [7] = flags,
      [8] = 0,
      [9] = X11_EVENT_FLAG_KEY_RELEASE | X11_EVENT_FLAG_EXPOSURE,
  };

  const i64 res = write(fd, (const void *)packet, sizeof(packet));
  if (res != sizeof(packet)) {
    exit((i32)-res);
  }

#undef CREATE_WINDOW_FLAG_COUNT
#undef CREATE_WINDOW_PACKET_U32_COUNT
}

static void x11_map_window(i32 fd, u32 window_id) {
  const u32 packet[2] = {
      [0] = X11_OP_REQ_MAP_WINDOW | (2 << 16), [1] = window_id};

  const i64 res = write(fd, (const void *)packet, sizeof(packet));
  if (res != sizeof(packet)) {
    exit((i32)-res);
  }
}

static void x11_query_text_extents(i32 fd, u32 font_id, const u8 *text,
                                   u32 text_byte_count, arena_t *arena) {
  const u32 padding = (4 - (2 * text_byte_count % 4)) % 4;
  const u32 packet_u32_count = 2 + ((2 * text_byte_count + padding) / 4);

  const u64 arena_offset = arena->current_offset;
  u32 *const packet = arena_alloc(arena, packet_u32_count);
  pg_assert(packet != NULL);

  const bool is_odd_length = (padding == 2);

  packet[0] = X11_OP_REQ_QUERY_TEXT_EXTENTS | ((u32)is_odd_length << 8) |
              (packet_u32_count << 16);
  packet[1] = font_id;
  for (u32 i = 0; i < text_byte_count; i++) {
    packet[2 + 2 * i] = 0;
    packet[2 + 2 * i + 1] = text[i];
  }

  const i64 res = write(fd, packet, packet_u32_count * 4);
  if (res != packet_u32_count * 4) {
    exit(1);
  }

  u8 response[32] = {0};

  x11_read_response(fd, response, sizeof(response));

  arena_reset_at(arena, arena_offset);
}

static i32 spawn_shell(u16 num_lines, u16 num_columns, i32 *child_pid) {
  pg_assert(num_lines > 0);
  pg_assert(num_columns > 0);
  pg_assert(child_pid != NULL);

  i32 master = 0, slave = 0;
  struct winsize winp = {.ws_row = num_lines, .ws_col = num_columns};
  openpty(&master, &slave, NULL, NULL, &winp);

  *child_pid = fork();
  if (*child_pid == -1) {
    exit(1);
  }

  if (*child_pid == 0) { // Child
    if (dup2(slave, STDIN_FILENO) == -1) {
      eprint("Failed to dup2 stdin");
      exit(1);
    }
    if (dup2(slave, STDOUT_FILENO) == -1) {
      eprint("Failed to dup2 stdout");
      exit(1);
    }
    if (dup2(slave, STDERR_FILENO) == -1) {
      eprint("Failed to dup2 stderr");
      exit(1);
    }

    // Create a new process group.
    if (setsid() == -1) {
      eprint("Failed to setsid");
      exit(1);
    }

    // Set controlling terminal.
    if (ioctl(slave, TIOCSCTTY, 0) != 0) {
      eprint("Failed to ioctl TIOCSCTTY");
      exit(1);
    }

    // Close now unneeded file descriptors.
    close(slave);
    close(master);

    /* signal(SIGCHLD, SIG_DFL); */
    /* signal(SIGHUP, SIG_DFL); */
    /* signal(SIGINT, SIG_DFL); */
    /* signal(SIGQUIT, SIG_DFL); */
    /* signal(SIGTERM, SIG_DFL); */
    /* signal(SIGALRM, SIG_DFL); */

    char *const argv[] = {(char *)"/bin/sh", 0};
    char *const envp[] = {(char *)"HOME=/home/pg", (char *)"USER=pg", 0};
    if (execve("/bin/sh", argv, envp) == -1) {
      exit(1);
    }

    __builtin_unreachable();
  }
  // Parent.

  close(slave);
  set_fd_non_blocking(master);
  /* signal(SIGCHLD, sigchld); */

  return master;
}

i32 main() {
  const u16 width = 800;
  const u16 height = 600;
  const u16 font_size = 20; // FIXME: Get it from server.
  const u16 num_lines = height / font_size;
  const u16 num_columns = width / font_size;

  i32 child_pid = 0;
  const i32 master = spawn_shell(num_lines, num_columns, &child_pid);
  pg_assert(child_pid > 0);
  pg_assert(master > 0);

  arena_t arena = {0};
  arena_init(&arena, 1 << 20);

  i32 x11_socket_fd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (x11_socket_fd < 0) {
    eprint("Error opening socket");
    exit(-x11_socket_fd);
  }
  pg_assert(x11_socket_fd > 0);

  const sockaddr_un addr = {.sun_family = AF_UNIX,
                            .sun_path = "/tmp/.X11-unix/X0"};
  const i32 res =
      connect(x11_socket_fd, (const struct sockaddr *)&addr, sizeof(addr));
  if (res != 0) {
    eprint("Error connecting");
    exit(-res);
  }

  const u64 read_buffer_length = 1 << 15;
  u8 *const read_buffer = arena_alloc(&arena, read_buffer_length);

  x11_connection_t connection = {0};
  x11_handshake(x11_socket_fd, &connection, read_buffer, read_buffer_length);

  const u32 gc_id = x11_generate_id(&connection);
  const u32 font_id = x11_generate_id(&connection);

  x11_open_font(x11_socket_fd, font_id);

  x11_create_gc(x11_socket_fd, gc_id, connection.root->id, font_id);

  const u32 window_id = x11_generate_id(&connection);
  pg_assert(window_id > 0);

  const u16 x = 200, y = 200, w = 800, h = 600;
  x11_create_window(x11_socket_fd, window_id, connection.root->id, x, y, w, h,
                    connection.root->root_visual_id);

  x11_map_window(x11_socket_fd, window_id);

  x11_query_text_extents(x11_socket_fd, font_id, (const u8 *)"hello", 0,
                         &arena);

  set_fd_non_blocking(x11_socket_fd);
  struct pollfd fds[] = {{.fd = x11_socket_fd, .events = POLLIN},
                         {.fd = master, .events = POLLIN}};

  u8 *text = arena_alloc(&arena, 1 << 16);
  u64 text_len = 0;
  for (;;) {
    const i32 changed_count = poll(fds, PG_ARRAY_SIZE(fds), -1);
    if (changed_count == -1) {
      exit(1);
    }
    pg_assert((u64)changed_count <= PG_ARRAY_SIZE(fds));

    for (u64 i = 0; i < (u64)changed_count; i++) {
      const struct pollfd changed = fds[i];

      pg_assert((changed.revents & POLLNVAL) == 0);
      pg_assert((changed.revents & POLLPRI) == 0); // Unimplemented yet.

      // Other end is closed, stop.
      if ((changed.revents & POLLERR) || (changed.revents & POLLHUP)) {
        exit(1);
      }
      pg_assert(changed.revents & POLLIN);

      u8 buf[1024] = {0};
      const u64 read_byte_count =
          x11_read_response(x11_socket_fd, buf, sizeof(buf));
      pg_assert(read_byte_count == 32);

      switch (buf[0]) {
      case X11_EVENT_EXPOSURE:
        break;
      case X11_EVENT_KEY_RELEASE:
        if (buf[1] == 9) // Escape.
          return 0;

        const char keysym = keycode_to_keysym[buf[1]];
        if (keysym != 0) {
          text[text_len++] = keysym;
          x11_draw_text(x11_socket_fd, window_id, gc_id, text, (u8)text_len, 10,
                        10, &arena);
        }
        break;
      }
    }
  }
}
