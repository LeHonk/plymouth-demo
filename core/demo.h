/* demo.h - the interface every demo implements (see demo/demo.c)
 *
 * The core calls demo_frame() once per tick (FPS from the theme file) and
 * presents the surface on every display, scaled up to fit. A demo never
 * touches Plymouth directly, so the same code also runs in the SDL
 * previewer (make preview).
 */
#ifndef DEMO_H
#define DEMO_H

#include <stdint.h>

#include "gfx.h"
#include "font8x8.h"
#include "dmath.h"

typedef enum
{
        DEMO_MODE_BOOT,
        DEMO_MODE_SHUTDOWN,
        DEMO_MODE_REBOOT,
        DEMO_MODE_UPDATES,
} demo_mode_t;

typedef struct
{
        int         width, height;  /* render surface size */
        double      t;              /* seconds since the splash was shown */
        uint32_t    frame;          /* frame counter */
        double      boot_progress;  /* 0..1, estimated by plymouthd */
        const char *status;         /* last update_status text, or NULL */
        const char *message;        /* last display_message text, or NULL */
        const char *theme_dir;      /* where assets/ was installed */
        demo_mode_t mode;
} demo_ctx_t;

/* Called once before the first frame. Return a pointer to your state. */
void *demo_init (const demo_ctx_t *ctx);

/* Draw a complete frame into fb (fb->w == ctx->width, fb->h == ctx->height).
 * The previous frame's pixels are still there, so feedback effects work. */
void demo_frame (void *state, const demo_ctx_t *ctx, surface_t *fb);

/* A key was typed (UTF-8). Optional: an empty body is fine. */
void demo_key (void *state, const char *key);

/* Splash is going away: free everything from demo_init. */
void demo_shutdown (void *state);

#endif
