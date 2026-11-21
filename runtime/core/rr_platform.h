/* Interface between the portable core and a platform layer (runtime/platform/<name>/).
 * Game code and the core only use what is declared here. */
#ifndef RR_PLATFORM_H
#define RR_PLATFORM_H

#include "rr_package.h"

/* The size of the screen in the units the game counts in: the exporter bakes UI rectangles
 * for it (screen_size() of the platform's baker), and 2D positions are in these pixels. A
 * platform whose screen has another shape sets both on its compiler's command line; its
 * graphics layer scales to the pixels the screen really has. */
#ifndef RR_SCREEN_WIDTH
#define RR_SCREEN_WIDTH  640
#define RR_SCREEN_HEIGHT 480
#endif

/* The platform id this build loads packages for (RR_PLATFORM_*). */
uint8_t rr_plat_id(void);

/* The language the machine is set to, as a locale name like "en" or "de"; NULL or "" if
 * it cannot tell. */
const char *rr_plat_locale(void);

/* Called first thing in main, before any file is read: a platform whose files are on a
 * card that has to be mounted does that here. */
void  rr_plat_boot(void);
bool  rr_plat_init(const char *title);
void  rr_plat_shutdown(void);
bool  rr_plat_should_quit(void);
float rr_plat_frame_time(void); /* seconds since the previous frame */
uint32_t rr_plat_time_us(void); /* a running clock in microseconds, for measuring */

/* Reads a whole file into one malloc'ed, 4-byte aligned block. Returns NULL and sets the
 * error message on failure. The caller frees the block with free(). */
void *rr_plat_read_file(const char *path, uint32_t *size);

/* Saves the frame that is being drawn to an image file. Call it after drawing and before
 * rr_gfx_end_frame. Debug aid; platforms without a place to write to return false. */
bool rr_plat_screenshot(const char *path);

/* Saved games, see rr_save_write in rr_scene.h. The name is already checked. */
bool rr_plat_save_write(const char *name, const void *data, uint32_t size);
bool rr_plat_save_read(const char *name, void *data, uint32_t size);

/* Current value of one input binding, 0..1. The codes in a binding were chosen by this
 * platform's baker. */
float rr_plat_binding_value(const RRPkgBinding *binding);

/* Rumble of the first pad: the strength of its light and of its heavy motor, 0..1. It
 * runs until both are set to 0. Platforms without rumble do nothing. */
void rr_plat_vibrate(float weak, float strong);

/* Graphics. The interface works at the level of "draw this mesh", not at the level of GL
 * calls, so each console can implement it with its native API. Meshes and textures are
 * the baked assets from the package, in this platform's formats.
 *
 * Frame order: begin_frame, optionally set_camera + set_fog + draw_mesh calls +
 * draw_quad_3d calls,
 * begin_2d, draw_quad calls, end_frame. Quads are drawn over the 3D scene, in call order. */
void rr_gfx_begin_frame(uint32_t clear_rgba);
/* With an `ortho_size` above 0 the camera is orthogonal: the view is that many units high
 * at any distance, and the field of view does not count. */
void rr_gfx_set_camera(const RRMat34 *world, float fov_y_degrees, float z_near, float z_far,
                       float ortho_size);
/* Fog for the meshes and particles that follow, until rr_gfx_begin_2d. NULL or an
 * environment with its fog off means none. Call it after rr_gfx_set_camera. */
void rr_gfx_set_fog(const RRPkgEnvironmentNode *environment);
/* One face of the sky: an opaque quad that everything else is drawn over, whatever its
 * distance, and that fog does not touch. Corners and `uv` as for rr_gfx_draw_quad_3d.
 * Call it after rr_gfx_set_camera and before anything else of the frame. */
void rr_gfx_draw_sky(uint32_t texture, const float *xyz, const float *uv);
/* `textures` maps asset indices to texture handles, for the surfaces that have one. */
void rr_gfx_draw_mesh(const RRPkgMesh *mesh, const RRMat34 *world, const uint32_t *textures);
/* The vertices of a mesh (`size` bytes, a copy the core keeps for a skin) were written to
 * since it was last drawn. For hardware that reads vertices from memory behind the back of
 * the processor's cache; the others do nothing. */
void rr_gfx_mesh_changed(const RRPkgMesh *mesh, uint32_t size);
/* One translucent quad in world space, for particles. Corners as x,y,z triples: top left,
 * top right, bottom right, bottom left; `uv` and `rgba` as for rr_gfx_draw_quad. Call it
 * after all rr_gfx_draw_mesh calls of the frame and before rr_gfx_begin_2d. */
void rr_gfx_draw_quad_3d(uint32_t texture, const float *xyz, const float *uv, uint32_t rgba);
void rr_gfx_begin_2d(void);
void rr_gfx_end_frame(void);

/* Textures are handed over once and referred to by the returned handle. The pixel data
 * stays in the package; a platform may keep pointing at it until the unload. */
uint32_t rr_gfx_texture_load(const RRPkgTexture *texture);
void     rr_gfx_texture_unload(uint32_t handle);

/* One quad in screen pixels. Corners: top left, top right, bottom right, bottom left,
 * as x,y pairs; `uv` in the same order, 0..1 over the stored texture. `texture` is a
 * handle or RR_NONE32 for a plain colour. `rgba` is 0xRRGGBBAA and tints the texture. */
void rr_gfx_draw_quad(uint32_t texture, const float *xy, const float *uv, uint32_t rgba);

/* Sound effects, same handle scheme as textures. */
uint32_t rr_snd_load(const RRPkgSound *sound);
void     rr_snd_unload(uint32_t handle);
/* `pan` runs from -1 (left) to 1 (right); `pitch` is the speed, 1.0 as recorded.
 * rr_snd_set changes a sound that is playing. */
void     rr_snd_play(uint32_t handle, float volume, float pan, float pitch, bool loop);
void     rr_snd_set(uint32_t handle, float volume, float pan, float pitch);
void     rr_snd_stop(uint32_t handle);

/* Music: a 16-bit PCM WAV file that is streamed while it plays. One piece at a time;
 * starting another one ends the first. */
bool rr_music_play(const char *path, float volume, bool loop);
void rr_music_set_volume(float volume); /* of the piece that plays */
void rr_music_stop(void);

#endif
