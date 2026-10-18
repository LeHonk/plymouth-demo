/* plugin.c - Plymouth splash plugin glue
 *
 * This is the only file that talks to Plymouth. It owns the render
 * surface, ticks the demo at a fixed rate, forwards boot events into
 * demo_ctx_t and presents each frame on every pixel display.
 *
 * plymouthd loads <pluginsdir>/<ModuleName>.so and looks up
 * ply_boot_splash_plugin_get_interface().
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <ply-boot-splash-plugin.h>
#include <ply-event-loop.h>
#include <ply-key-file.h>
#include <ply-keyboard.h>
#include <ply-list.h>
#include <ply-logger.h>
#include <ply-pixel-buffer.h>
#include <ply-pixel-display.h>
#include <ply-rectangle.h>
#include <ply-trigger.h>

#include "demo.h"
#include "overlay.h"

typedef struct
{
        ply_boot_splash_plugin_t *plugin;
        ply_pixel_display_t      *display;
        uint32_t                 *scaled;       /* display-sized frame */
        unsigned long             w, h;
} view_t;

struct _ply_boot_splash_plugin
{
        ply_event_loop_t *loop;
        ply_keyboard_t   *keyboard;
        ply_list_t       *views;

        /* settings from the [demo] group of the .plymouth file */
        char             *theme_dir;
        int               render_w, render_h;   /* 0 = native */
        double            fps;

        surface_t        *fb;
        void             *demo_state;
        demo_ctx_t        ctx;
        overlay_t         overlay;
        char              status[256];
        char              message[256];
        double            start_time;
        bool              running;
};

static void on_timeout (void *user_data, ply_event_loop_t *loop);

static double
now (void)
{
        struct timespec ts;

        clock_gettime (CLOCK_MONOTONIC, &ts);
        return ts.tv_sec + ts.tv_nsec / 1e9;
}

/* ---- drawing ------------------------------------------------------------ */

static void
on_draw (void                *user_data,
         ply_pixel_buffer_t  *pixel_buffer,
         int                  x,
         int                  y,
         int                  width,
         int                  height,
         ply_pixel_display_t *display)
{
        view_t *view = user_data;
        ply_boot_splash_plugin_t *plugin = view->plugin;
        ply_rectangle_t area = { 0, 0, view->w, view->h };

        (void) x; (void) y; (void) width; (void) height; (void) display;

        if (plugin->fb == NULL || view->scaled == NULL) {
                ply_pixel_buffer_fill_with_hex_color (pixel_buffer, NULL, 0x000000);
                return;
        }
        gfx_scale_to (view->scaled, view->w, view->h, view->w, plugin->fb);
        ply_pixel_buffer_fill_with_argb32_data (pixel_buffer, &area, view->scaled);
}

static void
request_redraw (ply_boot_splash_plugin_t *plugin)
{
        ply_list_node_t *node;

        for (node = ply_list_get_first_node (plugin->views); node != NULL;
             node = ply_list_get_next_node (plugin->views, node)) {
                view_t *view = ply_list_node_get_data (node);
                ply_pixel_display_draw_area (view->display, 0, 0, view->w, view->h);
        }
}

/* Pick the render size: fixed from the theme, or the largest display */
static bool
ensure_surface (ply_boot_splash_plugin_t *plugin)
{
        int w = plugin->render_w, h = plugin->render_h;

        if (w <= 0 || h <= 0) {
                ply_list_node_t *node;
                w = h = 0;
                for (node = ply_list_get_first_node (plugin->views); node != NULL;
                     node = ply_list_get_next_node (plugin->views, node)) {
                        view_t *view = ply_list_node_get_data (node);
                        if ((int) (view->w * view->h) > w * h) {
                                w = view->w;
                                h = view->h;
                        }
                }
        }
        if (w <= 0 || h <= 0)
                return false;
        if (plugin->fb != NULL && plugin->fb->w == w && plugin->fb->h == h)
                return true;

        surface_free (plugin->fb);
        plugin->fb = surface_new (w, h);
        plugin->ctx.width = w;
        plugin->ctx.height = h;
        ply_trace ("render surface %dx%d", w, h);
        return plugin->fb != NULL;
}

static void
render_frame (ply_boot_splash_plugin_t *plugin)
{
        if (!ensure_surface (plugin))
                return;

        plugin->ctx.t = now () - plugin->start_time;
        if (plugin->demo_state == NULL)
                plugin->demo_state = demo_init (&plugin->ctx);

        demo_frame (plugin->demo_state, &plugin->ctx, plugin->fb);
        overlay_draw (&plugin->overlay, plugin->fb, plugin->ctx.t);
        plugin->ctx.frame++;

        request_redraw (plugin);
}

