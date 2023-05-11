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

static i64 sys_write(i32 fd, void *data, u64 len) {
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

#define X11_OP_REQ_CREATE_WINDOW 0x01
#define X11_OP_REQ_MAP_WINDOW 0x08
#define X11_OP_REQ_CREATE_PIX 0x35
#define X11_OP_REQ_CREATE_GC 0x37
#define X11_OP_REQ_PUT_IMG 0x48

#define X11_FLAG_GC_FUNC 0x00000001
#define X11_FLAG_GC_PLANE 0x00000002
#define X11_FLAG_GC_BG 0x00000004
#define X11_FLAG_GC_FG 0x00000008
#define X11_FLAG_GC_LINE_WIDTH 0x00000010
#define X11_FLAG_GC_LINE_STYLE 0x00000020
#define X11_FLAG_GC_FONT 0x00004000
#define X11_FLAG_GC_EXPOSE 0x00010000

#define MY_COLOR_ARGB 0x00aa00ff

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

static void *arena_alloc(arena_t *arena, u64 len) {
  assert(arena != NULL);
  assert(arena->current_offset < arena->capacity);
  assert(arena->current_offset + len < arena->capacity);

  // TODO: align?
  arena->current_offset += len;
  return arena->base + arena->current_offset - len;
}

static void x11_handshake(i32 fd, x11_connection_t *connection, u8 *read_buffer,
                          u64 read_buffer_length) {
  assert(fd > 0);
  assert(connection != NULL);
  assert(read_buffer != NULL);
  assert(read_buffer_length > 0);

  x11_connection_req_t req = {.order = 'l', .major = 11};

  i32 res = sys_write(fd, &req, sizeof(req));
  if (res != sizeof(req)) {
    sys_exit(-res);
  }

  x11_connection_reply_t header = {0};
  res = sys_read(fd, &header, sizeof(header));
  if (res != sizeof(header)) {
    sys_exit(-res);
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

static void x11_create_gc(i32 fd, u32 gc_id, u32 root_id) {
  assert(fd > 0);
  assert(gc_id > 0);
  assert(root_id > 0);

  const u32 flags = X11_FLAG_GC_BG | X11_FLAG_GC_EXPOSE;

#define CREATE_GC_PACKET_U32_COUNT (4 + 2)

  const u32 packet[CREATE_GC_PACKET_U32_COUNT] = {
      [0] = X11_OP_REQ_CREATE_GC | (CREATE_GC_PACKET_U32_COUNT << 16),
      [1] = gc_id,
      [2] = root_id,
      [3] = flags,
      [4] = MY_COLOR_ARGB,
      [5] = 0,
  };

  const int res = sys_write(fd, (void *)packet, sizeof(packet));
  if (res != sizeof(packet)) {
    sys_exit(1);
  }
}

int main() {

  arena_t arena = {0};
  arena_init(&arena, 1 << 16);

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

  x11_create_gc(fd, gc_id, connection.root->id);

  sys_exit(0);
}
