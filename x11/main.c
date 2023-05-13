#include <stdbool.h>

#define u64 unsigned long int
#define i64 signed long int
#define u32 unsigned int
#define i32 signed int
#define u16 unsigned short
#define i16 signed short
#define u8 unsigned char
#define i8 signed char
#define NULL 0

#define assert(condition)                                                      \
  do {                                                                         \
    if (!(condition))                                                          \
      __builtin_trap();                                                        \
  } while (0)

static __inline i64 syscall0(i64 n) {
  u64 ret;
  __asm__ __volatile__("syscall" : "=a"(ret) : "a"(n) : "rcx", "r11", "memory");
  return (i64)ret;
}

static __inline i64 syscall1(i64 n, i64 a1) {
  u64 ret;
  __asm__ __volatile__("syscall"
                       : "=a"(ret)
                       : "a"(n), "D"(a1)
                       : "rcx", "r11", "memory");
  return (i64)ret;
}

static __inline i64 syscall2(i64 n, i64 a1, i64 a2) {
  u64 ret;
  __asm__ __volatile__("syscall"
                       : "=a"(ret)
                       : "a"(n), "D"(a1), "S"(a2)
                       : "rcx", "r11", "memory");
  return (i64)ret;
}

static __inline i64 syscall3(i64 n, i64 a1, i64 a2, i64 a3) {
  u64 ret;
  __asm__ __volatile__("syscall"
                       : "=a"(ret)
                       : "a"(n), "D"(a1), "S"(a2), "d"(a3)
                       : "rcx", "r11", "memory");
  return (i64)ret;
}

static __inline i64 syscall4(i64 n, i64 a1, i64 a2, i64 a3, i64 a4) {
  u64 ret;
  register i64 r10 __asm__("r10") = a4;
  __asm__ __volatile__("syscall"
                       : "=a"(ret)
                       : "a"(n), "D"(a1), "S"(a2), "d"(a3), "r"(r10)
                       : "rcx", "r11", "memory");
  return (i64)ret;
}

static __inline i64 syscall5(i64 n, i64 a1, i64 a2, i64 a3, i64 a4, i64 a5) {
  u64 ret;
  register i64 r10 __asm__("r10") = a4;
  register i64 r8 __asm__("r8") = a5;
  __asm__ __volatile__("syscall"
                       : "=a"(ret)
                       : "a"(n), "D"(a1), "S"(a2), "d"(a3), "r"(r10), "r"(r8)
                       : "rcx", "r11", "memory");
  return (i64)ret;
}

static __inline i64 syscall6(i64 n, i64 a1, i64 a2, i64 a3, i64 a4, i64 a5,
                             i64 a6) {
  u64 ret;
  register i64 r10 __asm__("r10") = a4;
  register i64 r8 __asm__("r8") = a5;
  register i64 r9 __asm__("r9") = a6;
  __asm__ __volatile__("syscall"
                       : "=a"(ret)
                       : "a"(n), "D"(a1), "S"(a2), "d"(a3), "r"(r10), "r"(r8),
                         "r"(r9)
                       : "rcx", "r11", "memory");
  return (i64)ret;
}

#define PROT_READ 0x1
#define PROT_WRITE 0x2

#define MAP_PRIVATE 0x2
#define MAP_ANON 0x20

#define AF_UNIX 1
#define SOCK_STREAM 1

#define FD_STDOUT 1
#define FD_STDERR 2

#define F_GETFL 3
#define F_SETFL 4
const int width = 800;
const int height = 600;
const int font_size = 20;

#define O_NONBLOCK 04000

#define STDIN_FILENO 0
#define STDOUT_FILENO 1
#define STDERR_FILENO 0

#define EAGAIN 11 /* Try again */

static i64 sys_write(i32 fd, const void *data, u64 len) {
  return syscall3(1, fd, (i64)data, (i64)len);
}

static i64 sys_read(i32 fd, void *data, u64 len) {
  return syscall3(0, fd, (i64)data, (i64)len);
}

static void sys_exit(i32 code) { syscall1(60, code); }

static i32 sys_socket(i32 domain, i32 type, i32 protocol) {
  return (i32)syscall3(41, (i64)domain, (i64)type, (i64)protocol);
}

