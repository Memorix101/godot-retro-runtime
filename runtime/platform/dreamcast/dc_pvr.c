/* Dreamcast graphics straight on the PowerVR, through KallistiOS' PVR API.
 *
 * The baked data is already in PVR form: triangle strips, packed ARGB colours, 16-bit
 * textures. What is left to do per vertex is one matrix transform (the SH-4's ftrv), one
 * division and eight words written to a list.
 *
 * The three polygon lists (opaque, punch-through, translucent) are filled in main memory
 * while the frame is put together, in whatever order the draw calls come, and go to the
 * tile accelerator by DMA when the frame ends. Strips that reach in front of the near
 * plane are clipped here; the hardware clips to the screen edges by itself.
 *
 * Depth is 1 / distance along the view direction, as the PVR wants it. The translucent
 * list is not sorted by the hardware: it is drawn in the order of the calls. */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include <kos.h>
#include <dc/matrix.h>
#include <dc/pvr.h>

#include "../../core/rr_platform.h"
#include "dc_gfx.h"

#define MAX_TEXTURES 256

/* Room in each list for one frame, in units of 32 bytes: a vertex or a polygon header.
 * Each buffer exists twice in main memory (the frame that is built and the one that is
 * transferred), and the tile accelerator keeps as much again in video memory. The
 * highway demo needs up to some 20500 solid units with its LOD levels (FPS=1 prints the
 * peaks). */
#define OP_UNITS 24576
#define PT_UNITS 4096
#define TR_UNITS 4096
/* Kept free at the end of a list: KallistiOS adds an empty header and an end marker. */
#define LIST_RESERVE 3

/* The alpha value from which on a punch-through pixel is drawn; a register that
 * KallistiOS has no name for. */
#define PVR_PT_ALPHA_REF 0x011c

enum { LIST_OP, LIST_PT, LIST_TR, LIST_COUNT };

typedef struct {
    pvr_list_t    id;
    uint32_t      units;
    pvr_vertex_t *tail;   /* where the next unit goes */
    uint32_t      left;   /* units that still fit in this frame */
    uint32_t      state;  /* the polygon header that is in effect, see begin_poly */
    bool          warned; /* the overflow message was printed */
} List;

typedef struct {
    pvr_ptr_t         pixels; /* NULL: free slot */
    uint16_t          width, height;
    int               format;
    pvr_filter_mode_t filter;
} Texture;

/* A vertex between the transform and the division. x and y are screen coordinates times
 * w; w is the distance from the camera along its view direction. */
typedef struct {
    float    x, y, w;
    float    u, v;
    uint32_t argb;
} ClipVertex;

static uint8_t g_op_buffer[OP_UNITS * 64] __attribute__((aligned(32)));
static uint8_t g_pt_buffer[PT_UNITS * 64] __attribute__((aligned(32)));
static uint8_t g_tr_buffer[TR_UNITS * 64] __attribute__((aligned(32)));

static List g_lists[LIST_COUNT] = {
    { PVR_LIST_OP_POLY, OP_UNITS, NULL, 0, 0, false },
    { PVR_LIST_PT_POLY, PT_UNITS, NULL, 0, 0, false },
    { PVR_LIST_TR_POLY, TR_UNITS, NULL, 0, 0, false },
};
static Texture  g_textures[MAX_TEXTURES];
static matrix_t g_view;         /* world space to ClipVertex */
static bool     g_view_loaded;  /* g_view is what the SH-4's matrix register holds */
static float    g_near, g_far;
/* An orthogonal camera: 0 for none. The picture then does not shrink with the distance,
 * but the PVR still wants 1 / depth for its depth test, and bends textures by it. So the
 * depth is counted from this far behind the camera and scaled to end up just below 1
 * (in front of the background, which is far away), which keeps the order of things and
 * makes the bending too small to see. */
static float    g_ortho_base;
static bool     g_fog;          /* fog applies to what is drawn now */
static struct {
    uint32_t color;
    uint8_t  mode;
    float    a, b, far;
} g_fog_table;                  /* what the fog table was last filled for */

