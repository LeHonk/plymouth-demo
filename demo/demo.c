/* demo.c - example effects: plasma, starfield, copper bars, sine scroller
 *
 * Replace this file in your fork. Everything here only uses core/demo.h,
 * so it runs unchanged inside plymouthd and in the SDL previewer.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "demo.h"

#define NUM_STARS 160

typedef struct
{
        int x, y, z;            /* 16.16-ish world coordinates */
} star_t;

typedef struct
{
        palette_t plasma_pal;
        star_t    stars[NUM_STARS];
        uint32_t  rng;
        char      scroll_text[512];
} state_t;

static const char *greetings =
        "   *** PLYMOUTH DEMO BASE ***   GREETINGS TO ALL SCENERS BOOTING LINUX ON REAL HARDWARE ...   "
        "FORK ME, REPLACE DEMO/DEMO.C AND MAKE YOUR OWN INTRO ...   ";

static void
star_reset (state_t *s, star_t *st, bool anywhere)
{
        st->x = (int) (drand (&s->rng) % 2000) - 1000;
        st->y = (int) (drand (&s->rng) % 2000) - 1000;
        st->z = anywhere ? (int) (drand (&s->rng) % 1000) + 1 : 1000;
}

void *
demo_init (const demo_ctx_t *ctx)
{
        state_t *s = calloc (1, sizeof(*s));

        (void) ctx;
        s->rng = 0x1234567u;
        for (int i = 0; i < NUM_STARS; i++)
                star_reset (s, &s->stars[i], true);

        /* deep blue -> magenta -> orange -> back, a classic plasma palette */
        gfx_palette_ramp (s->plasma_pal, 0, 63, RGB (0, 0, 40), RGB (120, 0, 140));
        gfx_palette_ramp (s->plasma_pal, 64, 127, RGB (120, 0, 140), RGB (255, 120, 0));
        gfx_palette_ramp (s->plasma_pal, 128, 191, RGB (255, 120, 0), RGB (20, 40, 120));
        gfx_palette_ramp (s->plasma_pal, 192, 255, RGB (20, 40, 120), RGB (0, 0, 40));
        return s;
}

static void
draw_plasma (state_t *s, surface_t *fb, int t)
{
        for (int y = 0; y < fb->h; y++) {
                uint32_t *row = fb->px + y * fb->pitch;
                int ys = dsin (y * 3 + t * 2) + dsin (y * 2 - t * 3);
                for (int x = 0; x < fb->w; x++) {
                        int v = ys + dsin (x * 4 + t * 5) + dsin ((x + y) * 2 + t);
                        /* v in about -131068..131068 -> palette index */
                        row[x] = s->plasma_pal[((v >> 10) + t) & 0xff];
                }
        }
}

static void
draw_stars (state_t *s, surface_t *fb)
{
        int cx = fb->w / 2, cy = fb->h / 2;

        for (int i = 0; i < NUM_STARS; i++) {
                star_t *st = &s->stars[i];
                st->z -= 8;
                if (st->z <= 0)
                        star_reset (s, st, false);
                int sx = cx + st->x * 128 / st->z;
                int sy = cy + st->y * 128 / st->z;
                if (sx < 0 || sy < 0 || sx >= fb->w || sy >= fb->h) {
                        star_reset (s, st, false);
                        continue;
                }
                int b = 255 - st->z / 4;
                gfx_pset (fb, sx, sy, RGB (b, b, b));
        }
}

static void
draw_copper_bars (surface_t *fb, int t)
{
        static const uint32_t colours[] = {
                RGB (255, 40, 40), RGB (40, 255, 40), RGB (60, 120, 255), RGB (255, 220, 40),
        };
        const int bar_h = 16;

        for (int i = 0; i < 4; i++) {
                int centre = fb->h / 3 + (dsin (t * 4 + i * 90) * (fb->h / 4)) / 32767;
                for (int k = -bar_h / 2; k < bar_h / 2; k++) {
                        int shade = 255 - abs (k) * 255 / (bar_h / 2);
                        uint32_t c = gfx_mix (RGB (0, 0, 0), colours[i], shade);
                        int y = centre + k;
                        if ((unsigned) y >= (unsigned) fb->h)
                                continue;
                        uint32_t *row = fb->px + y * fb->pitch;
                        for (int x = 0; x < fb->w; x++)
                                row[x] = gfx_add (gfx_mix (row[x], RGB (0, 0, 0), 160), c);
                }
        }
}

static void
draw_scroller (state_t *s, const demo_ctx_t *ctx, surface_t *fb)
{
        const int scale = 2, cw = 8 * scale;
        int len = (int) strlen (s->scroll_text);
        int scroll = (int) (ctx->t * 90.0);      /* pixels per second */
        int first = scroll / cw, offset = scroll % cw;
        int base_y = fb->h - 48;

        for (int i = 0; i <= fb->w / cw + 1; i++) {
                char ch[2] = { s->scroll_text[(first + i) % len], '\0' };
                int x = i * cw - offset;
                int y = base_y + dsin (x * 3 + (int) (ctx->t * 300)) * 10 / 32767;
                uint32_t c = RGB (255, 255 - (y - base_y + 10) * 8, 80);
                font_draw (fb, x + 1, y + 1, ch, scale, RGB (0, 0, 0));
                font_draw (fb, x, y, ch, scale, c);
        }
}

static void
draw_progress (const demo_ctx_t *ctx, surface_t *fb)
{
        int w = (int) ((fb->w - 40) * ctx->boot_progress);

        gfx_rect (fb, 20, fb->h - 12, fb->w - 40, 4, RGB (30, 30, 60));
        gfx_rect (fb, 20, fb->h - 12, w, 4, RGB (120, 200, 255));
}

void
demo_frame (void *state, const demo_ctx_t *ctx, surface_t *fb)
{
        state_t *s = state;
        int t = (int) (ctx->t * 50.0);          /* animation clock in "frames" */

        /* Boot status from plymouth becomes part of the scroller text */
        snprintf (s->scroll_text, sizeof(s->scroll_text), "%s%s   ",
                  greetings, ctx->message ? ctx->message : (ctx->status ? ctx->status : ""));

        draw_plasma (s, fb, t);
        draw_copper_bars (fb, t);
        draw_stars (s, fb);

        const char *title = ctx->mode == DEMO_MODE_BOOT ? "BOOTING" :
                            ctx->mode == DEMO_MODE_UPDATES ? "UPDATING" : "SHUTTING DOWN";
        font_draw_centered (fb, 16, title, 3, RGB (255, 255, 255));

        draw_scroller (s, ctx, fb);
        draw_progress (ctx, fb);
}

void
demo_key (void *state, const char *key)
{
        (void) state;
        (void) key;
}

void
demo_shutdown (void *state)
{
        free (state);
}
