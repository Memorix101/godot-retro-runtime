/* PSP graphics on the GU library of the PSP SDK.
 *
 * The graphics chip transforms the vertices itself, and the baked vertex is the one it
 * reads (texture coordinates, colour, position), so a mesh is drawn straight from the
 * package: one command per strip.
 *
 * What the chip does not do is cut triangles at the edges of the screen. It works in a
 * square of 4096 x 4096 pixels with the screen in the middle, and a triangle with a corner
 * outside of that square is left out as a whole: the ground under the camera, a wall next
 * to it. So a mesh whose bounding sphere reaches out of that square, or in front of the
 * near plane, takes a slower path: its triangles are looked at one by one here and cut
 * where they leave. Everything that is not a mesh (the sky, particles) goes that way too.
 *
 * The game counts in a screen of RR_SCREEN_WIDTH x RR_SCREEN_HEIGHT; 2D quads are scaled
 * to the 480 x 272 pixels of the screen when they are drawn.
 *
 * The chip reads vertices and textures from memory by itself, not through the processor's
 * cache: what the processor wrote must be written back before it is drawn (the package in
 * rr_plat_read_file, skinned meshes in rr_gfx_mesh_changed, textures when they are loaded).
 * The display list is written without the cache. */
#include <malloc.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include <pspdisplay.h>
#include <pspge.h>
#include <pspgu.h>
#include <pspkernel.h>
#include <vram.h>

#include "../../core/rr_platform.h"
#include "psp_gfx.h"

#define SCREEN_W 480
#define SCREEN_H 272
#define BUFFER_W 512 /* a row of the frame buffer in video memory */

#define MAX_TEXTURES    256
#define MAX_TRANSLUCENT 256

/* The display list of one frame: the commands, and the vertices that are made here. */
#define LIST_BYTES   (2u * 1024u * 1024u)
/* Kept free for the commands, which are not counted one by one. */
#define LIST_RESERVE (128u * 1024u)
/* Vertices that are made here go into blocks of this many, taken from the list. */
#define BLOCK_VERTS 1020u

#define VERTEX_FORMAT (GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF)

/* How far a corner may be from the middle of the screen, in half screens: the square has
 * 2048 pixels to each side, the screen 240 and 136. A little is left for rounding. */
#define GUARD_X 8.0f
#define GUARD_Y 14.0f

enum { CULL_NONE, CULL_BACK, CULL_FRONT };
enum { PLANE_NEAR = 1, PLANE_LEFT = 2, PLANE_RIGHT = 4, PLANE_BOTTOM = 8, PLANE_TOP = 16 };

typedef struct {
    void    *pixels; /* NULL: free slot */
    uint16_t width, height;
    uint8_t  format; /* GU_PSM_* */
    uint8_t  filter; /* GU_NEAREST or GU_LINEAR */
    bool     in_vram;
} Texture;

/* A vertex with where the camera's projection puts it (x, y, z, w before the division). */
typedef struct {
    float       clip[4];
    RRVertexPSP v;
} ClipVertex;

typedef struct {
    const RRPkgMesh    *mesh;
    const RRPkgSurface *surface;
    const uint32_t     *textures;
    RRMat34             world;
    float               distance; /* squared, from the camera to the middle of the mesh */
} Translucent;

static uint32_t g_list[LIST_BYTES / 4] __attribute__((aligned(16)));
static uint32_t g_list_left; /* bytes of the list that are not handed out yet */
static bool     g_list_warned;

static Texture g_textures[MAX_TEXTURES];

/* Vertices made here: a block from the list that is filled from the front. What was added
 * since the last draw command is the batch; it is drawn when the state changes. */
static RRVertexPSP *g_block;
static uint32_t     g_block_used, g_block_size, g_batch_first;
static int          g_batch_transform; /* GU_TRANSFORM_2D or GU_TRANSFORM_3D */

/* The state the chip is in, so that only what changes is sent. */
static uint32_t g_state;         /* see set_state; 0xFFFFFFFF: not known */
static uint32_t g_state_texture; /* handle + 1, 0 for none */
static bool     g_model_identity;

static float g_view_proj[16]; /* world to clip space, columns */
static float g_mvp[16];       /* the space of what is drawn now to clip space */
static float g_scale_x, g_scale_y; /* what a length of 1 at distance 1 is in clip space */
static float g_near;
static bool  g_ortho;
static float g_eye[3];
static bool  g_fog;
static bool  g_in_frame;

static Translucent g_translucent[MAX_TRANSLUCENT];
static uint16_t    g_translucent_order[MAX_TRANSLUCENT];
static uint32_t    g_translucent_count;

