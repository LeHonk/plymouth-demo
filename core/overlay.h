/* overlay.h - prompt UI drawn over the demo (passwords, questions)
 *
 * Without this a demo would hide LUKS password prompts, so the core
 * always draws it on top of demo_frame's output.
 */
#ifndef OVERLAY_H
#define OVERLAY_H

#include <stdbool.h>

#include "gfx.h"

typedef struct
{
        bool active;
        bool secret;
        char prompt[256];
        char entry[256];        /* question answer text */
        int  bullets;           /* password length */
} overlay_t;

void overlay_password (overlay_t *o, const char *prompt, int bullets);
void overlay_question (overlay_t *o, const char *prompt, const char *entry);
void overlay_hide (overlay_t *o);
void overlay_draw (const overlay_t *o, surface_t *s, double t);

#endif