static i32 sys_connect(i32 fd, const void *sock_addr, u32 len) {
  return (i32)syscall3(42, (i64)fd, (i64)sock_addr, (i64)len);
}

static void *sys_mmap(void *addr, u64 len, i32 prot, i32 flags, i32 fd,
                      i64 offset) {
  return (void *)syscall6(9, (i64)addr, len, prot, flags, fd, offset);
}

static i64 sys_fcntl(i32 fd, i32 cmd, i32 val) {
  return syscall3(72, fd, cmd, val);
}

struct pollfd {
  i32 fd;      /* File descriptor to poll.  */
  i16 events;  /* Types of events poller cares about.  */
  i16 revents; /* Types of events that actually occurred.  */
};

#define POLLIN 0x001  /* There is data to read.  */
#define POLLPRI 0x002 /* There is urgent data to read.  */
#define POLLOUT 0x004 /* Writing now will not block.  */

#define POLLERR 0x008  /* Error condition.  */
#define POLLHUP 0x010  /* Hung up.  */
#define POLLNVAL 0x020 /* Invalid polling request.  */

static i32 sys_poll(struct pollfd *fds, i32 nfds, i32 timeout_ms) {
  return (i32)syscall3(7, (i64)fds, (i64)nfds, (i64)timeout_ms);
}

#define O_RDONLY 00
#define O_WRONLY 01
#define O_RDWR 02
#define O_NOCTTY 0400
static i32 sys_open(const char *pathname, i32 flags) {
  return (i32)syscall2(0, (i64)pathname, (i64)flags);
}

static i32 sys_close(i32 fd) { return (i32)syscall1(3, (i32)fd); }

static i32 sys_ioctl3(i32 fd, u32 req, i64 flags) {
  return (i32)syscall3(16, fd, req, flags);
}

static i32 sys_dup2(i32 oldfd, i32 newfd) {
  return (i32)syscall2(33, oldfd, newfd);
}

struct winsize {
  u32 ws_row;
  u32 ws_col;
  u32 ws_xpixel;
  u32 ws_ypixel;
};

#define NCCS 32
struct termios {
  u32 c_iflag;   /* input mode flags */
  u32 c_oflag;   /* output mode flags */
  u32 c_cflag;   /* control mode flags */
  u32 c_lflag;   /* local mode flags */
  u8 c_line;     /* line discipline */
  u8 c_cc[NCCS]; /* control characters */
  u32 c_ispeed;  /* input speed */
  u32 c_ospeed;  /* output speed */
};

#define TCSETS 0x5402
i32 tcsetattr(i32 fd, i32 act, const struct termios *tio) {
  assert(act == 0 || act == 1);
  return sys_ioctl3(fd, TCSETS + act, (i64)tio);
}

void str_append_u32(u8 *s, u64 *s_len, u64 s_cap, u32 n) {
  assert(s != NULL);
  assert(s_len != NULL);

  u8 buf[30] = "";
  u64 buf_len = 0;

  for (u64 i = 0; i < sizeof(buf); i++) {
    const u8 x = (u8)(n % 10);
    buf[buf_len++] = x + '0';
    n = n / 10;

    if (n == 0)
      break;
  }
  assert(n == 0);
  assert(buf_len > 0);
  assert(buf_len <= sizeof(buf));
  assert(*s_len + buf_len < s_cap);

  for (i64 i = buf_len - 1; i >= 0; i--) {
    s[*s_len] = buf[i];
    *s_len += 1;
  }

  s[*s_len] = 0;
}

#define TIOCGPTN 0x80045430
#define TIOCSPTLCK 0x40045431
#define TIOCSWINSZ 0x5414
#define TIOCSCTTY 0x540E
#define TCSANOW 0
i32 openpty(i32 *pm, i32 *ps, const struct winsize *ws) {
  i32 m, s, n = 0;
  u8 name[20] = "/dev/pts";

  m = sys_open("/dev/ptmx", O_RDWR | O_NOCTTY);
  assert(m >= 0);

  /* pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &cs); */

  s = sys_ioctl3(m, TIOCSPTLCK, (i64)&n);
  assert(s == 0);
  s = sys_ioctl3(m, TIOCGPTN, (i64)&n);
  assert(s == 0);

  u64 name_len = sizeof("/dev/pts") - 1;
  str_append_u32(name, &name_len, sizeof(name) - 1, m);
  assert(name_len <= sizeof(name) - 1);
  assert(name[name_len] == 0);

  s = sys_open((char *)name, O_RDWR | O_NOCTTY);
  assert(s >= 0);

  if (ws)
    sys_ioctl3(s, TIOCSWINSZ, (i64)ws);

  *pm = m;
  *ps = s;

  /* pthread_setcancelstate(cs, 0); */
  return 0;
}

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