bool psp_gfx_init(void)
{
    /* The frame buffers and the depth buffer take 1.4 MB of the 2 MB of video memory;
     * textures get what is left and go into main memory after that. */
    void *front = vrelptr(vramalloc(BUFFER_W * SCREEN_H * 4));
    void *back = vrelptr(vramalloc(BUFFER_W * SCREEN_H * 4));
    void *depth = vrelptr(vramalloc(BUFFER_W * SCREEN_H * 2));

    sceGuInit();
    sceGuStart(GU_DIRECT, g_list);
    sceGuDrawBuffer(GU_PSM_8888, front, BUFFER_W);
    sceGuDispBuffer(SCREEN_W, SCREEN_H, back, BUFFER_W);
    sceGuDepthBuffer(depth, BUFFER_W);
    sceGuOffset(2048 - SCREEN_W / 2, 2048 - SCREEN_H / 2);
    sceGuViewport(2048, 2048, SCREEN_W, SCREEN_H);
    /* The depth buffer counts down: 65535 is the near plane, 0 the far one. */
    sceGuDepthRange(65535, 0);
    sceGuDepthFunc(GU_GEQUAL);
    sceGuScissor(0, 0, SCREEN_W, SCREEN_H);
    sceGuEnable(GU_SCISSOR_TEST);
    sceGuEnable(GU_CLIP_PLANES);
    sceGuShadeModel(GU_SMOOTH);
    /* Texture times vertex colour, alpha included. */
    sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGBA);
    sceGuTexScale(1.0f, 1.0f);
    sceGuTexOffset(0.0f, 0.0f);
    sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);
    /* Punch-through: a pixel is drawn from half the alpha on. */
    sceGuAlphaFunc(GU_GEQUAL, 0x80, 0xFF);
    sceGuFinish();
    sceGuSync(GU_SYNC_FINISH, GU_SYNC_WHAT_DONE);
    sceDisplayWaitVblankStart();
    sceGuDisplay(GU_TRUE);
    return true;
}

void psp_gfx_shutdown(void)
{
    sceGuDisplay(GU_FALSE);
    sceGuTerm();
}

/* --- the list ------------------------------------------------------------------------ */

static void *list_memory(uint32_t bytes)
{
    if (g_list_left < bytes + LIST_RESERVE) {
        if (!g_list_warned) {
            g_list_warned = true;
            printf("retro: the display list is full (%u bytes per frame), polygons are left out\n",
                   (unsigned)LIST_BYTES);
        }
        return NULL;
    }
    g_list_left -= bytes + 8;
    return sceGuGetMemory((int)bytes);
}

/* Draws the vertices that were made since the last draw command. */
static void batch_flush(void)
{
    if (g_block_used > g_batch_first) {
        sceGuDrawArray(GU_TRIANGLES, VERTEX_FORMAT | g_batch_transform,
                       (int)(g_block_used - g_batch_first), NULL, &g_block[g_batch_first]);
        g_list_left -= g_list_left > 32 ? 32 : g_list_left;
    }
    g_batch_first = g_block_used;
}

/* Room for `count` more vertices of triangles. `expected` is how many the caller thinks
 * it will ask for in all, which keeps a new block small when that is little. NULL when
 * the list is full. */
static RRVertexPSP *batch_reserve(uint32_t count, int transform, uint32_t expected)
{
    RRVertexPSP *out;

    if (transform != g_batch_transform) {
        batch_flush();
        g_batch_transform = transform;
    }
    if (g_block_used + count > g_block_size) {
        uint32_t size = expected < count ? count : expected;

        batch_flush();
        size = size > BLOCK_VERTS ? BLOCK_VERTS : (size < 96 ? 96 : size);
        g_block = (RRVertexPSP *)list_memory(size * sizeof(RRVertexPSP));
        g_block_size = g_block != NULL ? size : 0;
        g_block_used = 0;
        g_batch_first = 0;
        if (g_block == NULL) {
            return NULL;
        }
    }
    out = &g_block[g_block_used];
    g_block_used += count;
    return out;
}

/* --- state --------------------------------------------------------------------------- */

static void toggle(int what, bool on)
{
    if (on) {
        sceGuEnable(what);
    } else {
        sceGuDisable(what);
    }
}

/* Puts the chip into the state for what is drawn next. `texture` is a handle or
 * RR_NONE32; `clamp` keeps the texture from repeating (the sky, whose edges would show
 * the other side of the picture). */