static void
on_timeout (void *user_data, ply_event_loop_t *loop)
{
        ply_boot_splash_plugin_t *plugin = user_data;
        double started = now ();

        if (!plugin->running)
                return;
        render_frame (plugin);

        /* Keep a steady rate: subtract the time spent rendering */
        double wait = 1.0 / plugin->fps - (now () - started);
        if (wait < 0.001)
                wait = 0.001;
        ply_event_loop_watch_for_timeout (loop, wait, on_timeout, plugin);
}

static void
stop_animation (ply_boot_splash_plugin_t *plugin)
{
        if (!plugin->running)
                return;
        plugin->running = false;
        if (plugin->loop != NULL)
                ply_event_loop_stop_watching_for_timeout (plugin->loop, on_timeout, plugin);
}

/* ---- plugin lifecycle --------------------------------------------------- */

static ply_boot_splash_plugin_t *
create_plugin (ply_key_file_t *key_file)
{
        ply_boot_splash_plugin_t *plugin = calloc (1, sizeof(*plugin));

        plugin->views = ply_list_new ();
        plugin->theme_dir = ply_key_file_get_value (key_file, "demo", "ThemeDir");
        plugin->render_w = (int) ply_key_file_get_long (key_file, "demo", "RenderWidth", 320);
        plugin->render_h = (int) ply_key_file_get_long (key_file, "demo", "RenderHeight", 200);
        plugin->fps = ply_key_file_get_double (key_file, "demo", "FPS", 50.0);
        if (plugin->fps < 1.0)
                plugin->fps = 50.0;

        plugin->ctx.theme_dir = plugin->theme_dir;
        dmath_init ();
        return plugin;
}

static void
detach_from_event_loop (void *user_data, int exit_code, ply_event_loop_t *loop)
{
        ply_boot_splash_plugin_t *plugin = user_data;

        (void) exit_code; (void) loop;
        plugin->loop = NULL;
}

static void
destroy_plugin (ply_boot_splash_plugin_t *plugin)
{
        ply_list_node_t *node;

        if (plugin == NULL)
                return;
        stop_animation (plugin);
        if (plugin->loop != NULL)
                ply_event_loop_stop_watching_for_exit (plugin->loop,
                                                       detach_from_event_loop,
                                                       plugin);
        if (plugin->demo_state != NULL)
                demo_shutdown (plugin->demo_state);

        for (node = ply_list_get_first_node (plugin->views); node != NULL;
             node = ply_list_get_next_node (plugin->views, node)) {
                view_t *view = ply_list_node_get_data (node);
                free (view->scaled);
                free (view);
        }
        ply_list_free (plugin->views);
        surface_free (plugin->fb);
        free (plugin->theme_dir);
        free (plugin);
}

static void
on_keyboard_input (void *user_data, const char *input, size_t size)
{
        ply_boot_splash_plugin_t *plugin = user_data;
        char key[16];

        if (plugin->demo_state == NULL || size >= sizeof(key))
                return;
        memcpy (key, input, size);
        key[size] = '\0';
        demo_key (plugin->demo_state, key);
}

static void
set_keyboard (ply_boot_splash_plugin_t *plugin, ply_keyboard_t *keyboard)
{
        plugin->keyboard = keyboard;
        ply_keyboard_add_input_handler (keyboard, on_keyboard_input, plugin);
}

static void
unset_keyboard (ply_boot_splash_plugin_t *plugin, ply_keyboard_t *keyboard)
{
        ply_keyboard_remove_input_handler (keyboard, on_keyboard_input);
        plugin->keyboard = NULL;
}

static void
add_pixel_display (ply_boot_splash_plugin_t *plugin, ply_pixel_display_t *display)
{
        view_t *view = calloc (1, sizeof(*view));

        view->plugin = plugin;
        view->display = display;
        view->w = ply_pixel_display_get_width (display);
        view->h = ply_pixel_display_get_height (display);
        view->scaled = calloc (view->w * view->h, sizeof(uint32_t));
        ply_trace ("adding display %lux%lu", view->w, view->h);

        ply_list_append_data (plugin->views, view);
        ply_pixel_display_set_draw_handler (display, on_draw, view);
}

static void
remove_pixel_display (ply_boot_splash_plugin_t *plugin, ply_pixel_display_t *display)
{
        ply_list_node_t *node;

        for (node = ply_list_get_first_node (plugin->views); node != NULL;
             node = ply_list_get_next_node (plugin->views, node)) {
                view_t *view = ply_list_node_get_data (node);
                if (view->display != display)
                        continue;
                ply_pixel_display_set_draw_handler (display, NULL, NULL);
                ply_list_remove_node (plugin->views, node);
                free (view->scaled);
                free (view);
                return;
        }
}

