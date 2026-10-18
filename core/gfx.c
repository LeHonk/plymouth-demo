/* gfx.c - tiny software rasteriser for 32-bit ARGB surfaces */
#include "gfx.h"

#include <stdlib.h>
#include <string.h>

surface_t *
surface_new (int w, int h)
{
        surface_t *s = calloc (1, sizeof(*s));

        if (s == NULL)
                return NULL;
        s->w = w;
        s->h = h;
        s->pitch = w;
        s->px = calloc ((size_t) w * h, sizeof(uint32_t));
        if (s->px == NULL) {
                free (s);
                return NULL;
        }
        return s;
}

void
surface_free (surface_t *s)
{
        if (s == NULL)
                return;
        free (s->px);
        free (s);
}

void
gfx_clear (surface_t *s, uint32_t c)
{
        for (int y = 0; y < s->h; y++) {
                uint32_t *row = s->px + y * s->pitch;
                for (int x = 0; x < s->w; x++)
                        row[x] = c;
        }
}

void
gfx_hline (surface_t *s, int x0, int x1, int y, uint32_t c)
{
        if ((unsigned) y >= (unsigned) s->h)
                return;
        if (x0 > x1) {
                int t = x0;
                x0 = x1;
                x1 = t;
        }
        if (x0 < 0)
                x0 = 0;
        if (x1 >= s->w)
                x1 = s->w - 1;
        uint32_t *row = s->px + y * s->pitch;
        for (int x = x0; x <= x1; x++)
                row[x] = c;
}

void
gfx_vline (surface_t *s, int x, int y0, int y1, uint32_t c)
{
        if ((unsigned) x >= (unsigned) s->w)
                return;
        if (y0 > y1) {
                int t = y0;
                y0 = y1;
                y1 = t;
        }
        if (y0 < 0)
                y0 = 0;
        if (y1 >= s->h)
                y1 = s->h - 1;
        for (int y = y0; y <= y1; y++)
                s->px[y * s->pitch + x] = c;
}

void
gfx_rect (surface_t *s, int x, int y, int w, int h, uint32_t c)
{
        for (int yy = y; yy < y + h; yy++)
                gfx_hline (s, x, x + w - 1, yy, c);
}

void
gfx_line (surface_t *s, int x0, int y0, int x1, int y1, uint32_t c)
{
        int dx = abs (x1 - x0), sx = x0 < x1 ? 1 : -1;
        int dy = -abs (y1 - y0), sy = y0 < y1 ? 1 : -1;
        int err = dx + dy;

        for (;;) {
                gfx_pset (s, x0, y0, c);
                if (x0 == x1 && y0 == y1)
                        break;
                int e2 = 2 * err;
                if (e2 >= dy) {
                        err += dy;
                        x0 += sx;
                }
                if (e2 <= dx) {
                        err += dx;
                        y0 += sy;
                }
        }
}

void
gfx_blit (surface_t *dst, const surface_t *src, int x, int y)
{
        for (int sy = 0; sy < src->h; sy++) {
                int dy = y + sy;
                if ((unsigned) dy >= (unsigned) dst->h)
                        continue;
                for (int sx = 0; sx < src->w; sx++) {
                        int dx = x + sx;
                        uint32_t c = src->px[sy * src->pitch + sx];
                        if ((unsigned) dx < (unsigned) dst->w && (c >> 24) != 0)
                                dst->px[dy * dst->pitch + dx] = c;
                }
        }
}

uint32_t
gfx_mix (uint32_t a, uint32_t b, int t)
{
        int r = RGB_R (a) + ((RGB_R (b) - (int) RGB_R (a)) * t) / 255;
        int g = RGB_G (a) + ((RGB_G (b) - (int) RGB_G (a)) * t) / 255;
        int bl = RGB_B (a) + ((RGB_B (b) - (int) RGB_B (a)) * t) / 255;

        return RGB (r, g, bl);
}

uint32_t
gfx_add (uint32_t a, uint32_t b)
{
        int r = RGB_R (a) + RGB_R (b);
        int g = RGB_G (a) + RGB_G (b);
        int bl = RGB_B (a) + RGB_B (b);

        return RGB (r > 255 ? 255 : r, g > 255 ? 255 : g, bl > 255 ? 255 : bl);
}

void
gfx_palette_ramp (palette_t pal, int from, int to, uint32_t c0, uint32_t c1)
{
        int n = to - from;

        for (int i = from; i <= to; i++)
                pal[i & 0xff] = n > 0 ? gfx_mix (c0, c1, (i - from) * 255 / n) : c0;
}

void
gfx_indexed_to_surface (surface_t *s, const uint8_t *idx, const palette_t pal)
{
        for (int y = 0; y < s->h; y++) {
                uint32_t *row = s->px + y * s->pitch;
                const uint8_t *in = idx + y * s->w;
                for (int x = 0; x < s->w; x++)
                        row[x] = pal[in[x]];
        }
}

void
gfx_scale_to (uint32_t *dst, int dst_w, int dst_h, int dst_pitch, const surface_t *src)
{
        int sx = dst_w / src->w, sy = dst_h / src->h;
        int scale = sx < sy ? sx : sy;

        if (scale < 1) {
                /* Display smaller than render surface: nearest-neighbour shrink */
                for (int y = 0; y < dst_h; y++) {
                        const uint32_t *in = src->px + (y * src->h / dst_h) * src->pitch;
                        uint32_t *out = dst + y * dst_pitch;
                        for (int x = 0; x < dst_w; x++)
                                out[x] = in[x * src->w / dst_w];
                }
                return;
        }

        int ox = (dst_w - src->w * scale) / 2;
        int oy = (dst_h - src->h * scale) / 2;

        for (int y = 0; y < dst_h; y++) {
                uint32_t *out = dst + y * dst_pitch;
                int syy = (y - oy) / scale;
                if (y < oy || syy >= src->h) {
                        memset (out, 0, dst_w * sizeof(uint32_t));
                        continue;
                }
                /* Rows that repeat the previous source row are copied */
                if (y > oy && (y - oy) % scale != 0) {
                        memcpy (out, out - dst_pitch, dst_w * sizeof(uint32_t));
                        continue;
                }
                const uint32_t *in = src->px + syy * src->pitch;
                int x = 0;
                for (; x < ox; x++)
                        out[x] = 0xff000000u;
                for (int i = 0; i < src->w; i++) {
                        uint32_t c = in[i] | 0xff000000u;
                        for (int k = 0; k < scale; k++)
                                out[x++] = c;
                }
                for (; x < dst_w; x++)
                        out[x] = 0xff000000u;
        }
}