static void set_state(uint32_t texture, uint8_t blend, bool depth_test, bool depth_write,
                      int cull, bool fog, bool clamp)
{
    const Texture *t = texture < MAX_TEXTURES && g_textures[texture].pixels != NULL
                           ? &g_textures[texture]
                           : NULL;
    const uint32_t tex = t != NULL ? texture + 1u : 0u;
    const uint32_t state = (uint32_t)blend | (depth_test ? 4u : 0u) | (depth_write ? 8u : 0u) |
                           ((uint32_t)cull << 4) | (fog ? 64u : 0u) | (clamp ? 128u : 0u) |
                           (tex != 0 ? 256u : 0u);
    const uint32_t changed = g_state ^ state;

    if (changed == 0 && tex == g_state_texture) {
        return;
    }
    batch_flush();
    if (g_state == 0xFFFFFFFFu || (changed & 3u) != 0) {
        toggle(GU_BLEND, blend == RR_BLEND_TRANSLUCENT);
        toggle(GU_ALPHA_TEST, blend == RR_BLEND_PUNCH_THROUGH);
    }
    if (g_state == 0xFFFFFFFFu || (changed & 4u) != 0) {
        toggle(GU_DEPTH_TEST, depth_test);
    }
    if (g_state == 0xFFFFFFFFu || (changed & 8u) != 0) {
        sceGuDepthMask(depth_write ? GU_FALSE : GU_TRUE); /* the mask says what is kept out */
    }
    if (g_state == 0xFFFFFFFFu || (changed & 0x30u) != 0) {
        toggle(GU_CULL_FACE, cull != CULL_NONE);
        /* Front faces are counter-clockwise, as in Godot. */
        sceGuFrontFace(cull == CULL_FRONT ? GU_CW : GU_CCW);
    }
    if (g_state == 0xFFFFFFFFu || (changed & 64u) != 0) {
        toggle(GU_FOG, fog);
    }
    if (g_state == 0xFFFFFFFFu || (changed & 256u) != 0) {
        toggle(GU_TEXTURE_2D, tex != 0);
    }
    if (tex != 0 && (tex != g_state_texture || g_state == 0xFFFFFFFFu || (changed & 128u) != 0)) {
        sceGuTexMode(t->format, 0, 0, GU_TRUE);
        sceGuTexImage(0, t->width, t->height, t->width, t->pixels);
        sceGuTexFilter(t->filter, t->filter);
        sceGuTexWrap(clamp ? GU_CLAMP : GU_REPEAT, clamp ? GU_CLAMP : GU_REPEAT);
    }
    g_list_left -= g_list_left > 96 ? 96 : g_list_left;
    g_state = state;
    g_state_texture = tex;
}

/* The transform of what is drawn next; NULL for things that are in world space. */
static void set_model(const RRMat34 *world)
{
    ScePspFMatrix4 model;
    const float   *m;
    int            c, r;

    if (world == NULL) {
        if (g_model_identity) {
            return;
        }
        batch_flush();
        memset(&model, 0, sizeof(model));
        model.x.x = model.y.y = model.z.z = model.w.w = 1.0f;
        sceGuSetMatrix(GU_MODEL, &model);
        memcpy(g_mvp, g_view_proj, sizeof(g_mvp));
        g_model_identity = true;
        return;
    }
    batch_flush();
    m = world->m;
    model.x.x = m[0];  model.x.y = m[1];  model.x.z = m[2];  model.x.w = 0.0f;
    model.y.x = m[3];  model.y.y = m[4];  model.y.z = m[5];  model.y.w = 0.0f;
    model.z.x = m[6];  model.z.y = m[7];  model.z.z = m[8];  model.z.w = 0.0f;
    model.w.x = m[9];  model.w.y = m[10]; model.w.z = m[11]; model.w.w = 1.0f;
    sceGuSetMatrix(GU_MODEL, &model);
    for (r = 0; r < 4; r++) {
        const float a = g_view_proj[r], b = g_view_proj[4 + r], d = g_view_proj[8 + r];
        for (c = 0; c < 3; c++) {
            g_mvp[c * 4 + r] = a * m[c * 3] + b * m[c * 3 + 1] + d * m[c * 3 + 2];
        }
        g_mvp[12 + r] = a * m[9] + b * m[10] + d * m[11] + g_view_proj[12 + r];
    }
    g_model_identity = false;
    g_list_left -= g_list_left > 64 ? 64 : g_list_left;
}

/* --- cutting ------------------------------------------------------------------------- */

static inline void to_clip(float *clip, const float *p)
{
    const float *m = g_mvp;

    clip[0] = m[0] * p[0] + m[4] * p[1] + m[8] * p[2] + m[12];
    clip[1] = m[1] * p[0] + m[5] * p[1] + m[9] * p[2] + m[13];
    clip[2] = m[2] * p[0] + m[6] * p[1] + m[10] * p[2] + m[14];
    clip[3] = m[3] * p[0] + m[7] * p[1] + m[11] * p[2] + m[15];
}