bool dc_gfx_init(void)
{
    pvr_init_params_t params;

    memset(&params, 0, sizeof(params));
    params.opb_sizes[PVR_LIST_OP_POLY] = PVR_BINSIZE_32;
    params.opb_sizes[PVR_LIST_TR_POLY] = PVR_BINSIZE_32;
    params.opb_sizes[PVR_LIST_PT_POLY] = PVR_BINSIZE_32;
    params.vertex_buf_size = (OP_UNITS + PT_UNITS + TR_UNITS) * 32;
    params.dma_enabled = 1;
    params.autosort_disabled = 1;
    params.opb_overflow_count = 3;
    if (pvr_init(&params) < 0) {
        printf("retro: pvr_init failed\n");
        return false;
    }
    pvr_set_vertbuf(PVR_LIST_OP_POLY, g_op_buffer, sizeof(g_op_buffer));
    pvr_set_vertbuf(PVR_LIST_PT_POLY, g_pt_buffer, sizeof(g_pt_buffer));
    pvr_set_vertbuf(PVR_LIST_TR_POLY, g_tr_buffer, sizeof(g_tr_buffer));
    PVR_SET(PVR_PT_ALPHA_REF, 0x80);
    /* With face culling on, the PVR also leaves out triangles whose area on the screen is
     * below the value in this register (a float). KallistiOS does not set it, so it is
     * whatever was there at start-up. Zero keeps every triangle; emulators ignore it. */
    PVR_SET(PVR_OBJECT_CLIP, 0);
    g_fog_table.mode = 0xFF;
    return true;
}

void dc_gfx_shutdown(void)
{
    pvr_shutdown();
}

/* --- lists --------------------------------------------------------------------------- */

#ifdef RR_PRINT_FPS
/* With FPS=1: how full the lists get, printed every 120 frames next to the rate. */
static uint32_t g_stat_peak[LIST_COUNT];
static uint32_t g_stat_frames, g_stat_full_frames;
static bool     g_stat_full;
#endif

static void list_full(List *list)
{
#ifdef RR_PRINT_FPS
    g_stat_full = true;
#endif
    if (!list->warned) {
        list->warned = true;
        printf("retro: PVR list %d is full (%u units per frame), polygons are left out\n",
               (int)list->id, (unsigned)list->units);
    }
}

/* Makes the polygon header for what follows in `list`. Strips end by themselves, so a
 * header stays in effect until the next one: when the state is the one of the previous
 * call, nothing is written. False when the list is full. */
static bool begin_poly(List *list, uint32_t texture, pvr_cull_mode_t cull, bool depth_test,
                       bool depth_write, bool fog)
{
    const Texture *t = texture < MAX_TEXTURES && g_textures[texture].pixels != NULL
                           ? &g_textures[texture]
                           : NULL;
    const uint32_t state = (t != NULL ? texture + 1u : 0u) | ((uint32_t)cull << 16) |
                           (depth_test ? 1u << 18 : 0u) | (depth_write ? 1u << 19 : 0u) |
                           (fog ? 1u << 20 : 0u);
    pvr_poly_cxt_t cxt;

    if (state == list->state) {
        return true;
    }
    if (list->left == 0) {
        list_full(list);
        return false;
    }
    if (t != NULL) {
        pvr_poly_cxt_txr(&cxt, list->id, t->format, t->width, t->height, t->pixels, t->filter);
        /* Texture times vertex colour, alpha included. */
        cxt.txr.env = PVR_TXRENV_MODULATEALPHA;
    } else {
        pvr_poly_cxt_col(&cxt, list->id);
    }
    cxt.gen.culling = cull;
    cxt.gen.fog_type = fog ? PVR_FOG_TABLE : PVR_FOG_DISABLE;
    cxt.depth.comparison = depth_test ? PVR_DEPTHCMP_GEQUAL : PVR_DEPTHCMP_ALWAYS;
    cxt.depth.write = depth_write ? PVR_DEPTHWRITE_ENABLE : PVR_DEPTHWRITE_DISABLE;
    pvr_poly_compile((pvr_poly_hdr_t *)list->tail, &cxt);
    list->tail++;
    list->left--;
    list->state = state;
    return true;
}

