#include <OpenGL/gl3.h>
#include <SDL2/SDL.h>
#include <_types/_uint64_t.h>
#include <stdbool.h>
#include <stdint.h>

int main(void) {

  if (SDL_Init(SDL_INIT_VIDEO) < 0) {
    SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Unable to initialize SDL: %s",
                 SDL_GetError());
    return 1;
  }

  SDL_LogSetAllPriority(SDL_LOG_PRIORITY_VERBOSE);

  // Version
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);

  // Double Buffer
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
  SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

  uint32_t flags = SDL_WINDOW_SHOWN | SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE;
  _Bool fullscreen = false;
  if (fullscreen) {
    flags |= SDL_WINDOW_MAXIMIZED;
  }

  const char window_title[] = "hello";
  const uint64_t window_width = 1024;
  const uint64_t window_height = 768;
  SDL_Window *window = SDL_CreateWindow(window_title, SDL_WINDOWPOS_CENTERED,
                                        SDL_WINDOWPOS_CENTERED, window_width,
                                        window_height, flags);

  if (!window) {
    SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Unable to create window: %s",
                 SDL_GetError());
    return 1;
  }

  SDL_GLContext gl_ctx = SDL_GL_CreateContext(window);
  if (!gl_ctx) {
    SDL_LogError(SDL_LOG_CATEGORY_ERROR,
                 "Unable to create context from window: %s", SDL_GetError());
    return 1;
  }

  glEnable(GL_DEPTH_TEST);
  glEnable(GL_CULL_FACE);
}
