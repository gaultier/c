#include <OpenGL/gl3.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_video.h>
#include <stdbool.h>
#include <stdint.h>

static SDL_Window *window = NULL;
static SDL_GLContext gl_ctx = NULL;

static void init(void) {
  if (SDL_Init(SDL_INIT_VIDEO) < 0) {
    SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Unable to initialize SDL: %s",
                 SDL_GetError());
    exit(1);
  }

  SDL_LogSetAllPriority(SDL_LOG_PRIORITY_VERBOSE);

  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,
                      1); // Max supported on macos
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
  SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

  const uint32_t flags =
      SDL_WINDOW_SHOWN | SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE;

  const char window_title[] = "hello";
  const uint64_t window_width = 1024;
  const uint64_t window_height = 768;
  window = SDL_CreateWindow(window_title, SDL_WINDOWPOS_CENTERED,
                            SDL_WINDOWPOS_CENTERED, window_width, window_height,
                            flags);

  if (!window) {
    SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Unable to create window: %s",
                 SDL_GetError());
    exit(1);
  }

  gl_ctx = SDL_GL_CreateContext(window);
  if (!gl_ctx) {
    SDL_LogError(SDL_LOG_CATEGORY_ERROR,
                 "Unable to create context from window: %s", SDL_GetError());
    exit(1);
  }

  glEnable(GL_DEPTH_TEST);
  glEnable(GL_CULL_FACE);
}

static void loop(void) {
  SDL_Event event;

  // SDL_SetRelativeMouseMode(SDL_FALSE);

  const uint64_t fps_desired = 60;
  uint64_t frame_rate = 1000 / fps_desired;

  uint64_t start = 0, end = 0, delta_time = 1;

  while (true) {
    start = SDL_GetTicks();

    // Input
    while (SDL_PollEvent(&event)) {
      switch (event.type) {
      case SDL_QUIT:
        return;
      case SDL_KEYDOWN:
        switch (event.key.keysym.scancode) {
        case SDL_SCANCODE_ESCAPE:
          return;
        default:
          break;
        }
        break;
      default:
        break;
      }
    }
    //
    // Rendering
    //
    glClearColor(100, 200, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    // glUseProgram(program_id);

    SDL_GL_SwapWindow(window);

    end = SDL_GetTicks();
    delta_time = end - start;

    if (delta_time < frame_rate)
      SDL_Delay((uint32_t)(frame_rate - delta_time));
  }
}

static GLuint initTriangle(void) {
  
}

int main(void) {
  init();
  loop();
}