#define eprint(s) sys_write(FD_STDERR, s, sizeof(s))

typedef struct {
  u8 *base;
  u64 current_offset;
  u64 capacity;
} arena_t;

static void arena_init(arena_t *arena, u64 capacity) {
  assert(arena != NULL);

  arena->base = sys_mmap(NULL, capacity, PROT_READ | PROT_WRITE,
                         MAP_ANON | MAP_PRIVATE, -1, 0);
  assert(arena->base != NULL);
  arena->capacity = capacity;
  arena->current_offset = 0;
}

static u64 align_forward_16(u64 n) {
  const u64 modulo = n & (16 - 1);
  if (modulo != 0)
    n += 16 - modulo;

  assert((n % 16) == 0);
  return n;
}

static void *arena_alloc(arena_t *arena, u64 len) {
  assert(arena != NULL);
  assert(arena->current_offset < arena->capacity);
  assert(arena->current_offset + len < arena->capacity);

  // TODO: align?
  arena->current_offset = align_forward_16(arena->current_offset + len);
  assert((arena->current_offset % 16) == 0);

  return arena->base + arena->current_offset - len;
}

static void arena_reset_at(arena_t *arena, u64 offset) {
  assert(arena != NULL);
  assert(arena->current_offset < arena->capacity);
  assert((arena->current_offset % 16) == 0);

  arena->current_offset = offset;
}

static void set_fd_non_blocking(i32 fd) {
  i64 res = sys_fcntl(fd, F_GETFL, 0);
  if (res < 0) {
    sys_exit((i32)-res);
  }

  res = sys_fcntl(fd, F_SETFL, (i32)((u64)res | (u64)O_NONBLOCK));
  if (res != 0) {
    sys_exit((i32)-res);
  }
}

