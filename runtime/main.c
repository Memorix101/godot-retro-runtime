/* Entry point, shared by all platforms.
 *
 *     retro [scene] [--data dir] [--dump] [--placeholders] [--frames N] [--screenshot file.png]
 *           [--input script]
 *
 * scene         name of the scene to start with (default: the name in <data>/boot.txt,
 *               else RR_START_SCENE)
 * --data        directory with the <scene>.rpk packages (default: a `data` directory
 *               next to the executable if it has a boot.txt, else RR_DATA_DIR)
 * --dump        print the scene's package as text and exit, without opening a window
 * --placeholders  print a placeholder script for every registered component and exit
 * --frames N    exit after N frames, each advanced by a fixed 1/60 s (for automated runs)
 * --screenshot  save the last frame before exiting (needs --frames)
 * --input       scripted input for tests, see rr_input_set_script() in core/rr_app.h
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/rr_app.h"
#include "core/rr_component.h"
#include "core/rr_platform.h"
#include "game/game.h"

#ifndef RR_DATA_DIR
#define RR_DATA_DIR "build/pkg/pc"
#endif
#ifndef RR_START_SCENE
#define RR_START_SCENE "main_menu"
#endif

static int fail(const char *what)
{
    fprintf(stderr, "retro: %s: %s\n", what, rr_last_error());
    return 1;
}

/* Reads the start scene from <data_dir>/boot.txt. Returns false if there is none. */
static bool read_boot_scene(const char *data_dir, char *scene, size_t size)
{
    char  path[300];
    FILE *f;
    bool  ok;

    snprintf(path, sizeof(path), "%s/boot.txt", data_dir);
    f = fopen(path, "rb");
    if (f == NULL) {
        return false;
    }
    ok = fgets(scene, (int)size, f) != NULL;
    fclose(f);
    scene[strcspn(scene, "\r\n")] = '\0';
    return ok && scene[0] != '\0';
}

/* A packaged game keeps its packages in a `data` directory next to the executable, with
 * the name of the first scene in data/boot.txt. Fills `data_dir` and returns true if that
 * layout is present. */
static bool find_packaged_data(const char *exe_path, char *data_dir, size_t size)
{
    const char *slash = strrchr(exe_path, '/');
    const char *backslash = strrchr(exe_path, '\\');
    char        scene[64];
    int         dir_length;

    if (backslash != NULL && (slash == NULL || backslash > slash)) {
        slash = backslash;
    }
    dir_length = slash != NULL ? (int)(slash - exe_path) + 1 : 0;
    snprintf(data_dir, size, "%.*sdata", dir_length, exe_path);
    return read_boot_scene(data_dir, scene, sizeof(scene));
}

static int dump(const char *data_dir, const char *scene)
{
    char      path[256];
    RRPackage pkg;
    uint32_t  size = 0;
    void     *data;

    snprintf(path, sizeof(path), "%s/%s.rpk", data_dir, scene);
    data = rr_plat_read_file(path, &size);
    if (data == NULL || !rr_package_open(&pkg, data, size, rr_plat_id())) {
        free(data);
        return fail(path);
    }
    rr_package_dump(&pkg, stdout);
    free(data);
    return 0;
}

int main(int argc, char **argv)
{
    const char *scene = NULL;
    const char *data_dir = NULL;
    static char packaged_dir[260], boot_scene[64];
    const char *screenshot = NULL;
    bool        want_dump = false, want_placeholders = false, running = true;
    long        max_frames = -1, frame = 0;
    int         i;

    rr_plat_boot();
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--dump") == 0) {
            want_dump = true;
        } else if (strcmp(argv[i], "--placeholders") == 0) {
            want_placeholders = true;
        } else if (strcmp(argv[i], "--data") == 0 && i + 1 < argc) {
            data_dir = argv[++i];
        } else if (strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
            max_frames = strtol(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "--screenshot") == 0 && i + 1 < argc) {
            screenshot = argv[++i];
        } else if (strcmp(argv[i], "--input") == 0 && i + 1 < argc) {
            rr_input_set_script(argv[++i]);
        } else if (argv[i][0] != '-') {
            scene = argv[i];
        } else {
            fprintf(stderr, "retro: unknown option '%s'\n", argv[i]);
            return 2;
        }
    }

    if (data_dir == NULL) {
        data_dir = argc > 0 && find_packaged_data(argv[0], packaged_dir, sizeof(packaged_dir))
                       ? packaged_dir
                       : RR_DATA_DIR;
    }
    if (scene == NULL) {
        scene = read_boot_scene(data_dir, boot_scene, sizeof(boot_scene)) ? boot_scene
                                                                          : RR_START_SCENE;
    }

#ifdef RR_INPUT_SCRIPT
    /* Test builds for consoles, which have no command line. */
    rr_input_set_script(RR_INPUT_SCRIPT);
#endif
    if (want_dump) {
        return dump(data_dir, scene);
    }
    if (!game_register_components()) {
        return fail("component registration");
    }
    if (want_placeholders) {
        rr_component_write_placeholders(stdout);
        return 0;
    }
    if (!rr_plat_init("Retro Runtime")) {
        return fail("platform init");
    }
    if (!rr_app_start(data_dir, scene)) {
        rr_plat_shutdown();
        return fail(scene);
    }

    while (running && !rr_plat_should_quit() && (max_frames < 0 || frame < max_frames)) {
        frame++;
        running = rr_app_frame(max_frames < 0 ? rr_plat_frame_time() : 1.0f / 60.0f,
                               frame == max_frames ? screenshot : NULL);
    }
    if (rr_app_failed()) {
        fail("scene change");
        rr_plat_shutdown();
        return 1;
    }

    rr_app_stop();
    rr_plat_shutdown();
    return 0;
}
