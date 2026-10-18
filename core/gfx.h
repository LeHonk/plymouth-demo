/* gfx.h - tiny software rasteriser for 32-bit ARGB surfaces */
#ifndef GFX_H
#define GFX_H

#include <stdint.h>
#include <stdbool.h>

typedef struct
{
        int       w, h;
        int       pitch;        /* in pixels */
        uint32_t *px;
} surface_t;

/* 256-entry palette for classic 8-bit indexed effects */
typedef uint32_t palette_t[256];

#define RGB(r, g, b) (0xff000000u | ((uint32_t) (r) << 16) | ((uint32_t) (g) << 8) | (uint32_t) (b))
#define RGB_R(c)     (((c) >> 16) & 0xff)
#define RGB_G(c)     (((c) >> 8) & 0xff)
#define RGB_B(c)     ((c) & 0xff)

surface_t *surface_new (int w, int h);
void       surface_free (surface_t *s);

static inline void
gfx_pset (surface_t *s, int x, int y, uint32_t c)
{
        if ((unsigned) x < (unsigned) s->w && (unsigned) y < (unsigned) s->h)
                s->px[y * s->pitch + x] = c;
}

static inline uint32_t
gfx_pget (const surface_t *s, int x, int y)
{
        if ((unsigned) x < (unsigned) s->w && (unsigned) y < (unsigned) s->h)
                return s->px[y * s->pitch + x];
        return 0;
}

void gfx_clear (surface_t *s, uint32_t c);
void gfx_hline (surface_t *s, int x0, int x1, int y, uint32_t c);
void gfx_vline (surface_t *s, int x, int y0, int y1, uint32_t c);
void gfx_rect (surface_t *s, int x, int y, int w, int h, uint32_t c);
void gfx_line (surface_t *s, int x0, int y0, int x1, int y1, uint32_t c);

/* Copy src onto dst at (x,y). Pixels with alpha 0 are skipped (colour key). */
void gfx_blit (surface_t *dst, const surface_t *src, int x, int y);

/* Linear blend: t=0 -> a, t=255 -> b */
uint32_t gfx_mix (uint32_t a, uint32_t b, int t);
/* Additive blend, saturating per channel */
uint32_t gfx_add (uint32_t a, uint32_t b);

/* Fill pal[from..to] with a gradient c0 -> c1 */
void gfx_palette_ramp (palette_t pal, int from, int to, uint32_t c0, uint32_t c1);
/* Convert a w*h buffer of palette indices into the surface */
void gfx_indexed_to_surface (surface_t *s, const uint8_t *idx, const palette_t pal);

/* Integer-scale src into a dst_w*dst_h ARGB buffer, centred, with black
 * borders. Used by the plugin glue; also handy for previewers. */
void gfx_scale_to (uint32_t *dst, int dst_w, int dst_h, int dst_pitch, const surface_t *src);

#endif
