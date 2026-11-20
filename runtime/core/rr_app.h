/* The running program: one loaded scene at a time. Used by main.c. Game components use
 * rr_change_scene() and rr_quit() from rr_scene.h. */
#ifndef RR_APP_H
#define RR_APP_H

#include <stdbool.h>

/* Loads `<data_dir>/<scene>.rpk` and makes it the current scene. The platform layer must
 * be initialised. Returns false with an error message on failure. */
bool rr_app_start(const char *data_dir, const char *scene);

/* Runs one frame: update, draw, and a pending scene change. `screenshot` is NULL or a
 * file to save this frame to. Returns false when the program should end, either because
 * rr_quit() was called or because a scene failed to load (see rr_app_failed). */
bool rr_app_frame(float dt, const char *screenshot);

bool rr_app_failed(void);
void rr_app_stop(void);

/* Test aid: presses actions at given frames. The script is a comma-separated list of
 * `frame:action` or `frame:action:frames_held`, for example "30:ui_accept,60:accelerate:300".
 * The string must stay valid. */
void rr_input_set_script(const char *script);

#endif