/* How far inside a plane a point is; negative outside. */
static inline float plane_side(const float *clip, uint32_t plane)
{
    switch (plane) {
    case PLANE_NEAR:   return clip[2] + clip[3];
    case PLANE_LEFT:   return GUARD_X * clip[3] + clip[0];
    case PLANE_RIGHT:  return GUARD_X * clip[3] - clip[0];
    case PLANE_BOTTOM: return GUARD_Y * clip[3] + clip[1];
    default:           return GUARD_Y * clip[3] - clip[1];
    }
}

/* The planes a point is outside of. */
static inline uint32_t planes_outside(const float *clip)
{
    const float gx = GUARD_X * clip[3], gy = GUARD_Y * clip[3];

    return (clip[2] + clip[3] < 0.0f ? PLANE_NEAR : 0u) | (gx + clip[0] < 0.0f ? PLANE_LEFT : 0u) |
           (gx - clip[0] < 0.0f ? PLANE_RIGHT : 0u) | (gy + clip[1] < 0.0f ? PLANE_BOTTOM : 0u) |
           (gy - clip[1] < 0.0f ? PLANE_TOP : 0u);
}

static uint32_t mix_colour(uint32_t a, uint32_t b, float t)
{
    const uint32_t f = (uint32_t)(t * 256.0f);
    uint32_t       out = 0;
    int            shift;

    if (a == b) {
        return a;
    }
    for (shift = 0; shift < 32; shift += 8) {
        const uint32_t ca = (a >> shift) & 0xFFu, cb = (b >> shift) & 0xFFu;
        out |= ((ca * (256u - f) + cb * f) >> 8) << shift;
    }
    return out;
}

/* Cuts a convex polygon of `count` corners (at most 4) at the planes in `planes` and adds
 * what is left as triangles. `expected` as for batch_reserve. */
static void put_polygon(const ClipVertex *corners, int count, uint32_t planes, uint32_t expected)
{
    ClipVertex   buffer[2][10];
    const ClipVertex *p = corners;
    RRVertexPSP *out;
    uint32_t     plane;
    int          n = count, target = 0, i, k;

    for (plane = PLANE_NEAR; plane <= PLANE_TOP && n >= 3; plane <<= 1) {
        ClipVertex *q = buffer[target];
        int         m = 0;

        if ((planes & plane) == 0) {
            continue;
        }
        for (i = 0; i < n; i++) {
            const ClipVertex *a = &p[i], *b = &p[i + 1 < n ? i + 1 : 0];
            const float       da = plane_side(a->clip, plane), db = plane_side(b->clip, plane);

            if (da >= 0.0f) {
                q[m++] = *a;
            }
            if ((da >= 0.0f) != (db >= 0.0f)) {
                const float t = da / (da - db);
                ClipVertex *c = &q[m++];
                for (k = 0; k < 4; k++) {
                    c->clip[k] = a->clip[k] + (b->clip[k] - a->clip[k]) * t;
                }
                for (k = 0; k < 3; k++) {
                    c->v.pos[k] = a->v.pos[k] + (b->v.pos[k] - a->v.pos[k]) * t;
                }
                c->v.uv[0] = a->v.uv[0] + (b->v.uv[0] - a->v.uv[0]) * t;
                c->v.uv[1] = a->v.uv[1] + (b->v.uv[1] - a->v.uv[1]) * t;
                c->v.abgr = mix_colour(a->v.abgr, b->v.abgr, t);
            }
        }
        p = q;
        n = m;
        target ^= 1;
    }
    if (n < 3) {
        return;
    }
    out = batch_reserve((uint32_t)(n - 2) * 3u, GU_TRANSFORM_3D, expected);
    if (out == NULL) {
        return;
    }
    for (i = 1; i + 1 < n; i++) {
        *out++ = p[0].v;
        *out++ = p[i].v;
        *out++ = p[i + 1].v;
    }
}

/* --- frame --------------------------------------------------------------------------- */

static uint32_t to_abgr(uint32_t rgba)
{
    return (rgba >> 24) | ((rgba >> 8) & 0xFF00u) | ((rgba << 8) & 0xFF0000u) | (rgba << 24);
}