/* Divides and writes one vertex. `depth_scale` is 1 except for the sky. */
static inline pvr_vertex_t *put(pvr_vertex_t *out, const ClipVertex *c, float depth_scale,
                                uint32_t flags)
{
    const float inv_w = g_ortho_base > 0.0f ? 1.0f : 1.0f / c->w;

    /* The whole cache line is written, so it need not be read from memory first. */
    dcache_alloc_line_with_value(out, flags);
    out->x = c->x * inv_w;
    out->y = c->y * inv_w;
    out->z = (g_ortho_base > 0.0f ? g_ortho_base / (g_ortho_base + c->w) : inv_w) * depth_scale;
    out->u = c->u;
    out->v = c->v;
    out->argb = c->argb;
    out->oargb = 0;
    return out + 1;
}

/* Transforms a position by the matrix register. */
static inline void to_clip(ClipVertex *c, const float *pos)
{
    float x = pos[0], y = pos[1], z = pos[2], w;

    mat_trans_single3_nodivw(x, y, z, w);
    (void)z;
    c->x = x;
    c->y = y;
    c->w = w;
}

static uint32_t mix_argb(uint32_t a, uint32_t b, float t)
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

/* Cuts a convex polygon of `count` corners at the near plane and writes what is left
 * as one strip. Writes at most count + 1 vertices. */
static pvr_vertex_t *put_polygon(pvr_vertex_t *out, const ClipVertex *p, int count,
                                 float depth_scale)
{
    ClipVertex q[8];
    int        n = 0, i, low, high;

    for (i = 0; i < count; i++) {
        const ClipVertex *a = &p[i], *b = &p[i + 1 < count ? i + 1 : 0];
        const bool        a_in = a->w >= g_near, b_in = b->w >= g_near;

        if (a_in) {
            q[n++] = *a;
        }
        if (a_in != b_in) {
            const float t = (g_near - a->w) / (b->w - a->w);
            ClipVertex *c = &q[n++];
            c->x = a->x + (b->x - a->x) * t;
            c->y = a->y + (b->y - a->y) * t;
            c->w = g_near;
            c->u = a->u + (b->u - a->u) * t;
            c->v = a->v + (b->v - a->v) * t;
            c->argb = mix_argb(a->argb, b->argb, t);
        }
    }
    if (n < 3) {
        return out;
    }
    /* Around the polygon from both ends: 0, 1, n-1, 2, n-2, ... keeps the winding. */
    out = put(out, &q[0], depth_scale, PVR_CMD_VERTEX);
    low = 1;
    high = n - 1;
    while (low <= high) {
        out = put(out, &q[low++], depth_scale, PVR_CMD_VERTEX);
        if (low <= high) {
            out = put(out, &q[high--], depth_scale, PVR_CMD_VERTEX);
        }
    }
    out[-1].flags = PVR_CMD_VERTEX_EOL;
    return out;
}

/* A strip of a mesh that may reach in front of the near plane. Runs of triangles that
 * are wholly behind it stay strips, the others are cut one by one. Writes at most four
 * vertices per triangle. */
static pvr_vertex_t *put_strip_clipped(pvr_vertex_t *out, const RRVertexDC *v, uint32_t count)
{
    ClipVertex c[3]; /* the last three vertices; vertex k is in c[k % 3] */
    bool       run = false;
    uint32_t   k;

    for (k = 0; k < count; k++) {
        ClipVertex *d = &c[k % 3];

        to_clip(d, v[k].pos);
        d->u = v[k].uv[0];
        d->v = v[k].uv[1];
        d->argb = v[k].argb;
        if (k >= 2) {
            const ClipVertex *a = &c[(k - 2) % 3], *b = &c[(k - 1) % 3];
            const int         inside = (a->w >= g_near) + (b->w >= g_near) + (d->w >= g_near);

            if (inside == 3) {
                if (!run) {
                    /* A new strip. Every second triangle of a strip is wound the other
                     * way round, so one that starts at such a triangle begins with an
                     * empty triangle. */
                    if (((k - 2) & 1u) != 0) {
                        out = put(out, a, 1.0f, PVR_CMD_VERTEX);
                    }
                    out = put(out, a, 1.0f, PVR_CMD_VERTEX);
                    out = put(out, b, 1.0f, PVR_CMD_VERTEX);
                    run = true;
                }
                out = put(out, d, 1.0f, PVR_CMD_VERTEX);
            } else {
                if (run) {
                    out[-1].flags = PVR_CMD_VERTEX_EOL;
                    run = false;
                }
                if (inside != 0) {
                    ClipVertex triangle[3];
                    const bool flip = ((k - 2) & 1u) != 0;
                    triangle[0] = flip ? *b : *a;
                    triangle[1] = flip ? *a : *b;
                    triangle[2] = *d;
                    out = put_polygon(out, triangle, 3, 1.0f);
                }
            }
        }
    }
    if (run) {
        out[-1].flags = PVR_CMD_VERTEX_EOL;
    }
    return out;
}

