// GC components: function, plane-mask, subwindow-mode, clip-x-origin,
// clip-y-origin, clip-mask
// GC mode-dependent components: foreground, background
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <unistd.h>

#define u64 uint64_t
#define i64 int64_t
#define u32 uint32_t
#define i32 int32_t
#define u16 uint16_t
#define i16 int16_t
#define u8 uint8_t
#define i8 int8_t

#include "crate_rgb.h"

// Taken from libX11
#define ROUNDUP(nbytes, pad) (((nbytes) + ((pad)-1)) & ~(long)((pad)-1))

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
#define X11_FLAG_GC_FG 0x00000004
#define X11_FLAG_GC_BG 0x00000008
#define X11_FLAG_GC_LINE_WIDTH 0x00000010
#define X11_FLAG_GC_LINE_STYLE 0x00000020
#define X11_FLAG_GC_FONT 0x00004000
#define X11_FLAG_GC_EXPOSE 0x00010000

#define X11_FLAG_WIN_BG_PIXMAP 0x00000001
#define X11_FLAG_WIN_BG_PIXEL 0x00000002
#define X11_FLAG_WIN_EVENT 0x00000800

#define X11_EVENT_FLAG_KEY_RELEASE 0x0002
#define X11_EVENT_FLAG_EXPOSURE 0x8000

#define X11_EVENT_KEY_RELEASE 0x3
#define X11_EVENT_EXPOSURE 0xc

#define MY_COLOR_ARGB 0x00ff00ff

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
                          u8 text_byte_count, u16 x, u16 y) {
  pg_assert(fd > 0);
  pg_assert(text != NULL);

  const u32 padding = (4 - (text_byte_count % 4)) % 4;
  const u32 packet_u32_count = 4 + ((text_byte_count + padding) / 4);

  u32 packet[256] = {0};
  pg_assert(packet != NULL);

  packet[0] = X11_OP_REQ_IMAGE_TEXT8 | ((u32)text_byte_count << 8) |
              (packet_u32_count << 16);
  packet[1] = window_id;
  packet[2] = gc_id;
  packet[3] = (u32)x | ((u32)y << 16);

  pg_assert(ROUNDUP(text_byte_count, 4) >= text_byte_count);
  pg_assert((4 * sizeof(u32) + ROUNDUP(text_byte_count, 4)) < sizeof(packet));
  __builtin_memcpy(&packet[4], text, text_byte_count);

  const i64 res = write(fd, packet, packet_u32_count * 4);
  if (res != packet_u32_count * 4) {
    exit(1);
  }
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

  for (uint64_t i = 0; i < connection->setup->formats; i++) {
    p += sizeof(x11_pixmap_format_t);
  }
  pg_assert((u8 *)p < (u8 *)read_buffer + read_buffer_length);

  connection->root = (x11_root_window_t *)p;
  for (uint64_t i = 0; i < connection->setup->roots; i++) {
    p += sizeof(x11_root_window_t) * connection->setup->roots;
  }
  pg_assert((u8 *)p < (u8 *)read_buffer + read_buffer_length);
  connection->depth = (x11_depth_t *)p;
  connection->visual = (x11_visual_t *)p;
}

static u32 x11_generate_id(x11_connection_t const *conn) {
  static u32 id = 0;
  return ((conn->setup->id_mask & id++) | conn->setup->id_base);
}

static void x11_create_gc(i32 fd, u32 gc_id, u32 root_id) {
  pg_assert(fd > 0);
  pg_assert(gc_id > 0);
  pg_assert(root_id > 0);

  const u32 flags = X11_FLAG_GC_BG;

#define CREATE_GC_FLAG_COUNT 1
#define CREATE_GC_PACKET_U32_COUNT (4 + CREATE_GC_FLAG_COUNT)

  const u32 packet[CREATE_GC_PACKET_U32_COUNT] = {
      [0] = X11_OP_REQ_CREATE_GC | (CREATE_GC_PACKET_U32_COUNT << 16),
      [1] = gc_id,
      [2] = root_id,
      [3] = flags,
      [4] = 0x0000ff00,
  };

  const i64 res = write(fd, (const void *)packet, sizeof(packet));
  if (res != sizeof(packet)) {
    exit(1);
  }

#undef CREATE_GC_PACKET_U32_COUNT
}