/* Text-only consoles get nothing; Plymouth falls back to details there. */
static void
add_text_display (ply_boot_splash_plugin_t *plugin, ply_text_display_t *display)
{
        (void) plugin; (void) display;
}

static void
remove_text_display (ply_boot_splash_plugin_t *plugin, ply_text_display_t *display)
{
        (void) plugin; (void) display;
}

static bool
show_splash_screen (ply_boot_splash_plugin_t *plugin,
                    ply_event_loop_t         *loop,
                    ply_buffer_t             *boot_buffer,
                    ply_boot_splash_mode_t    mode)
{
        (void) boot_buffer;

        switch (mode) {
        case PLY_BOOT_SPLASH_MODE_SHUTDOWN: plugin->ctx.mode = DEMO_MODE_SHUTDOWN; break;
        case PLY_BOOT_SPLASH_MODE_REBOOT:   plugin->ctx.mode = DEMO_MODE_REBOOT; break;
        case PLY_BOOT_SPLASH_MODE_BOOT_UP:  plugin->ctx.mode = DEMO_MODE_BOOT; break;
        default:                            plugin->ctx.mode = DEMO_MODE_UPDATES; break;
        }

        if (plugin->loop == NULL) {
                plugin->loop = loop;
                ply_event_loop_watch_for_exit (loop,
                                               detach_from_event_loop,
                                               plugin);
        }
        if (!plugin->running) {
                plugin->running = true;
                plugin->start_time = now ();
                ply_event_loop_watch_for_timeout (loop, 1.0 / plugin->fps, on_timeout, plugin);
        }
        ply_trace ("splash shown, %.0f fps", plugin->fps);
        return true;
}

static void
hide_splash_screen (ply_boot_splash_plugin_t *plugin, ply_event_loop_t *loop)
{
        (void) loop;
        stop_animation (plugin);
}

/* ---- boot events -------------------------------------------------------- */

static void
update_status (ply_boot_splash_plugin_t *plugin, const char *status)
{
        snprintf (plugin->status, sizeof(plugin->status), "%s", status);
        plugin->ctx.status = plugin->status;
}

static void
system_update (ply_boot_splash_plugin_t *plugin, int progress)
{
        plugin->ctx.boot_progress = progress / 100.0;
}

static void
on_boot_progress (ply_boot_splash_plugin_t *plugin, double duration, double fraction_done)
{
        (void) duration;
        plugin->ctx.boot_progress = fraction_done;
}

static void
display_message (ply_boot_splash_plugin_t *plugin, const char *message)
{
        snprintf (plugin->message, sizeof(plugin->message), "%s", message);
        plugin->ctx.message = plugin->message;
}

static void
hide_message (ply_boot_splash_plugin_t *plugin, const char *message)
{
        if (plugin->ctx.message != NULL && strcmp (plugin->message, message) == 0)
                plugin->ctx.message = NULL;
}

static void
display_normal (ply_boot_splash_plugin_t *plugin)
{
        overlay_hide (&plugin->overlay);
}

static void
display_password (ply_boot_splash_plugin_t *plugin, const char *prompt, int bullets)
{
        overlay_password (&plugin->overlay, prompt, bullets);
}

static void
display_question (ply_boot_splash_plugin_t *plugin, const char *prompt, const char *entry_text)
{
        overlay_question (&plugin->overlay, prompt, entry_text);
}

static void
become_idle (ply_boot_splash_plugin_t *plugin, ply_trigger_t *idle_trigger)
{
        (void) plugin;
        ply_trigger_pull (idle_trigger, NULL);
}

/* The only exported symbol (everything else is -fvisibility=hidden) */
__attribute__((visibility ("default"))) ply_boot_splash_plugin_interface_t *
ply_boot_splash_plugin_get_interface (void)
{
        static ply_boot_splash_plugin_interface_t interface = {
                .create_plugin        = create_plugin,
                .destroy_plugin       = destroy_plugin,
                .set_keyboard         = set_keyboard,
                .unset_keyboard       = unset_keyboard,
                .add_pixel_display    = add_pixel_display,
                .remove_pixel_display = remove_pixel_display,
                .add_text_display     = add_text_display,
                .remove_text_display  = remove_text_display,
                .show_splash_screen   = show_splash_screen,
                .system_update        = system_update,
                .update_status        = update_status,
                .on_boot_progress     = on_boot_progress,
                .hide_splash_screen   = hide_splash_screen,
                .display_message      = display_message,
                .hide_message         = hide_message,
                .display_normal       = display_normal,
                .display_password     = display_password,
                .display_question     = display_question,
                .become_idle          = become_idle,
        };

        return &interface;
}
