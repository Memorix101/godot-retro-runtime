/* The running program: one loaded scene at a time, and the switch between scenes. */
#include "rr_app.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rr_internal.h"
#include "rr_platform.h"

static struct {
    char      data_dir[160];
    char      pending[64]; /* scene to load at the end of the frame, "" for none */
    bool      quit;
    bool      loaded;
    bool      fresh; /* the scene was loaded since the last frame */
    void     *file;
    RRPackage pkg;
    RRScene   scene;
} g_app;

#ifdef RR_PRINT_FPS
static uint32_t g_mark, g_spent[3]; /* microseconds in update, draw and present */
#endif

static void unload_scene(void)
{
    if (g_app.loaded) {
        rr_scene_keep_autoloads(&g_app.scene);
        rr_scene_unload_resources(&g_app.scene);
        rr_scene_destroy(&g_app.scene);
        rr_input_attach(NULL);
        g_app.loaded = false;
    }
    free(g_app.file);
    g_app.file = NULL;
}

static bool load_scene(const char *name)
{
    char     path[256];
    uint32_t size = 0;

    unload_scene();
    snprintf(path, sizeof(path), "%s/%s.rpk", g_app.data_dir, name);
    g_app.file = rr_plat_read_file(path, &size);
    if (g_app.file == NULL || !rr_package_open(&g_app.pkg, g_app.file, size, rr_plat_id())) {
        return false;
    }
    rr_input_attach(&g_app.pkg);
    if (!rr_scene_create(&g_app.scene, &g_app.pkg)) {
        rr_input_attach(NULL);
        return false;
    }
    g_app.loaded = true;
    g_app.fresh  = true;
    rr_scene_load_resources(&g_app.scene);
    rr_scene_ready(&g_app.scene);
    printf("scene: %s\n", name);
    fflush(stdout);
    return true;
}

bool rr_app_start(const char *data_dir, const char *scene)
{
    memset(&g_app, 0, sizeof(g_app));
    snprintf(g_app.data_dir, sizeof(g_app.data_dir), "%s", data_dir);
    return load_scene(scene);
}

bool rr_app_frame(float dt, const char *screenshot)
{
    /* The time of the frame in which a scene was loaded is mostly loading, and a hitch
     * must not throw things through walls: game time never runs faster than this. */
    if (g_app.fresh) {
        dt = 1.0f / 60.0f;
        g_app.fresh = false;
    } else if (dt > 0.1f) {
        dt = 0.1f;
    }
#ifdef RR_PRINT_FPS
    {
        /* The rate the game really runs at, every two seconds or so. On a console this
         * goes to the serial output; an emulator's own counter shows something else. */
        static float seconds;
        static int   frames;
        seconds += dt;
        if (++frames == 120) {
            /* With the milliseconds a frame spends in the game logic, in handing the scene
             * to the graphics layer, and in finishing the frame there (which includes
             * waiting for the screen). */
            printf("fps: %d  update %.2f ms  draw %.2f ms  present %.2f ms\n",
                   (int)(120.0f / seconds + 0.5f), (double)((float)g_spent[0] / 120000.0f),
                   (double)((float)g_spent[1] / 120000.0f), (double)((float)g_spent[2] / 120000.0f));
            g_spent[0] = g_spent[1] = g_spent[2] = 0;
            fflush(stdout);
            seconds = 0.0f;
            frames  = 0;
        }
    }
#endif
#ifdef RR_PRINT_FPS
    g_mark = rr_plat_time_us();
#endif
    rr_scene_update(&g_app.scene, dt);
#ifdef RR_PRINT_FPS
    g_spent[0] += rr_plat_time_us() - g_mark;
    g_mark = rr_plat_time_us();
#endif

    rr_gfx_begin_frame(g_app.pkg.header->clear_color);
    rr_scene_draw(&g_app.scene);
    if (screenshot != NULL && !rr_plat_screenshot(screenshot)) {
        fprintf(stderr, "retro: cannot save screenshot '%s'\n", screenshot);
    }
#ifdef RR_PRINT_FPS
    g_spent[1] += rr_plat_time_us() - g_mark;
    g_mark = rr_plat_time_us();
#endif
    rr_gfx_end_frame();
#ifdef RR_PRINT_FPS
    g_spent[2] += rr_plat_time_us() - g_mark;
#endif

    if (g_app.pending[0] != '\0') {
        char name[sizeof(g_app.pending)];
        memcpy(name, g_app.pending, sizeof(name));
        g_app.pending[0] = '\0';
        if (!load_scene(name)) {
            return false;
        }
    }
    return !g_app.quit;
}

void rr_app_stop(void)
{
    unload_scene();
}

bool rr_app_failed(void)
{
    return !g_app.loaded;
}

const char *rr_app_data_dir(void)
{
    return g_app.data_dir;
}

void rr_change_scene(const char *name)
{
    /* The name usually points into the package that is about to be freed, so copy it. */
    snprintf(g_app.pending, sizeof(g_app.pending), "%s", name);
}

static bool save_name_ok(const char *name)
{
    size_t i;
    for (i = 0; name[i] != '\0'; i++) {
        const char c = name[i];
        if (i >= 12 || !((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_')) {
            return false;
        }
    }
    return i > 0;
}

bool rr_save_write(const char *name, const void *data, uint32_t size)
{
    return save_name_ok(name) && rr_plat_save_write(name, data, size);
}

bool rr_save_read(const char *name, void *data, uint32_t size)
{
    return save_name_ok(name) && rr_plat_save_read(name, data, size);
}

void rr_quit(void)
{
    g_app.quit = true;
    printf("quit\n");
    fflush(stdout);
}
