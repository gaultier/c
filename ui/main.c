#include <SDL2/SDL.h>
#include <stdbool.h>
#include <stdio.h>

#include "SDL2/SDL_events.h"
#include "SDL2/SDL_mouse.h"
#include "SDL2/SDL_pixels.h"
#include "SDL2/SDL_rect.h"
#include "SDL2/SDL_render.h"
#include "SDL2/SDL_surface.h"
#include "SDL2/SDL_video.h"

typedef struct {
    int x, y, w, h;
    // TODO: color?
} button;

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

    SDL_Surface* window_surface = SDL_GetWindowSurface(window);
    SDL_Renderer* renderer = SDL_CreateSoftwareRenderer(window_surface);
    if (!renderer) {
        fprintf(stderr, "Failed to SDL_CreateRenderer: %s\n", SDL_GetError());
        return 1;
    }
    SDL_SetRenderDrawColor(renderer, 0xff, 0xff, 0xff, 0xff);
    SDL_RenderClear(renderer);

    bool running = true;
    while (running) {
        SDL_Event e;
        button b = {
            .x = (SCREEN_WIDTH - 100) / 2,
            .y = (SCREEN_HEIGHT - 20) / 2,
            .w = 100,
            .h = 20,
        };

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

        SDL_SetRenderDrawColor(renderer, 0xff, 0xff, 0xff, 0xff);
        SDL_RenderClear(renderer);
        SDL_Rect pos = {.x = b.x, .y = b.y, .w = b.w, .h = b.h};
        SDL_SetRenderDrawColor(renderer, 255, 255, 0, 0xFF);
        SDL_RenderFillRect(renderer, &pos);
        /* SDL_UpdateWindowSurfaceRects(window, &pos, 1); */
        SDL_UpdateWindowSurface(window);
    }
}