/* --- frame --------------------------------------------------------------------------- */

void rr_gfx_begin_frame(uint32_t clear_rgba)
{
    int i;

    pvr_set_bg_color((float)(clear_rgba >> 24) * (1.0f / 255.0f),
                     (float)((clear_rgba >> 16) & 0xFFu) * (1.0f / 255.0f),
                     (float)((clear_rgba >> 8) & 0xFFu) * (1.0f / 255.0f));
    pvr_scene_begin();
    for (i = 0; i < LIST_COUNT; i++) {
        List *list = &g_lists[i];
        list->tail = (pvr_vertex_t *)pvr_vertbuf_tail(list->id);
        list->left = list->units - LIST_RESERVE;
        list->state = 0xFFFFFFFFu;
    }
    g_fog = false;
    g_view_loaded = false;
}

void rr_gfx_end_frame(void)
{
    int i;

    for (i = 0; i < LIST_COUNT; i++) {
        const List    *list = &g_lists[i];
        const uint32_t used = list->units - LIST_RESERVE - list->left;
        if (used != 0) {
            pvr_vertbuf_written(list->id, used * 32u);
        }
#ifdef RR_PRINT_FPS
        if (used > g_stat_peak[i]) {
            g_stat_peak[i] = used;
        }
#endif
    }
#ifdef RR_PRINT_FPS
    g_stat_full_frames += g_stat_full ? 1u : 0u;
    g_stat_full = false;
    if (++g_stat_frames == 120) {
        printf("pvr: peak units op %u/%u pt %u/%u tr %u/%u, full in %u of 120 frames, %u KB video memory free\n",
               (unsigned)g_stat_peak[LIST_OP], (unsigned)OP_UNITS, (unsigned)g_stat_peak[LIST_PT],
               (unsigned)PT_UNITS, (unsigned)g_stat_peak[LIST_TR], (unsigned)TR_UNITS,
               (unsigned)g_stat_full_frames, (unsigned)(pvr_mem_available() / 1024));
        memset(g_stat_peak, 0, sizeof(g_stat_peak));
        g_stat_frames = g_stat_full_frames = 0;
    }
#endif
    /* Waits until the tile accelerator is free, then hands the lists over. */
    pvr_scene_finish();
}

