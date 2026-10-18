/* overlay.c - prompt UI drawn over the demo */
#include "overlay.h"
#include "font8x8.h"

#include <stdio.h>
#include <string.h>

static void
copy_text (char *dst, size_t size, const char *src)
{
        snprintf (dst, size, "%s", src != NULL ? src : "");
}

void
overlay_password (overlay_t *o, const char *prompt, int bullets)
{
        o->active = true;
        o->secret = true;
        copy_text (o->prompt, sizeof(o->prompt), prompt != NULL ? prompt : "Password:");
        o->entry[0] = '\0';
        o->bullets = bullets;
}

void
overlay_question (overlay_t *o, const char *prompt, const char *entry)
{
        o->active = true;
        o->secret = false;
        copy_text (o->prompt, sizeof(o->prompt), prompt);
        copy_text (o->entry, sizeof(o->entry), entry);
        o->bullets = 0;
}

void
overlay_hide (overlay_t *o)
{
        o->active = false;
}

void
overlay_draw (const overlay_t *o, surface_t *s, double t)
{
        if (!o->active)
                return;

        char line[64];
        int max_chars = s->w / 8 - 4;
        int n;

        if (max_chars > (int) sizeof(line) - 2)
                max_chars = sizeof(line) - 2;
        if (o->secret) {
                n = o->bullets < max_chars ? o->bullets : max_chars;
                memset (line, '*', n);
        } else {
                n = snprintf (line, sizeof(line), "%.*s", max_chars, o->entry);
        }
        /* blinking cursor */
        line[n++] = ((int) (t * 2) & 1) ? '_' : ' ';
        line[n] = '\0';

        int box_w = s->w - 16, box_h = 36;
        int bx = 8, by = (s->h - box_h) / 2;

        gfx_rect (s, bx, by, box_w, box_h, RGB (0, 0, 0));
        gfx_hline (s, bx, bx + box_w - 1, by, RGB (255, 255, 255));
        gfx_hline (s, bx, bx + box_w - 1, by + box_h - 1, RGB (255, 255, 255));
        gfx_vline (s, bx, by, by + box_h - 1, RGB (255, 255, 255));
        gfx_vline (s, bx + box_w - 1, by, by + box_h - 1, RGB (255, 255, 255));

        char prompt[64];
        snprintf (prompt, sizeof(prompt), "%.*s", max_chars, o->prompt);
        font_draw_centered (s, by + 8, prompt, 1, RGB (255, 255, 255));
        font_draw_centered (s, by + 20, line, 1, RGB (255, 255, 0));
}