void rr_gfx_begin_frame(uint32_t clear_rgba)
{
    sceGuStart(GU_DIRECT, g_list);
    g_list_left = LIST_BYTES;
    g_block = NULL;
    g_block_used = g_block_size = g_batch_first = 0;
    g_batch_transform = GU_TRANSFORM_3D;
    g_state = 0xFFFFFFFFu;
    g_state_texture = 0;
    g_model_identity = false;
    g_translucent_count = 0;
    g_fog = false;
    g_in_frame = true;

    sceGuDepthMask(GU_FALSE);
    sceGuClearColor(to_abgr(clear_rgba) | 0xFF000000u);
    sceGuClearDepth(0);
    sceGuClear(GU_COLOR_BUFFER_BIT | GU_DEPTH_BUFFER_BIT);
}

void rr_gfx_set_camera(const RRMat34 *world, float fov_y_degrees, float z_near, float z_far,
                       float ortho_size)
{
    const float   *m = world->m;
    const float    aspect = (float)SCREEN_W / (float)SCREEN_H;
    const float    depth = 1.0f / (z_near - z_far);
    ScePspFMatrix4 projection, view;
    float          axis[3][3]; /* unit axes of the camera; it looks along -Z */
    const float   *p = (const float *)&projection, *v = (const float *)&view;
    int            i, k, r, c;

    for (i = 0; i < 3; i++) {
        const float *a = &m[i * 3];
        const float  length = sqrtf(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
        const float  inv = length > 0.0f ? 1.0f / length : 0.0f;
        for (k = 0; k < 3; k++) {
            axis[i][k] = a[k] * inv;
        }
    }
    memset(&view, 0, sizeof(view));
    view.x.x = axis[0][0]; view.x.y = axis[1][0]; view.x.z = axis[2][0];
    view.y.x = axis[0][1]; view.y.y = axis[1][1]; view.y.z = axis[2][1];
    view.z.x = axis[0][2]; view.z.y = axis[1][2]; view.z.z = axis[2][2];
    view.w.x = -(axis[0][0] * m[9] + axis[0][1] * m[10] + axis[0][2] * m[11]);
    view.w.y = -(axis[1][0] * m[9] + axis[1][1] * m[10] + axis[1][2] * m[11]);
    view.w.z = -(axis[2][0] * m[9] + axis[2][1] * m[10] + axis[2][2] * m[11]);
    view.w.w = 1.0f;

    /* Clip space as in OpenGL: the near plane is at z = -w, the far one at z = w. */
    memset(&projection, 0, sizeof(projection));
    g_ortho = ortho_size > 0.0f;
    if (g_ortho) {
        g_scale_y = 2.0f / ortho_size;
        g_scale_x = g_scale_y / aspect;
        projection.z.z = 2.0f * depth;
        projection.w.z = (z_far + z_near) * depth;
        projection.w.w = 1.0f;
    } else {
        g_scale_y = 1.0f / tanf(fov_y_degrees * (3.14159265f / 360.0f));
        g_scale_x = g_scale_y / aspect;
        projection.z.z = (z_far + z_near) * depth;
        projection.z.w = -1.0f;
        projection.w.z = 2.0f * z_far * z_near * depth;
    }
    projection.x.x = g_scale_x;
    projection.y.y = g_scale_y;

    batch_flush();
    sceGuSetMatrix(GU_PROJECTION, &projection);
    sceGuSetMatrix(GU_VIEW, &view);
    for (c = 0; c < 4; c++) {
        for (r = 0; r < 4; r++) {
            g_view_proj[c * 4 + r] = p[r] * v[c * 4] + p[4 + r] * v[c * 4 + 1] +
                                     p[8 + r] * v[c * 4 + 2] + p[12 + r] * v[c * 4 + 3];
        }
    }
    g_model_identity = false;
    g_near = z_near;
    g_eye[0] = m[9];
    g_eye[1] = m[10];
    g_eye[2] = m[11];
}

/* The chip's fog grows evenly between two distances. Exponential fog becomes the even
 * fog that is as thick at two thirds of the way to where it has swallowed 95 %. */
void rr_gfx_set_fog(const RRPkgEnvironmentNode *environment)
{
    const RRPkgEnvironmentNode *e = environment;
    float                       begin, end;

    g_fog = e != NULL && e->fog_enabled;
    if (!g_fog) {
        return;
    }
    if (e->fog_mode == RR_FOG_DEPTH) {
        begin = e->fog_begin;
        end = e->fog_end > begin ? e->fog_end : begin + 0.001f;
    } else {
        begin = 0.0f;
        end = e->fog_density > 0.0f ? 2.3f / e->fog_density : 1.0e9f;
    }
    batch_flush();
    sceGuFog(begin, end, to_abgr(e->fog_color) & 0xFFFFFFu);
}

/* --- drawing ------------------------------------------------------------------------- */

/* The slow path for one strip: every triangle that leaves the square the chip works in,
 * or the near plane, is cut; the others are copied. */
static void put_strip_cut(const RRVertexPSP *v, uint32_t count, uint32_t expected)
{
    ClipVertex c[3]; /* the last three vertices; vertex k is in c[k % 3] */
    uint32_t   outside[3];
    uint32_t   k;

    for (k = 0; k < count; k++) {
        ClipVertex *d = &c[k % 3];

        to_clip(d->clip, v[k].pos);
        d->v = v[k];
        outside[k % 3] = planes_outside(d->clip);
        if (k >= 2) {
            /* Every second triangle of a strip is wound the other way round. */
            const bool        flip = ((k - 2) & 1u) != 0;
            const uint32_t    ia = (k - 2) % 3, ib = (k - 1) % 3, id = k % 3;
            const uint32_t    any = outside[ia] | outside[ib] | outside[id];

            if ((outside[ia] & outside[ib] & outside[id]) != 0) {
                continue; /* wholly outside one plane */
            }
            if (any == 0) {
                RRVertexPSP *out = batch_reserve(3, GU_TRANSFORM_3D, expected);
                if (out == NULL) {
                    return;
                }
                out[0] = c[flip ? ib : ia].v;
                out[1] = c[flip ? ia : ib].v;
                out[2] = d->v;
            } else {
                ClipVertex triangle[3];
                triangle[0] = c[flip ? ib : ia];
                triangle[1] = c[flip ? ia : ib];
                triangle[2] = *d;
                put_polygon(triangle, 3, any, expected);
            }
        }
    }
}

static void draw_surface(const RRPkgMesh *mesh, const RRPkgSurface *surf, const RRMat34 *world,
                         const uint32_t *textures, bool depth_write)
{
    const float *m = world->m;
    /* A mirroring transform turns the faces round on the screen. */
    const float  det = m[0] * (m[4] * m[8] - m[5] * m[7]) - m[1] * (m[3] * m[8] - m[5] * m[6]) +
                       m[2] * (m[3] * m[7] - m[4] * m[6]);
    const float  sx = m[0] * m[0] + m[1] * m[1] + m[2] * m[2];
    const float  sy = m[3] * m[3] + m[4] * m[4] + m[5] * m[5];
    const float  sz = m[6] * m[6] + m[7] * m[7] + m[8] * m[8];
    const float  radius = mesh->radius * sqrtf(sx > sy ? (sx > sz ? sx : sz) : (sy > sz ? sy : sz));
    const uint16_t    *strips = rr_surface_strips(mesh, surf);
    const RRVertexPSP *v = (const RRVertexPSP *)rr_surface_verts(mesh, surf);
    float        center[4], room;
    bool         cut;
    uint32_t     i;

    set_model(world);
    set_state(surf->texture != RR_NONE32 ? textures[surf->texture] : RR_NONE32, surf->blend, true,
              depth_write, det < 0.0f ? CULL_FRONT : CULL_BACK, g_fog, false);

    /* Only a mesh whose bounding sphere reaches out of the chip's square, or in front of
     * the near plane, needs the path that looks at every triangle. With perspective the
     * square grows with the distance; `room` is the distance of the nearest point. */
    to_clip(center, mesh->center);
    room = g_ortho ? 1.0f : center[3] - radius;
    cut = (!g_ortho && room <= g_near) ||
          fabsf(center[0]) + radius * g_scale_x > GUARD_X * room ||
          fabsf(center[1]) + radius * g_scale_y > GUARD_Y * room;

    if (cut) {
        for (i = 0; i < surf->strip_count; i++) {
            if (strips[i] >= 3) {
                put_strip_cut(v, strips[i], (surf->vertex_count - surf->strip_count * 2u) * 3u);
            }
            v += strips[i];
        }
        return;
    }
    if (g_list_left < surf->strip_count * 20u + LIST_RESERVE) {
        list_memory(surf->strip_count * 20u); /* prints the message */
        return;
    }
    batch_flush();
    for (i = 0; i < surf->strip_count; i++) {
        if (strips[i] >= 3) {
            sceGuDrawArray(GU_TRIANGLE_STRIP, VERTEX_FORMAT | GU_TRANSFORM_3D, strips[i], NULL, v);
        }
        v += strips[i];
    }
    g_list_left -= surf->strip_count * 20u;
}

/* Translucent surfaces are not drawn where they come in the tree: as in Godot they wait
 * until everything solid is there, and are then drawn from the far ones to the near ones
 * without writing the depth, so that what is behind or inside one stays visible. */
void rr_gfx_draw_mesh(const RRPkgMesh *mesh, const RRMat34 *world, const uint32_t *textures)
{
    uint32_t s;
    int      k;

    for (s = 0; s < mesh->surface_count; s++) {
        const RRPkgSurface *surf = &mesh->surfaces[s];

        if (surf->blend == RR_BLEND_TRANSLUCENT && g_translucent_count < MAX_TRANSLUCENT) {
            Translucent *t = &g_translucent[g_translucent_count++];
            const float *m = world->m, *c = mesh->center;
            t->mesh     = mesh;
            t->surface  = surf;
            t->textures = textures;
            t->world    = *world;
            t->distance = 0.0f;
            for (k = 0; k < 3; k++) {
                const float d = m[k] * c[0] + m[3 + k] * c[1] + m[6 + k] * c[2] + m[9 + k] - g_eye[k];
                t->distance += d * d;
            }
        } else {
            draw_surface(mesh, surf, world, textures, true);
        }
    }
}

/* Draws the translucent surfaces that were held back. Called when the meshes of the
 * frame are through: before the first particle, or at the end of the 3D part. */
static void draw_translucent(void)
{
    uint32_t i, k;

    for (i = 0; i < g_translucent_count; i++) {
        /* Sorted by insertion, the furthest first; equal ones stay in tree order. */
        for (k = i; k > 0 && g_translucent[g_translucent_order[k - 1]].distance < g_translucent[i].distance; k--) {
            g_translucent_order[k] = g_translucent_order[k - 1];
        }
        g_translucent_order[k] = (uint16_t)i;
    }
    for (i = 0; i < g_translucent_count; i++) {
        const Translucent *t = &g_translucent[g_translucent_order[i]];
        draw_surface(t->mesh, t->surface, &t->world, t->textures, false);
    }
    g_translucent_count = 0;
}

void rr_gfx_mesh_changed(const RRPkgMesh *mesh, uint32_t size)
{
    sceKernelDcacheWritebackRange(mesh, size);
}

/* A quad in world space: corners and `uv` as in rr_gfx_draw_quad_3d. The state is set. */
static void draw_world_quad(const float *xyz, const float *uv, uint32_t abgr)
{
    ClipVertex c[4];
    uint32_t   any = 0, all = 0xFFFFFFFFu;
    int        i;

    set_model(NULL);
    for (i = 0; i < 4; i++) {
        uint32_t outside;

        memcpy(c[i].v.pos, &xyz[i * 3], sizeof(c[i].v.pos));
        c[i].v.uv[0] = uv[i * 2];
        c[i].v.uv[1] = uv[i * 2 + 1];
        c[i].v.abgr = abgr;
        to_clip(c[i].clip, c[i].v.pos);
        outside = planes_outside(c[i].clip);
        any |= outside;
        all &= outside;
    }
    if (all == 0) {
        put_polygon(c, 4, any, 96);
    }
}

void rr_gfx_draw_sky(uint32_t texture, const float *xyz, const float *uv)
{
    /* Neither tested against the depth nor written to it, so the meshes that follow cover
     * the sky wherever they are. */
    set_state(texture, RR_BLEND_OPAQUE, false, false, CULL_NONE, false, true);
    draw_world_quad(xyz, uv, 0xFFFFFFFFu);
}

void rr_gfx_draw_quad_3d(uint32_t texture, const float *xyz, const float *uv, uint32_t rgba)
{
    if (g_translucent_count != 0) {
        draw_translucent();
    }
    /* Particles are tested against the depth buffer but do not write to it, so they do
     * not cut holes into each other. */
    set_state(texture, RR_BLEND_TRANSLUCENT, true, false, CULL_NONE, g_fog, false);
    draw_world_quad(xyz, uv, to_abgr(rgba));
}

void rr_gfx_begin_2d(void)
{
    if (g_translucent_count != 0) {
        draw_translucent();
    }
    g_fog = false;
}

void rr_gfx_draw_quad(uint32_t texture, const float *xy, const float *uv, uint32_t rgba)
{
    /* Two triangles: top left, top right, bottom left; top right, bottom right, bottom left. */
    static const int order[6] = { 0, 1, 3, 1, 2, 3 };
    const float      scale_x = (float)SCREEN_W / (float)RR_SCREEN_WIDTH;
    const float      scale_y = (float)SCREEN_H / (float)RR_SCREEN_HEIGHT;
    const uint32_t   abgr = to_abgr(rgba);
    const Texture   *t;
    RRVertexPSP     *out;
    float            width = 1.0f, height = 1.0f;
    int              i;

    /* Without a depth test every quad lies over what was drawn before it. */
    set_state(texture, RR_BLEND_TRANSLUCENT, false, false, CULL_NONE, false, false);
    out = batch_reserve(6, GU_TRANSFORM_2D, 192);
    if (out == NULL) {
        return;
    }
    /* Vertices that are not transformed have their texture coordinates in pixels. */
    if (g_state_texture != 0) {
        t = &g_textures[g_state_texture - 1];
        width = (float)t->width;
        height = (float)t->height;
    }
    for (i = 0; i < 6; i++) {
        const int c = order[i];
        out[i].uv[0] = uv[c * 2] * width;
        out[i].uv[1] = uv[c * 2 + 1] * height;
        out[i].abgr = abgr;
        out[i].pos[0] = xy[c * 2] * scale_x;
        out[i].pos[1] = xy[c * 2 + 1] * scale_y;
        out[i].pos[2] = 0.0f;
    }
}

void rr_gfx_end_frame(void)
{
    if (!g_in_frame) {
        return;
    }
    rr_gfx_begin_2d(); /* draws what was held back if the scene had no 2D pass */
    batch_flush();
    sceGuFinish();
    sceGuSync(GU_SYNC_FINISH, GU_SYNC_WHAT_DONE);
    sceDisplayWaitVblankStart();
    sceGuSwapBuffers();
    g_in_frame = false;
}

/* --- textures ------------------------------------------------------------------------ */

/* The chip reads a texture fastest when its pixels are stored in blocks of 16 bytes by 8
 * rows ("swizzled"), which keeps neighbours in both directions close in memory. */
static void swizzle(uint8_t *out, const uint8_t *in, uint32_t row_bytes, uint32_t height)
{
    const uint32_t blocks_x = row_bytes / 16, blocks_y = height / 8;
    uint32_t      *to = (uint32_t *)(void *)out;
    uint32_t       bx, by, row;

    for (by = 0; by < blocks_y; by++) {
        for (bx = 0; bx < blocks_x; bx++) {
            const uint32_t *from = (const uint32_t *)(const void *)(in + by * 8 * row_bytes + bx * 16);
            for (row = 0; row < 8; row++) {
                *to++ = from[0];
                *to++ = from[1];
                *to++ = from[2];
                *to++ = from[3];
                from += row_bytes / 4;
            }
        }
    }
}

uint32_t rr_gfx_texture_load(const RRPkgTexture *texture)
{
    const uint32_t size = (uint32_t)texture->width * texture->height * 2u;
    uint8_t        format;
    uint32_t       i;

    switch (texture->format) {
    case RR_TEX_BGR565:   format = GU_PSM_5650; break;
    case RR_TEX_ABGR1555: format = GU_PSM_5551; break;
    case RR_TEX_ABGR4444: format = GU_PSM_4444; break;
    default:              return RR_NONE32;
    }
    /* The baker makes every side a power of two of at least 8, which is what the blocks
     * of swizzle() need. */
    if (texture->width < 8 || texture->height < 8 || (texture->width & (texture->width - 1)) != 0 ||
        (texture->height & (texture->height - 1)) != 0) {
        return RR_NONE32;
    }
    for (i = 0; i < MAX_TEXTURES; i++) {
        Texture *t = &g_textures[i];
        if (t->pixels == NULL) {
            t->pixels = vramalloc(size);
            t->in_vram = t->pixels != NULL;
            if (t->pixels == NULL) {
                t->pixels = memalign(16, size);
            }
            if (t->pixels == NULL) {
                printf("retro: no memory for a texture of %u x %u\n", (unsigned)texture->width,
                       (unsigned)texture->height);
                return RR_NONE32;
            }
            swizzle((uint8_t *)t->pixels, (const uint8_t *)(texture + 1), texture->width * 2u,
                    texture->height);
            sceKernelDcacheWritebackRange(t->pixels, size);
            t->width = texture->width;
            t->height = texture->height;
            t->format = format;
            t->filter = texture->filter == RR_FILTER_NEAREST ? GU_NEAREST : GU_LINEAR;
            return i;
        }
    }
    printf("retro: more than %d textures, one is missing\n", MAX_TEXTURES);
    return RR_NONE32;
}

void rr_gfx_texture_unload(uint32_t handle)
{
    /* A frame is drawn to its end before rr_gfx_end_frame returns, so nothing that is on
     * its way to the screen still uses the texture. */
    if (handle < MAX_TEXTURES && g_textures[handle].pixels != NULL) {
        if (g_textures[handle].in_vram) {
            vfree(g_textures[handle].pixels);
        } else {
            free(g_textures[handle].pixels);
        }
        g_textures[handle].pixels = NULL;
    }
}
