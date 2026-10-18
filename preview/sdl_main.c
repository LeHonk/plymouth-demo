/* sdl_main.c - run the demo in an SDL window, without Plymouth
 *
 * The fastest edit/run loop: same demo/ code and core gfx, with a fake boot
 * that ramps progress over 20 s and cycles status messages.
 *   Esc/q: quit   p: toggle password prompt (overlay)   f: fullscreen
 */
#include <stdio.h>
#include <string.h>

#include <SDL.h>

#include "demo.h"
#include "overlay.h"

#ifndef RENDER_W
#define RENDER_W 320
#endif
#ifndef RENDER_H
#define RENDER_H 200
#endif
#ifndef FPS
#define FPS 50
#endif

int
main (int argc, char **argv)
{
        static const char *messages[] = {
                "Mounting filesystems", "Loading kernel modules", "Starting udev",
                "Bringing up network", "Starting display manager",
        };
        int w = RENDER_W > 0 ? RENDER_W : 640, h = RENDER_H > 0 ? RENDER_H : 400;
        int win_w = 1280, win_h = 800;

        (void) argc; (void) argv;

        if (SDL_Init (SDL_INIT_VIDEO) != 0) {
                fprintf (stderr, "SDL_Init: %s\n", SDL_GetError ());
                return 1;
        }
        SDL_Window *win = SDL_CreateWindow ("plymouth demo preview", SDL_WINDOWPOS_CENTERED,
                                            SDL_WINDOWPOS_CENTERED, win_w, win_h, SDL_WINDOW_RESIZABLE);
        SDL_Renderer *ren = SDL_CreateRenderer (win, -1, SDL_RENDERER_PRESENTVSYNC);
        SDL_Texture *tex = NULL;
        surface_t *screen = NULL;
        surface_t *fb = surface_new (w, h);
        overlay_t overlay = { 0 };

        dmath_init ();
        demo_ctx_t ctx = { .width = w, .height = h, .theme_dir = "assets", .mode = DEMO_MODE_BOOT };
        void *state = demo_init (&ctx);
        Uint64 start = SDL_GetPerformanceCounter ();
        bool quit = false;

        while (!quit) {
                Uint32 frame_start = SDL_GetTicks ();
                SDL_Event ev;

                while (SDL_PollEvent (&ev)) {
                        if (ev.type == SDL_QUIT)
                                quit = true;
                        else if (ev.type == SDL_TEXTINPUT)
                                demo_key (state, ev.text.text);
                        else if (ev.type == SDL_KEYDOWN) {
                                SDL_Keycode k = ev.key.keysym.sym;
                                if (k == SDLK_ESCAPE || k == SDLK_q)
                                        quit = true;
                                else if (k == SDLK_p && overlay.active)
                                        overlay_hide (&overlay);
                                else if (k == SDLK_p)
                                        overlay_password (&overlay, "Please unlock disk sda3_crypt", 0);
                                else if (k == SDLK_f)
                                        SDL_SetWindowFullscreen (win, SDL_GetWindowFlags (win) & SDL_WINDOW_FULLSCREEN_DESKTOP ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
                                else if (overlay.active && k == SDLK_BACKSPACE && overlay.bullets > 0)
                                        overlay.bullets--;
                                else if (overlay.active && k < 128 && k > ' ')
                                        overlay.bullets++;
                        }
                }

                ctx.t = (double) (SDL_GetPerformanceCounter () - start) / SDL_GetPerformanceFrequency ();
                ctx.boot_progress = ctx.t < 20.0 ? ctx.t / 20.0 : 1.0;
                ctx.message = messages[(int) ctx.t % (int) (sizeof(messages) / sizeof(*messages))];
                ctx.status = ctx.message;

                demo_frame (state, &ctx, fb);
                overlay_draw (&overlay, fb, ctx.t);
                ctx.frame++;

                /* Same integer scaling + letterbox as the plugin */
                SDL_GetRendererOutputSize (ren, &win_w, &win_h);
                if (screen == NULL || screen->w != win_w || screen->h != win_h) {
                        surface_free (screen);
                        if (tex)
                                SDL_DestroyTexture (tex);
                        screen = surface_new (win_w, win_h);
                        tex = SDL_CreateTexture (ren, SDL_PIXELFORMAT_ARGB8888,
                                                 SDL_TEXTUREACCESS_STREAMING, win_w, win_h);
                }
                gfx_scale_to (screen->px, screen->w, screen->h, screen->pitch, fb);
                SDL_UpdateTexture (tex, NULL, screen->px, screen->pitch * 4);
                SDL_RenderCopy (ren, tex, NULL, NULL);
                SDL_RenderPresent (ren);

                Uint32 spent = SDL_GetTicks () - frame_start;
                if (spent < 1000 / FPS)
                        SDL_Delay (1000 / FPS - spent);
        }

        demo_shutdown (state);
        surface_free (fb);
        surface_free (screen);
        SDL_DestroyTexture (tex);
        SDL_DestroyRenderer (ren);
        SDL_DestroyWindow (win);
        SDL_Quit ();
        return 0;
}