void rr_gfx_set_camera(const RRMat34 *world, float fov_y_degrees, float z_near, float z_far,
                       float ortho_size)
{
    const float *m = world->m;
    const float  tan_y = tanf(fov_y_degrees * (F_PI / 360.0f));
    const float  tan_x = tan_y * ((float)RR_SCREEN_WIDTH / (float)RR_SCREEN_HEIGHT);
    const float  half_w = (float)RR_SCREEN_WIDTH * 0.5f, half_h = (float)RR_SCREEN_HEIGHT * 0.5f;
    float        axis[3][3]; /* unit axes of the camera; it looks along -Z */
    float        row[3][3];  /* x * w, y * w and w as functions of a world position */
    int          i, k;

    for (i = 0; i < 3; i++) {
        const float *a = &m[i * 3];
        const float  length = sqrtf(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
        const float  inv = length > 0.0f ? 1.0f / length : 0.0f;
        for (k = 0; k < 3; k++) {
            axis[i][k] = a[k] * inv;
        }
    }
    /* With e the position seen from the camera: w = -e.z, and on the screen
     * x = half_w + e.x / (w * tan_x) * half_w,  y = half_h - e.y / (w * tan_y) * half_h. */
    for (k = 0; k < 3; k++) {
        if (ortho_size > 0.0f) {
            /* The view is ortho_size high whatever the distance:
             * x = half_w + e.x / (ortho_size / 2 * aspect) * half_w, and so on. The rows
             * give x and y themselves, and w is still the depth. */
            row[0][k] = axis[0][k] * ((float)RR_SCREEN_HEIGHT / ortho_size);
            row[1][k] = -axis[1][k] * ((float)RR_SCREEN_HEIGHT / ortho_size);
        } else {
            row[0][k] = axis[0][k] * (half_w / tan_x) - axis[2][k] * half_w;
            row[1][k] = -axis[1][k] * (half_h / tan_y) - axis[2][k] * half_h;
        }
        row[2][k] = -axis[2][k];
    }
    memset(g_view, 0, sizeof(g_view));
    for (k = 0; k < 3; k++) {
        g_view[k][0] = row[0][k];
        g_view[k][1] = row[1][k];
        g_view[k][3] = row[2][k];
    }
    g_view[3][0] = -(row[0][0] * m[9] + row[0][1] * m[10] + row[0][2] * m[11]);
    g_view[3][1] = -(row[1][0] * m[9] + row[1][1] * m[10] + row[1][2] * m[11]);
    g_view[3][3] = -(row[2][0] * m[9] + row[2][1] * m[10] + row[2][2] * m[11]);
    if (ortho_size > 0.0f) {
        g_view[3][0] += half_w;
        g_view[3][1] += half_h;
    }
    g_ortho_base = ortho_size > 0.0f ? z_far * 32.0f : 0.0f;
    g_view_loaded = false;
    g_near = z_near;
    g_far = z_far;
}

static uint32_t fog_at(const RRPkgEnvironmentNode *e, float depth)
{
    float f;

    if (e->fog_mode == RR_FOG_DEPTH) {
        f = (depth - e->fog_begin) / (e->fog_end - e->fog_begin);
    } else {
        f = 1.0f - expf(-e->fog_density * depth);
    }
    return (uint32_t)((f < 0.0f ? 0.0f : (f > 1.0f ? 1.0f : f)) * 255.0f);
}

/* The PVR looks fog up in a table of 128 entries by the depth of a pixel. With the
 * "far depth" F in the density register, entry i stands for the distance
 * F / (2^(i / 16) * (1 + (i % 16) / 16)): entry 0 is at F, the last one at F / 256. An
 * entry holds the fog at its own distance and at that of the next entry. */
void rr_gfx_set_fog(const RRPkgEnvironmentNode *environment)
{
    const RRPkgEnvironmentNode *e = environment;
    float                       a, b, far;
    uint32_t                    i, fog;

    g_fog = e != NULL && e->fog_enabled;
    if (!g_fog) {
        return;
    }
    if (e->fog_mode == RR_FOG_DEPTH) {
        a = e->fog_begin;
        b = e->fog_end;
        far = b > a ? b : a + 0.001f;
    } else {
        a = e->fog_density;
        b = 0.0f;
        far = g_far;
    }
    if (g_fog_table.color == e->fog_color && g_fog_table.mode == e->fog_mode &&
        g_fog_table.a == a && g_fog_table.b == b && g_fog_table.far == far) {
        return;
    }
    g_fog_table.color = e->fog_color;
    g_fog_table.mode = e->fog_mode;
    g_fog_table.a = a;
    g_fog_table.b = b;
    g_fog_table.far = far;

    pvr_fog_table_color(1.0f, (float)(e->fog_color >> 24) * (1.0f / 255.0f),
                        (float)((e->fog_color >> 16) & 0xFFu) * (1.0f / 255.0f),
                        (float)((e->fog_color >> 8) & 0xFFu) * (1.0f / 255.0f));
    pvr_fog_far_depth(far);
    fog = fog_at(e, far);
    for (i = 0; i < 128; i++) {
        const uint32_t n = i + 1;
        const float    scale = (float)(1u << (n >> 4)) * (1.0f + (float)(n & 15u) * (1.0f / 16.0f));
        const uint32_t next = fog_at(e, far / scale);
        PVR_SET(PVR_FOG_TABLE_BASE + i * 4, (fog << 8) | next);
        fog = next;
    }
}

void rr_gfx_begin_2d(void)
{
    g_fog = false;
}

/* --- drawing ------------------------------------------------------------------------- */

void rr_gfx_draw_mesh(const RRPkgMesh *mesh, const RRMat34 *world, const uint32_t *textures)
{
    const float *m = world->m;
    matrix_t     model = { { m[0], m[1], m[2], 0.0f },
                           { m[3], m[4], m[5], 0.0f },
                           { m[6], m[7], m[8], 0.0f },
                           { m[9], m[10], m[11], 1.0f } };
    /* A mirroring transform turns the faces round on the screen. */
    const float  det = m[0] * (m[4] * m[8] - m[5] * m[7]) - m[1] * (m[3] * m[8] - m[5] * m[6]) +
                       m[2] * (m[3] * m[7] - m[4] * m[6]);
    /* Front faces are counter-clockwise, as in Godot. What the PVR calls clockwise is the
     * other way round on the screen, whose Y axis points down: this setting removes the
     * back faces. */
    const pvr_cull_mode_t cull = det < 0.0f ? PVR_CULLING_CCW : PVR_CULLING_CW;
    const float  sx = m[0] * m[0] + m[1] * m[1] + m[2] * m[2];
    const float  sy = m[3] * m[3] + m[4] * m[4] + m[5] * m[5];
    const float  sz = m[6] * m[6] + m[7] * m[7] + m[8] * m[8];
    const float  s2 = sx > sy ? (sx > sz ? sx : sz) : (sy > sz ? sy : sz);
    ClipVertex   center;
    bool         clip;
    uint32_t     s, i, k;

    mat_load(&g_view);
    mat_apply(&model);
    g_view_loaded = false;

    /* Only a mesh whose bounding sphere reaches in front of the near plane needs the
     * slower path that looks at every triangle. */
    to_clip(&center, mesh->center);
    clip = center.w - mesh->radius * sqrtf(s2) < g_near;

    for (s = 0; s < mesh->surface_count; s++) {
        const RRPkgSurface *surf = &mesh->surfaces[s];
        const uint16_t     *strips = rr_surface_strips(mesh, surf);
        const RRVertexDC   *v = (const RRVertexDC *)rr_surface_verts(mesh, surf);
        List               *list = &g_lists[surf->blend == RR_BLEND_TRANSLUCENT
                                                ? LIST_TR
                                                : (surf->blend == RR_BLEND_PUNCH_THROUGH ? LIST_PT
                                                                                         : LIST_OP)];
        pvr_vertex_t       *out;

        if (!begin_poly(list, surf->texture != RR_NONE32 ? textures[surf->texture] : RR_NONE32,
                        cull, true, true, g_fog)) {
            continue;
        }
        out = list->tail;
        for (i = 0; i < surf->strip_count; i++) {
            const uint32_t count = strips[i];

            if (count >= 3) {
                if (list->left < count * (clip ? 4u : 1u)) {
                    list_full(list);
                    break;
                }
                if (clip) {
                    pvr_vertex_t *end = put_strip_clipped(out, v, count);
                    list->left -= (uint32_t)(end - out);
                    out = end;
                } else {
                    for (k = 0; k < count; k++) {
                        float       x = v[k].pos[0], y = v[k].pos[1], z = v[k].pos[2], w;
                        float       inv_w;

                        mat_trans_single3_nodivw(x, y, z, w);
                        (void)z;
                        inv_w = g_ortho_base > 0.0f ? 1.0f : 1.0f / w;
                        dcache_alloc_line_with_value(out, PVR_CMD_VERTEX);
                        out->x = x * inv_w;
                        out->y = y * inv_w;
                        out->z = g_ortho_base > 0.0f ? g_ortho_base / (g_ortho_base + w) : inv_w;
                        out->u = v[k].uv[0];
                        out->v = v[k].uv[1];
                        out->argb = v[k].argb;
                        out->oargb = 0;
                        out++;
                    }
                    out[-1].flags = PVR_CMD_VERTEX_EOL;
                    list->left -= count;
                }
            }
            v += count;
        }
        list->tail = out;
    }
}

void rr_gfx_mesh_changed(const RRPkgMesh *mesh, uint32_t size)
{
    (void)mesh;
    (void)size;
}

/* A quad in world space: corners and `uv` as in rr_gfx_draw_quad_3d. */
static void draw_world_quad(List *list, const float *xyz, const float *uv, uint32_t argb,
                            float depth_scale)
{
    ClipVertex    c[4];
    pvr_vertex_t *end;
    int           i;

    if (list->left < 5) {
        list_full(list);
        return;
    }
    if (!g_view_loaded) {
        mat_load(&g_view);
        g_view_loaded = true;
    }
    for (i = 0; i < 4; i++) {
        to_clip(&c[i], &xyz[i * 3]);
        c[i].u = uv[i * 2];
        c[i].v = uv[i * 2 + 1];
        c[i].argb = argb;
    }
    end = put_polygon(list->tail, c, 4, depth_scale);
    list->left -= (uint32_t)(end - list->tail);
    list->tail = end;
}

void rr_gfx_draw_sky(uint32_t texture, const float *xyz, const float *uv)
{
    List *list = &g_lists[LIST_OP];

    /* Neither tested against the depth nor written to it, so the meshes that follow in
     * the list cover the sky wherever they are. The depth is scaled to lie behind the far
     * plane as well, which keeps the texture in perspective. */
    if (begin_poly(list, texture, PVR_CULLING_NONE, false, false, false)) {
        draw_world_quad(list, xyz, uv, 0xFFFFFFFFu, g_near / (g_far * 2.0f));
    }
}

void rr_gfx_draw_quad_3d(uint32_t texture, const float *xyz, const float *uv, uint32_t rgba)
{
    List *list = &g_lists[LIST_TR];

    if (begin_poly(list, texture, PVR_CULLING_NONE, true, false, g_fog)) {
        draw_world_quad(list, xyz, uv, (rgba >> 8) | (rgba << 24), 1.0f);
    }
}

void rr_gfx_draw_quad(uint32_t texture, const float *xy, const float *uv, uint32_t rgba)
{
    /* As a strip: top left, top right, bottom left, bottom right. */
    static const int order[4] = { 0, 1, 3, 2 };
    List            *list = &g_lists[LIST_TR];
    const uint32_t   argb = (rgba >> 8) | (rgba << 24);
    pvr_vertex_t    *out;
    int              i;

    /* The translucent list is drawn after the 3D scene and in call order; without a
     * depth test every quad lies over what was drawn before it. */
    if (!begin_poly(list, texture, PVR_CULLING_NONE, false, false, false)) {
        return;
    }
    if (list->left < 4) {
        list_full(list);
        return;
    }
    out = list->tail;
    for (i = 0; i < 4; i++) {
        const int c = order[i];
        dcache_alloc_line_with_value(out, i == 3 ? PVR_CMD_VERTEX_EOL : PVR_CMD_VERTEX);
        out->x = xy[c * 2];
        out->y = xy[c * 2 + 1];
        out->z = 1.0f;
        out->u = uv[c * 2];
        out->v = uv[c * 2 + 1];
        out->argb = argb;
        out->oargb = 0;
        out++;
    }
    list->tail = out;
    list->left -= 4;
}

/* --- textures ------------------------------------------------------------------------ */

uint32_t rr_gfx_texture_load(const RRPkgTexture *texture)
{
    const uint32_t size = (uint32_t)texture->width * texture->height * 2u;
    int            format;
    uint32_t       i;

    switch (texture->format) {
    case RR_TEX_RGB565:   format = PVR_TXRFMT_RGB565; break;
    case RR_TEX_ARGB1555: format = PVR_TXRFMT_ARGB1555; break;
    case RR_TEX_ARGB4444: format = PVR_TXRFMT_ARGB4444; break;
    default:              return RR_NONE32;
    }
    for (i = 0; i < MAX_TEXTURES; i++) {
        Texture *t = &g_textures[i];
        if (t->pixels == NULL) {
            t->pixels = pvr_mem_malloc(size);
            if (t->pixels == NULL) {
                printf("retro: no video memory for a texture of %u x %u\n",
                       (unsigned)texture->width, (unsigned)texture->height);
                return RR_NONE32;
            }
            /* The baked pixels are rows from top to bottom, not twiddled, and go over in
             * one block. */
            pvr_txr_load(texture + 1, t->pixels, size);
            t->width = texture->width;
            t->height = texture->height;
            t->format = format | PVR_TXRFMT_NONTWIDDLED;
            t->filter = texture->filter == RR_FILTER_NEAREST ? PVR_FILTER_NEAREST : PVR_FILTER_BILINEAR;
            return i;
        }
    }
    return RR_NONE32;
}

void rr_gfx_texture_unload(uint32_t handle)
{
    if (handle < MAX_TEXTURES && g_textures[handle].pixels != NULL) {
        /* The frame that is on its way to the screen may still use the texture. */
        pvr_wait_render_done();
        pvr_mem_free(g_textures[handle].pixels);
        g_textures[handle].pixels = NULL;
    }
}