static u64 x11_read_response(i32 fd, u8 *read_buffer, u64 read_buffer_length) {

  const i64 res = sys_read(fd, read_buffer, read_buffer_length);
  if (res <= 0) {
    sys_exit(1);
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

  const i64 res = sys_write(fd, (const void *)packet, sizeof(packet));
  if (res != sizeof(packet)) {
    sys_exit(1);
  }

#undef PADDING
#undef OPEN_FONT_PACKET_U32_COUNT
#undef OPEN_FONT_NAME
}

static void x11_draw_text(i32 fd, u32 window_id, u32 gc_id, const u8 *text,
                          u8 text_byte_count, u16 x, u16 y, arena_t *arena) {
  assert(fd > 0);
  assert(text != NULL);
  assert(arena != NULL);

  const u32 padding = (4 - (text_byte_count % 4)) % 4;
  const u32 packet_u32_count = 4 + ((text_byte_count + padding) / 4);

  const u64 arena_offset = arena->current_offset;
  u32 *const packet = arena_alloc(arena, packet_u32_count);
  assert(packet != NULL);

  packet[0] = X11_OP_REQ_IMAGE_TEXT8 | ((u32)text_byte_count << 8) |
              (packet_u32_count << 16);
  packet[1] = window_id;
  packet[2] = gc_id;
  packet[3] = (u32)x | ((u32)y << 16);
  __builtin_memcpy(&packet[4], text, text_byte_count);

  const i64 res = sys_write(fd, packet, packet_u32_count * 4);
  if (res != packet_u32_count * 4) {
    sys_exit(1);
  }

  arena_reset_at(arena, arena_offset);
}

static void x11_handshake(i32 fd, x11_connection_t *connection, u8 *read_buffer,
                          u64 read_buffer_length) {
  assert(fd > 0);
  assert(connection != NULL);
  assert(read_buffer != NULL);
  assert(read_buffer_length > 0);

  x11_connection_req_t req = {.order = 'l', .major = 11};

  i64 res = sys_write(fd, &req, sizeof(req));
  if (res != sizeof(req)) {
    sys_exit((i32)-res);
  }

  x11_connection_reply_t header = {0};
  res = sys_read(fd, &header, sizeof(header));
  if (res != sizeof(header)) {
    sys_exit((i32)-res);
  }

  if (!header.success) {
    sys_exit(1);
  }

  assert(header.length * (u64)sizeof(i32) < read_buffer_length);
  const u64 expected_read_length = header.length * (u64)sizeof(i32);
  res = sys_read(fd, read_buffer, expected_read_length);
  if (res != (i64)expected_read_length) {
    sys_exit(1);
  }

  // TODO: better deserializing.
  connection->setup = (x11_connection_setup_t *)read_buffer;
  void *p = read_buffer + sizeof(x11_connection_setup_t) +
            connection->setup->vendor_length;
  assert((u8 *)p < (u8 *)read_buffer + read_buffer_length);

  p += sizeof(x11_pixmap_format_t) * connection->setup->formats;
  assert((u8 *)p < (u8 *)read_buffer + read_buffer_length);

  connection->root = (x11_root_window_t *)p;
  p += sizeof(x11_root_window_t) * connection->setup->roots;
  assert((u8 *)p < (u8 *)read_buffer + read_buffer_length);
  connection->depth = (x11_depth_t *)p;
  connection->visual = (x11_visual_t *)p;
}

static u32 x11_generate_id(x11_connection_t const *conn) {
  static u32 id = 0;
  return ((conn->setup->id_mask & id++) | conn->setup->id_base);
}

static void x11_create_gc(i32 fd, u32 gc_id, u32 root_id, u32 font_id) {
  assert(fd > 0);
  assert(gc_id > 0);
  assert(root_id > 0);

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

  const i64 res = sys_write(fd, (const void *)packet, sizeof(packet));
  if (res != sizeof(packet)) {
    sys_exit(1);
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

  const i64 res = sys_write(fd, (const void *)packet, sizeof(packet));
  if (res != sizeof(packet)) {
    sys_exit((i32)-res);
  }

#undef CREATE_WINDOW_FLAG_COUNT
#undef CREATE_WINDOW_PACKET_U32_COUNT
}

static void x11_map_window(i32 fd, u32 window_id) {
  const u32 packet[2] = {
      [0] = X11_OP_REQ_MAP_WINDOW | (2 << 16), [1] = window_id};

  const i64 res = sys_write(fd, (const void *)packet, sizeof(packet));
  if (res != sizeof(packet)) {
    sys_exit((i32)-res);
  }
}

static void x11_query_text_extents(i32 fd, u32 font_id, const u8 *text,
                                   u32 text_byte_count, arena_t *arena) {
  const u32 padding = (4 - (2 * text_byte_count % 4)) % 4;
  const u32 packet_u32_count = 2 + ((2 * text_byte_count + padding) / 4);

  const u64 arena_offset = arena->current_offset;
  u32 *const packet = arena_alloc(arena, packet_u32_count);
  assert(packet != NULL);

  const bool is_odd_length = (padding == 2);

  packet[0] = X11_OP_REQ_QUERY_TEXT_EXTENTS | ((u32)is_odd_length << 8) |
              (packet_u32_count << 16);
  packet[1] = font_id;
  for (u32 i = 0; i < text_byte_count; i++) {
    packet[2 + 2 * i] = 0;
    packet[2 + 2 * i + 1] = text[i];
  }

  const i64 res = sys_write(fd, packet, packet_u32_count * 4);
  if (res != packet_u32_count * 4) {
    sys_exit(1);
  }

  u8 response[32] = {0};

  x11_read_response(fd, response, sizeof(response));

  arena_reset_at(arena, arena_offset);
}

static i32 spawn_shell(i32 num_lines, i32 num_columns, i32 *child_pid) {
  assert(num_lines > 0);
  assert(num_columns > 0);
  assert(child_pid != NULL);

  i32 master = 0, slave = 0;
  struct winsize winp = {.ws_row = num_lines, .ws_col = num_columns};
  openpty(&master, &slave, &winp);

  *child_pid = sys_fork();
  if (*child_pid == -1) {
    sys_exit(1);
  }

  if (*child_pid == 0) { // Child
    if (sys_dup2(slave, STDIN_FILENO) == -1) {
      sys_exit(1);
    }
    if (sys_dup2(slave, STDOUT_FILENO) == -1) {
      sys_exit(1);
    }
    if (sys_dup2(slave, STDERR_FILENO) == -1) {
      sys_exit(1);
    }

    // Create a new process group.
    if (sys_setsid() == -1) {
      sys_exit(1);
    }

    // Set controlling terminal.
    if (sys_ioctl3(slave, TIOCSCTTY, 0) != 0) {
      sys_exit(1);
    }

    // Close now unneeded file descriptors.
    sys_close(slave);
    sys_close(master);

    /* sys_signal(SIGCHLD, SIG_DFL); */
    /* sys_signal(SIGHUP, SIG_DFL); */
    /* sys_signal(SIGINT, SIG_DFL); */
    /* sys_signal(SIGQUIT, SIG_DFL); */
    /* sys_signal(SIGTERM, SIG_DFL); */
    /* sys_signal(SIGALRM, SIG_DFL); */

    char *const envp[] = {"HOME=/home/pg", "USER=pg", 0};
    if (sys_execle("/bin/sh", "/bin/sh", (char *)0, envp) == -1) {
      sys_exit(1);
    }

    __builtin_unreachable();
  }
  // Parent.

  sys_close(slave);
  set_fd_non_blocking(master);
  /* signal(SIGCHLD, sigchld); */

  return master;
}

i32 main() {
  const int width = 800;
  const int height = 600;
  const int font_size = 20; // FIXME: Get it from server.
  const int num_lines = height / font_size;
  const int num_columns = width / font_size;

  i32 child_pid = 0;
  const i32 master = spawn_shell(num_lines, num_columns, &child_pid);
  assert(child_pid > 0);
  assert(master > 0);

  arena_t arena = {0};
  arena_init(&arena, 1 << 20);

  i32 fd = sys_socket(AF_UNIX, SOCK_STREAM, 0);
  if (fd < 0) {
    eprint("Error opening socket");
    sys_exit(-fd);
  }
  assert(fd > 0);

  const sockaddr_un addr = {.sun_family = AF_UNIX,
                            .sun_path = "/tmp/.X11-unix/X0"};
  const i32 res = sys_connect(fd, &addr, sizeof(addr));
  if (res != 0) {
    eprint("Error connecting");
    sys_exit(-res);
  }

  const u64 read_buffer_length = 1 << 15;
  u8 *const read_buffer = arena_alloc(&arena, read_buffer_length);

  x11_connection_t connection = {0};
  x11_handshake(fd, &connection, read_buffer, read_buffer_length);

  const u32 gc_id = x11_generate_id(&connection);
  const u32 font_id = x11_generate_id(&connection);

  x11_open_font(fd, font_id);

  x11_create_gc(fd, gc_id, connection.root->id, font_id);

  const u32 window_id = x11_generate_id(&connection);
  assert(window_id > 0);

  const u16 x = 200, y = 200, w = 800, h = 600;
  x11_create_window(fd, window_id, connection.root->id, x, y, w, h,
                    connection.root->root_visual_id);

  x11_map_window(fd, window_id);

  x11_query_text_extents(fd, font_id, (const u8 *)"hello", 0, &arena);

  set_fd_non_blocking(fd);
  struct pollfd fds = {.fd = fd, .events = POLLIN};

  u8 *text = arena_alloc(&arena, 1 << 16);
  u64 text_len = 0;
  for (;;) {
    const i32 changed = sys_poll(&fds, 1, -1);
    if (changed == -1) {
      sys_exit(1);
    }
    assert(changed == 1);
    if ((fds.revents & POLLERR) || (fds.revents & POLLHUP)) {
      sys_exit(1);
    }
    assert(fds.revents & POLLIN);

    u8 buf[1024] = {0};
    const u64 read_byte_count = x11_read_response(fd, buf, sizeof(buf));
    assert(read_byte_count == 32);

    switch (buf[0]) {
    case X11_EVENT_EXPOSURE:
      break;
    case X11_EVENT_KEY_RELEASE:
      if (buf[1] == 9) // Escape.
        return 0;

      const char keysym = keycode_to_keysym[buf[1]];
      if (keysym != 0) {
        text[text_len++] = keysym;
        x11_draw_text(fd, window_id, gc_id, text, (u8)text_len, 10, 10, &arena);
      }
      break;
    }
  }
}
