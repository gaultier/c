#include <SDL.h>
#include <stdbool.h>
#include <stdint.h>

#include "conveyor0.rgb.h"
#include "conveyor1.rgb.h"
#include "conveyor2.rgb.h"
#include "conveyor3.rgb.h"
#include "conveyor4.rgb.h"
#include "conveyor5.rgb.h"
#include "conveyor6.rgb.h"
#include "conveyor7.rgb.h"

#define PG_ARRAY_LEN(x)(sizeof(x) / sizeof((x)[0]))

static SDL_Texture *load_texture(SDL_Renderer *renderer, uint16_t w, uint16_t h,
                                 const uint8_t *data) {
  SDL_Surface *surface = SDL_CreateRGBSurfaceFrom(
      (void *)data, w, h, 24, w * 3, 0x0000ff, 0x00ff00, 0xff0000, 0);
  SDL_assert(surface != 0);
  SDL_Texture *texture = SDL_CreateTextureFromSurface(renderer, surface);
  SDL_assert(texture != 0);
  SDL_FreeSurface(surface);

  return texture;
}

int main() {
  SDL_Window *window = SDL_CreateWindow("Sokoban", SDL_WINDOWPOS_UNDEFINED,
                                        SDL_WINDOWPOS_UNDEFINED, 600, 800, 0);
  if (!window)
    exit(1);

  SDL_Renderer *renderer =
      SDL_CreateRenderer(window, -1, SDL_RENDERER_PRESENTVSYNC);
  SDL_assert(renderer != 0);
  SDL_SetRenderDrawColor(renderer, 0xff, 0xff, 0xff, 0xff);

  const uint32_t belt_texture_w = 48;
  const uint32_t belt_texture_h = 16;
  SDL_Texture *belt_textures[] = {
      load_texture(renderer, belt_texture_w, belt_texture_h, conveyor0),
      load_texture(renderer, belt_texture_w, belt_texture_h, conveyor1),
      load_texture(renderer, belt_texture_w, belt_texture_h, conveyor2),
      load_texture(renderer, belt_texture_w, belt_texture_h, conveyor3),
      load_texture(renderer, belt_texture_w, belt_texture_h, conveyor4),
      load_texture(renderer, belt_texture_w, belt_texture_h, conveyor5),
      load_texture(renderer, belt_texture_w, belt_texture_h, conveyor6),
      load_texture(renderer, belt_texture_w, belt_texture_h, conveyor7),
  };
  uint8_t belt_current_texture = 0;
  const SDL_Rect belt_rect = {
      .w = belt_texture_w,
      .h = belt_texture_h,
      .x = 200,
      .y = 200,
  };

  const uint64_t desired_frame_rate = 60;
  while (true) {
    const uint64_t loop_start = SDL_GetTicks();

    SDL_Event e = {0};
    SDL_PollEvent(&e);
    if (e.type == SDL_QUIT) {
      exit(0);
    } else if (e.type == SDL_KEYDOWN) {
      switch (e.key.keysym.sym) {
      case SDLK_ESCAPE:
        exit(0);
        break;
      }
    }

    belt_current_texture =
        (belt_current_texture + 1) % PG_ARRAY_LEN(belt_textures);

    SDL_RenderClear(renderer);

    SDL_RenderCopy(renderer, belt_textures[belt_current_texture], 0,
                   &belt_rect);

    SDL_RenderPresent(renderer);

    const uint64_t loop_end = SDL_GetTicks();
    const uint64_t delta_time = loop_end - loop_start;

    if (delta_time < desired_frame_rate)
      SDL_Delay((uint32_t)(desired_frame_rate - delta_time));
  }

  return 0;
}