static void x11_create_window(i32 fd, u32 window_id, u32 root_id, u16 x, u16 y,
                              u16 w, u16 h, u32 root_visual_id) {

  const u32 flags = X11_FLAG_WIN_BG_PIXEL | X11_FLAG_WIN_EVENT;
#define CREATE_WINDOW_FLAG_COUNT 2
#define CREATE_WINDOW_PACKET_U32_COUNT (8 + CREATE_WINDOW_FLAG_COUNT)

  const u8 depth = 24;
  const u16 border = 0, group = 1;

  const u32 packet[CREATE_WINDOW_PACKET_U32_COUNT] = {
      [0] = X11_OP_REQ_CREATE_WINDOW | (depth << 8) |
            (CREATE_WINDOW_PACKET_U32_COUNT << 16),
      [1] = window_id,
      [2] = root_id,
      [3] = x | ((u32)y << 16),
      [4] = w | ((u32)h << 16),
      [5] = group | (border << 16),
      [6] = root_visual_id,
      [7] = flags,
      [8] = 0x00ffff00,
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

static void x11_create_pixmap(i32 fd, u32 window_id, u32 pixmap_id, u16 w,
                              u16 h) {
  const u8 X11_OP_REQ_CREATE_PIXMAP = 53;
  const u8 depth = 24; // RGBA
  const u8 packet_length = 4;
  const u32 packet[4] = {
      [0] = X11_OP_REQ_CREATE_PIXMAP | (depth << 8) | (packet_length << 16),
      [1] = pixmap_id,
      [2] = window_id,
      [3] = w | (h << 16),
  };

  const i64 res = write(fd, (const void *)packet, sizeof(packet));
  if (res != sizeof(packet)) {
    exit(1);
  }
}

static void x11_put_image(i32 fd, u32 gc_id, u8 *image_data,
                          u32 image_data_byte_count, u16 w, u16 h, u16 x, u16 y,
                          u32 pixmap_id) {
  pg_assert(image_data != NULL);

  const u8 X11_OP_REQ_PUT_IMAGE = 72;
  const u8 X11_PUT_IMAGE_FORMAT_ZPIXMAP = 2;
  const u8 depth = 24; // RGBA
  const u8 left_pad = 0;

  // Shortcut. Sue me!
  pg_assert(w == 34);
  pg_assert(h == 34);
  pg_assert(image_data_byte_count == w * h * (depth / 8));
  const u8 padding = 0;

  const u32 packet_length = 6 + (image_data_byte_count + padding) / 4;

  u32 packet[2048] = {
      [0] = X11_OP_REQ_PUT_IMAGE | (X11_PUT_IMAGE_FORMAT_ZPIXMAP << 8) |
            (packet_length << 16),
      [1] = pixmap_id,
      [2] = gc_id,
      [3] = w | (h << 16),
      [4] = x | (y << 16),
      [5] = left_pad | (depth << 8),
  };
  pg_assert(sizeof(packet) >= packet_length);
  __builtin_memcpy(packet + 6, image_data, image_data_byte_count);

  const i64 res = write(fd, (const void *)packet, packet_length);
  if (res != packet_length) {
    exit(1);
  }
}

static void x11_copy_area(i32 fd, u32 gc_id, u32 dst_id, u32 src_id, u16 src_x,
                          u16 src_y, u16 w, u16 h, u16 dst_x, u16 dst_y) {
  const u8 X11_OP_REQ_COPY_AREA = 62;
  const u8 packet_length = 28;
  const u8 request_length = 7;

  const u32 packet[28] = {
      [0] = X11_OP_REQ_COPY_AREA | (request_length << 16),
      [1] = src_id,
      [2] = dst_id,
      [3] = gc_id,
      [4] = src_x | (src_y << 16),
      [5] = dst_x | (dst_y << 16),
      [6] = w | (h << 16),
  };

  const i64 res = write(fd, (const void *)packet, packet_length);
  if (res != packet_length) {
    exit(1);
  }
}

i32 main() {
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

  u8 read_buffer[1 << 16] = {0};
  x11_connection_t connection = {0};
  x11_handshake(x11_socket_fd, &connection, read_buffer, sizeof(read_buffer));

  const u32 gc_id = x11_generate_id(&connection);
  /* const u32 font_id = x11_generate_id(&connection); */

  // x11_open_font(x11_socket_fd, font_id);
  //

  const u32 window_id = x11_generate_id(&connection);
  pg_assert(window_id > 0);

  const u16 x = 200, y = 200, w = 800, h = 600;
  x11_create_window(x11_socket_fd, window_id, connection.root->id, x, y, w, h,
                    connection.root->root_visual_id);
  x11_create_gc(x11_socket_fd, gc_id, connection.root->id);

  x11_map_window(x11_socket_fd, window_id);

  // const u32 pixmap_id = x11_generate_id(&connection);
  // x11_create_pixmap(x11_socket_fd, window_id, pixmap_id, 34, 34);
  //  x11_put_image(x11_socket_fd, gc_id, crate_rgb, crate_rgb_len, 34, 34, 0,
  //  0,
  //               pixmap_id);

  for (;;) {
    u8 read_buffer[1024] = {0};
    const i64 read_byte_count =
        read(x11_socket_fd, read_buffer, sizeof(read_buffer));
    pg_assert(read_byte_count >= 0);

    pg_assert(read_byte_count >= 32);

    switch (read_buffer[0]) {
    case 0:
      eprint("X11 server error");
      exit(1);
    case X11_EVENT_EXPOSURE:
      //     x11_draw_text(x11_socket_fd, window_id, gc_id,
      //                   (const u8 *)"Hello world !", 12, 50, 50);
      //    x11_copy_area(x11_socket_fd, gc_id, window_id, pixmap_id, 0, 0, 34,
      //    34,
      //                  100, 100);
      x11_put_image(x11_socket_fd, gc_id, crate_rgb, crate_rgb_len, 34, 34, 0,
                    0, window_id);
      break;
    case X11_EVENT_KEY_RELEASE: {
      /* const u8 keycode = read_buffer[1]; */
      break;
    }
    }
  }
}
