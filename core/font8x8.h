/* font8x8.h - built-in 8x8 VGA bitmap font and text helpers */
#ifndef FONT8X8_H
#define FONT8X8_H

#include <stdint.h>

#include "gfx.h"

extern const uint8_t font8x8_glyphs[95][8];

/* Glyph for an ASCII character (unknown characters map to '?') */
const uint8_t *font8x8_glyph (char ch);

/* Draw text at (x,y), each font pixel scaled to scale*scale. Returns the
 * x position after the last character. */
int font_draw (surface_t *s, int x, int y, const char *text, int scale, uint32_t c);

/* Width in pixels of text at the given scale */
int font_width (const char *text, int scale);

/* Draw text horizontally centred on the surface */
void font_draw_centered (surface_t *s, int y, const char *text, int scale, uint32_t c);

#endif
