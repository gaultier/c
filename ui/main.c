#include <SDL2/SDL.h>
#include <stdbool.h>
#include <stdio.h>

#include "SDL2/SDL_events.h"
#include "SDL2/SDL_mouse.h"
#include "SDL2/SDL_pixels.h"
#include "SDL2/SDL_rect.h"
#include "SDL2/SDL_render.h"
#include "SDL2/SDL_surface.h"

typedef struct {
    int x, y, w, h;
    SDL_Texture* texture;
    // TODO: color?
} button;

void make_button_texture(SDL_Renderer* renderer, button* b) {
    SDL_Surface* surface = SDL_CreateRGBSurface(0, b->w, b->h, 32, 0, 0, 0, 0);
    SDL_FillRect(surface, NULL, SDL_MapRGB(surface->format, 255, 255, 0));
    b->texture = SDL_CreateTextureFromSurface(renderer, surface);
    SDL_FreeSurface(surface);
}

int main(int argc, const char* argv[]) {
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        fprintf(stderr, "Failed to SDL_Init: %s\n", SDL_GetError());
        return 1;
    }

    const uint16_t SCREEN_WIDTH = 800;
    const uint16_t SCREEN_HEIGHT = 600;

    SDL_Window* window = SDL_CreateWindow("Sokoban", SDL_WINDOWPOS_UNDEFINED,
                                          SDL_WINDOWPOS_UNDEFINED, SCREEN_WIDTH,
                                          SCREEN_HEIGHT, 0);
    if (!window) {
        fprintf(stderr, "Failed to SDL_CreateWindow: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Renderer* renderer =
        SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (!renderer) {
        fprintf(stderr, "Failed to SDL_CreateRenderer: %s\n", SDL_GetError());
        return 1;
    }
    SDL_SetRenderDrawColor(renderer, 0xff, 0xff, 0xff, 0xff);

    button b = {
        .x = (SCREEN_WIDTH - 100) / 2,
        .y = (SCREEN_HEIGHT - 20) / 2,
        .w = 100,
        .h = 20,
    };
    make_button_texture(renderer, &b);

    bool running = true;
    while (running) {
        SDL_Event e;
        SDL_WaitEvent(&e);
        if (e.type == SDL_QUIT)
            running = false;
        else if (e.type == SDL_KEYDOWN) {
            switch (e.key.keysym.sym) {
                case SDLK_ESCAPE:
                    running = false;
                    break;
            }
        } else if (e.type == SDL_MOUSEBUTTONDOWN) {
            int x, y;
            SDL_GetMouseState(&x, &y);
            printf("x=%d y=%d\n", x, y);
            __builtin_dump_struct(&b, &printf);
            if (b.x <= x && x < b.x + b.w && b.y <= y && y < b.y + b.h) {
                printf("Clicked\n");
            }
        }
        SDL_RenderClear(renderer);
        SDL_Rect pos = {.x = b.x, .y = b.y, .w = b.w, .h = b.h};
        SDL_RenderCopy(renderer, b.texture, NULL, &pos);
        SDL_RenderPresent(renderer);
    }
}
