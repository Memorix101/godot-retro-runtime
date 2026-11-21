/* Wii and GameCube graphics on GX, through libogc.
 *
 * The graphics chip transforms and clips by itself. A baked vertex is the three values it
 * takes for one (position, colour, texture coordinates), in its byte order, so drawing a
 * strip is handing its vertices over one after the other. Textures are in the chip's own
 * formats; they only have to be copied to memory that is aligned the way it wants.
 *
 * The picture is 640 x 480, drawn into the chip's own buffer and copied out to the frame
 * that goes to the television when the frame ends. On a television with 576 lines that
 * copy stretches it. A Wii that is set to a wide television stretches every picture to
 * 16:9; there the game is drawn into the middle three quarters of the buffer, which gives
 * it its own shape back, with black bars left and right. */
#include <malloc.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include <gccore.h>

#include "../../core/rr_platform.h"
#include "wii_gfx.h"

#define MAX_TEXTURES    256
#define MAX_TRANSLUCENT 256
#define FIFO_SIZE       (256 * 1024)

enum { CULL_NONE, CULL_BACK, CULL_FRONT };

typedef struct {
    void    *pixels; /* NULL: free slot */
    GXTexObj object;
} Texture;

typedef struct {
    const RRPkgMesh    *mesh;
    const RRPkgSurface *surface;
    const uint32_t     *textures;
    RRMat34             world;
    float               distance; /* squared, from the camera to the middle of the mesh */
} Translucent;

static GXRModeObj *g_mode;
static void       *g_frames[2];
static int         g_frame;
static void       *g_fifo;
static u32         g_view_x, g_view_width; /* the part of the buffer the game is drawn into */

static Texture g_textures[MAX_TEXTURES];

static uint32_t g_state;         /* see set_state; 0xFFFFFFFF: not known */
static uint32_t g_state_texture; /* handle + 1, 0 for none */
static bool     g_in_2d;

static Mtx   g_view;
static float g_near, g_far;
static bool  g_ortho;
static float g_eye[3];
static bool  g_fog;
static struct {
    uint8_t type;
    float   start, end;
    GXColor color;
} g_fog_setting;

static Translucent g_translucent[MAX_TRANSLUCENT];
static uint16_t    g_translucent_order[MAX_TRANSLUCENT];
static uint32_t    g_translucent_count;

bool wii_gfx_init(void)
{
    const GXColor black = { 0, 0, 0, 255 };
    f32           scale;
    u32           lines;

    VIDEO_Init();
    g_mode = VIDEO_GetPreferredMode(NULL);
    g_frames[0] = MEM_K0_TO_K1(SYS_AllocateFramebuffer(g_mode));
    g_frames[1] = MEM_K0_TO_K1(SYS_AllocateFramebuffer(g_mode));
    VIDEO_Configure(g_mode);
    VIDEO_ClearFrameBuffer(g_mode, g_frames[0], COLOR_BLACK);
    VIDEO_ClearFrameBuffer(g_mode, g_frames[1], COLOR_BLACK);
    VIDEO_SetNextFramebuffer(g_frames[0]);
    VIDEO_SetBlack(false);
    VIDEO_Flush();
    VIDEO_WaitVSync();
    if ((g_mode->viTVMode & VI_NON_INTERLACE) != 0) {
        VIDEO_WaitVSync();
    }

    g_fifo = memalign(32, FIFO_SIZE);
    if (g_fifo == NULL) {
        rr_set_error("no memory for the graphics queue");
        return false;
    }
    memset(g_fifo, 0, FIFO_SIZE);
    GX_Init(g_fifo, FIFO_SIZE);
    GX_SetCopyClear(black, GX_MAX_Z24);
    g_view_x = 0;
    g_view_width = g_mode->fbWidth;
#ifdef HW_RVL
    if (CONF_GetAspectRatio() == CONF_ASPECT_16_9) {
        g_view_width = (g_mode->fbWidth * 3u / 4u) & ~1u;
        g_view_x = (g_mode->fbWidth - g_view_width) / 2u;
    }
#endif
    GX_SetViewport((f32)g_view_x, 0.0f, (f32)g_view_width, (f32)g_mode->efbHeight, 0.0f, 1.0f);
    GX_SetScissor(g_view_x, 0, g_view_width, g_mode->efbHeight);
    scale = GX_GetYScaleFactor(g_mode->efbHeight, g_mode->xfbHeight);
    lines = GX_SetDispCopyYScale(scale);
    GX_SetDispCopySrc(0, 0, g_mode->fbWidth, g_mode->efbHeight);
    GX_SetDispCopyDst(g_mode->fbWidth, (u16)lines);
    GX_SetCopyFilter(g_mode->aa, g_mode->sample_pattern, GX_TRUE, g_mode->vfilter);
    GX_SetFieldMode(g_mode->field_rendering,
                    g_mode->viHeight == 2 * g_mode->xfbHeight ? GX_ENABLE : GX_DISABLE);
    GX_SetPixelFmt(g_mode->aa ? GX_PF_RGB565_Z16 : GX_PF_RGB8_Z24, GX_ZC_LINEAR);
    GX_SetDispCopyGamma(GX_GM_1_0);
    GX_SetClipMode(GX_CLIP_ENABLE);

    /* A vertex: position, colour, texture coordinates, all handed over as values. */
    GX_ClearVtxDesc();
    GX_SetVtxDesc(GX_VA_POS, GX_DIRECT);
    GX_SetVtxDesc(GX_VA_CLR0, GX_DIRECT);
    GX_SetVtxDesc(GX_VA_TEX0, GX_DIRECT);
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0);

    /* The colour of a pixel is the vertex colour, times the texture if there is one. The
     * lighting is baked into the vertex colours, so the chip's own lights stay off. */
    GX_SetNumChans(1);
    GX_SetChanCtrl(GX_COLOR0A0, GX_DISABLE, GX_SRC_VTX, GX_SRC_VTX, GX_LIGHTNULL, GX_DF_NONE, GX_AF_NONE);
    GX_SetNumTexGens(1);
    GX_SetTexCoordGen(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY);
    GX_SetNumTevStages(1);
    GX_SetCurrentMtx(GX_PNMTX0);
    GX_SetColorUpdate(GX_TRUE);
    GX_SetAlphaUpdate(GX_FALSE);
    GX_InvVtxCache();
    GX_InvalidateTexAll();

    /* The first frame starts on a cleared buffer. */
    GX_SetZMode(GX_TRUE, GX_LEQUAL, GX_TRUE);
    GX_CopyDisp(g_frames[1], GX_TRUE);
    GX_DrawDone();
    g_fog_setting.type = 0xFF;
    return true;
}

void wii_gfx_shutdown(void)
{
    GX_AbortFrame();
    GX_Flush();
    VIDEO_SetBlack(true);
    VIDEO_Flush();
    VIDEO_WaitVSync();
}

/* --- state --------------------------------------------------------------------------- */

static GXColor to_color(uint32_t rgba)
{
    const GXColor c = { (u8)(rgba >> 24), (u8)(rgba >> 16), (u8)(rgba >> 8), (u8)rgba };
    return c;
}

/* Puts the chip into the state for what is drawn next. `texture` is a handle or
 * RR_NONE32; `clamp` keeps the texture from repeating (the sky, whose edges would show
 * the other side of the picture). */
static void set_state(uint32_t texture, uint8_t blend, bool depth_test, bool depth_write,
                      int cull, bool fog, bool clamp)
{
    Texture *t = texture < MAX_TEXTURES && g_textures[texture].pixels != NULL ? &g_textures[texture]
                                                                              : NULL;
    const uint32_t tex = t != NULL ? texture + 1u : 0u;
    const uint32_t state = (uint32_t)blend | (depth_test ? 4u : 0u) | (depth_write ? 8u : 0u) |
                           ((uint32_t)cull << 4) | (fog ? 64u : 0u) | (clamp ? 128u : 0u) |
                           (tex != 0 ? 256u : 0u);
    const uint32_t changed = g_state ^ state;
    const bool     all = g_state == 0xFFFFFFFFu;

    if (changed == 0 && tex == g_state_texture) {
        return;
    }
    if (all || (changed & 3u) != 0) {
        if (blend == RR_BLEND_TRANSLUCENT) {
            GX_SetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);
        } else {
            GX_SetBlendMode(GX_BM_NONE, GX_BL_ONE, GX_BL_ZERO, GX_LO_CLEAR);
        }
        if (blend == RR_BLEND_PUNCH_THROUGH) {
            /* A pixel is drawn from half the alpha on, and only then written to the depth. */
            GX_SetAlphaCompare(GX_GEQUAL, 128, GX_AOP_AND, GX_ALWAYS, 0);
            GX_SetZCompLoc(GX_FALSE);
        } else {
            GX_SetAlphaCompare(GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0);
            GX_SetZCompLoc(GX_TRUE);
        }
    }
    if (all || (changed & 12u) != 0) {
        GX_SetZMode(depth_test ? GX_TRUE : GX_FALSE, GX_LEQUAL, depth_write ? GX_TRUE : GX_FALSE);
    }
    if (all || (changed & 0x30u) != 0) {
        /* Front faces are counter-clockwise, as in Godot; the chip calls the clockwise
         * ones the front. */
        GX_SetCullMode(cull == CULL_NONE ? GX_CULL_NONE : (cull == CULL_BACK ? GX_CULL_FRONT : GX_CULL_BACK));
    }
    if (all || (changed & 64u) != 0) {
        if (fog) {
            GX_SetFog(g_fog_setting.type, g_fog_setting.start, g_fog_setting.end, g_near, g_far,
                      g_fog_setting.color);
        } else {
            GX_SetFog(GX_FOG_NONE, 0.0f, 1.0f, 0.1f, 1.0f, g_fog_setting.color);
        }
    }
    if (all || (changed & 256u) != 0) {
        if (tex != 0) {
            GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);
            GX_SetTevOp(GX_TEVSTAGE0, GX_MODULATE);
        } else {
            GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORDNULL, GX_TEXMAP_NULL, GX_COLOR0A0);
            GX_SetTevOp(GX_TEVSTAGE0, GX_PASSCLR);
        }
    }
    if (tex != 0 && (tex != g_state_texture || all || (changed & 128u) != 0)) {
        GX_InitTexObjWrapMode(&t->object, clamp ? GX_CLAMP : GX_REPEAT, clamp ? GX_CLAMP : GX_REPEAT);
        GX_LoadTexObj(&t->object, GX_TEXMAP0);
    }
    g_state = state;
    g_state_texture = tex;
}

/* The transform of what is drawn next; NULL for things that are in world space. */
static void set_model(const RRMat34 *world)
{
    if (world == NULL) {
        GX_LoadPosMtxImm(g_view, GX_PNMTX0);
    } else {
        const float *m = world->m;
        Mtx          model, both;
        int          r;

        for (r = 0; r < 3; r++) {
            model[r][0] = m[r];
            model[r][1] = m[3 + r];
            model[r][2] = m[6 + r];
            model[r][3] = m[9 + r];
        }
        guMtxConcat(g_view, model, both);
        GX_LoadPosMtxImm(both, GX_PNMTX0);
    }
}

/* --- frame --------------------------------------------------------------------------- */

static void enter_2d(void);

void rr_gfx_begin_frame(uint32_t clear_rgba)
{
    static const float corners[8] = { 0.0f, 0.0f, (float)RR_SCREEN_WIDTH, 0.0f,
                                      (float)RR_SCREEN_WIDTH, (float)RR_SCREEN_HEIGHT,
                                      0.0f, (float)RR_SCREEN_HEIGHT };
    int i;

    g_state = 0xFFFFFFFFu;
    g_state_texture = 0;
    g_translucent_count = 0;
    g_fog = false;
    g_in_2d = false;

    /* The buffer was cleared to black, depth included, when the frame before this one
     * was copied out. The colour of the scene is drawn over the part the game has. */
    enter_2d();
    set_state(RR_NONE32, RR_BLEND_OPAQUE, false, false, CULL_NONE, false, false);
    GX_Begin(GX_QUADS, GX_VTXFMT0, 4);
    for (i = 0; i < 4; i++) {
        GX_Position3f32(corners[i * 2], corners[i * 2 + 1], -0.5f);
        GX_Color1u32(clear_rgba | 0xFFu);
        GX_TexCoord2f32(0.0f, 0.0f);
    }
    GX_End();
}

void rr_gfx_set_camera(const RRMat34 *world, float fov_y_degrees, float z_near, float z_far,
                       float ortho_size)
{
    const float *m = world->m;
    const float  aspect = (float)RR_SCREEN_WIDTH / (float)RR_SCREEN_HEIGHT;
    Mtx44        projection;
    int          i, k;

    /* The rows of the view matrix are the unit axes of the camera; it looks along -Z. */
    for (i = 0; i < 3; i++) {
        const float *a = &m[i * 3];
        const float  length = sqrtf(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
        const float  inv = length > 0.0f ? 1.0f / length : 0.0f;
        for (k = 0; k < 3; k++) {
            g_view[i][k] = a[k] * inv;
        }
        g_view[i][3] = -(g_view[i][0] * m[9] + g_view[i][1] * m[10] + g_view[i][2] * m[11]);
    }
    g_ortho = ortho_size > 0.0f;
    if (g_ortho) {
        const float half = ortho_size * 0.5f;
        guOrtho(projection, half, -half, -half * aspect, half * aspect, z_near, z_far);
        GX_LoadProjectionMtx(projection, GX_ORTHOGRAPHIC);
    } else {
        guPerspective(projection, fov_y_degrees, aspect, z_near, z_far);
        GX_LoadProjectionMtx(projection, GX_PERSPECTIVE);
    }
    g_near = z_near;
    g_far = z_far;
    g_eye[0] = m[9];
    g_eye[1] = m[10];
    g_eye[2] = m[11];
    g_state = 0xFFFFFFFFu; /* the fog depends on the near and far plane */
    g_in_2d = false;
}

/* The chip's exponential fog is 1 - 2^(-8 * depth / end): with its end at 8 ln 2 / density
 * that is Godot's 1 - exp(-density * depth). */
void rr_gfx_set_fog(const RRPkgEnvironmentNode *environment)
{
    const RRPkgEnvironmentNode *e = environment;

    g_fog = e != NULL && e->fog_enabled;
    if (!g_fog) {
        return;
    }
    if (e->fog_mode == RR_FOG_DEPTH) {
        g_fog_setting.type = g_ortho ? GX_FOG_ORTHO_LIN : GX_FOG_PERSP_LIN;
        g_fog_setting.start = e->fog_begin;
        g_fog_setting.end = e->fog_end > e->fog_begin ? e->fog_end : e->fog_begin + 0.001f;
    } else {
        g_fog_setting.type = g_ortho ? GX_FOG_ORTHO_EXP : GX_FOG_PERSP_EXP;
        g_fog_setting.start = 0.0f;
        g_fog_setting.end = e->fog_density > 0.0f ? 5.545177f / e->fog_density : 1.0e9f;
    }
    g_fog_setting.color = to_color(e->fog_color);
    g_state = 0xFFFFFFFFu;
}

/* --- drawing ------------------------------------------------------------------------- */

static inline void put_vertex(const RRVertexWii *v)
{
    GX_Position3f32(v->pos[0], v->pos[1], v->pos[2]);
    GX_Color1u32(v->rgba);
    GX_TexCoord2f32(v->uv[0], v->uv[1]);
}

static void draw_surface(const RRPkgMesh *mesh, const RRPkgSurface *surf, const RRMat34 *world,
                         const uint32_t *textures, bool depth_write)
{
    const float *m = world->m;
    /* A mirroring transform turns the faces round on the screen. */
    const float  det = m[0] * (m[4] * m[8] - m[5] * m[7]) - m[1] * (m[3] * m[8] - m[5] * m[6]) +
                       m[2] * (m[3] * m[7] - m[4] * m[6]);
    const uint16_t    *strips = rr_surface_strips(mesh, surf);
    const RRVertexWii *v = (const RRVertexWii *)rr_surface_verts(mesh, surf);
    uint32_t           i, k;

    set_model(world);
    set_state(surf->texture != RR_NONE32 ? textures[surf->texture] : RR_NONE32, surf->blend, true,
              depth_write, det < 0.0f ? CULL_FRONT : CULL_BACK, g_fog, false);
    for (i = 0; i < surf->strip_count; i++) {
        const uint32_t count = strips[i];

        if (count >= 3) {
            GX_Begin(GX_TRIANGLESTRIP, GX_VTXFMT0, (u16)count);
            for (k = 0; k < count; k++) {
                put_vertex(&v[k]);
            }
            GX_End();
        }
        v += count;
    }
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
    /* The vertices are handed over as values each time they are drawn. */
    (void)mesh;
    (void)size;
}

/* A quad in world space: corners and `uv` as in rr_gfx_draw_quad_3d. The state is set. */
static void draw_world_quad(const float *xyz, const float *uv, uint32_t rgba)
{
    int i;

    set_model(NULL);
    GX_Begin(GX_QUADS, GX_VTXFMT0, 4);
    for (i = 0; i < 4; i++) {
        GX_Position3f32(xyz[i * 3], xyz[i * 3 + 1], xyz[i * 3 + 2]);
        GX_Color1u32(rgba);
        GX_TexCoord2f32(uv[i * 2], uv[i * 2 + 1]);
    }
    GX_End();
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
    draw_world_quad(xyz, uv, rgba);
}

/* Screen pixels, Y down, for what is drawn next. */
static void enter_2d(void)
{
    Mtx44 projection;
    Mtx   identity;

    if (g_in_2d) {
        return;
    }
    guOrtho(projection, 0.0f, (f32)RR_SCREEN_HEIGHT, 0.0f, (f32)RR_SCREEN_WIDTH, 0.0f, 1.0f);
    GX_LoadProjectionMtx(projection, GX_ORTHOGRAPHIC);
    guMtxIdentity(identity);
    GX_LoadPosMtxImm(identity, GX_PNMTX0);
    g_in_2d = true;
}

void rr_gfx_begin_2d(void)
{
    if (g_translucent_count != 0) {
        draw_translucent();
    }
    g_fog = false;
    enter_2d();
}

void rr_gfx_draw_quad(uint32_t texture, const float *xy, const float *uv, uint32_t rgba)
{
    int i;

    if (!g_in_2d) {
        rr_gfx_begin_2d();
    }
    /* Without a depth test every quad lies over what was drawn before it. */
    set_state(texture, RR_BLEND_TRANSLUCENT, false, false, CULL_NONE, false, false);
    GX_Begin(GX_QUADS, GX_VTXFMT0, 4);
    for (i = 0; i < 4; i++) {
        GX_Position3f32(xy[i * 2], xy[i * 2 + 1], -0.5f);
        GX_Color1u32(rgba);
        GX_TexCoord2f32(uv[i * 2], uv[i * 2 + 1]);
    }
    GX_End();
}

void rr_gfx_end_frame(void)
{
    if (g_translucent_count != 0) {
        draw_translucent(); /* a scene without a 2D pass */
    }
    GX_DrawDone();
    /* The copy clears the buffer for the next frame, which it only does to what may be
     * written. */
    g_frame ^= 1;
    GX_SetZMode(GX_TRUE, GX_LEQUAL, GX_TRUE);
    GX_SetColorUpdate(GX_TRUE);
    GX_CopyDisp(g_frames[g_frame], GX_TRUE);
    GX_Flush();
    VIDEO_SetNextFramebuffer(g_frames[g_frame]);
    VIDEO_Flush();
    VIDEO_WaitVSync();
    wii_frame_done();
}

/* --- textures ------------------------------------------------------------------------ */

uint32_t rr_gfx_texture_load(const RRPkgTexture *texture)
{
    const uint32_t size = (uint32_t)texture->width * texture->height * 2u;
    u8             format;
    uint32_t       i;

    switch (texture->format) {
    case RR_TEX_GX_RGB565: format = GX_TF_RGB565; break;
    case RR_TEX_GX_RGB5A3: format = GX_TF_RGB5A3; break;
    case RR_TEX_GX_IA8:    format = GX_TF_IA8; break;
    default:               return RR_NONE32;
    }
    for (i = 0; i < MAX_TEXTURES; i++) {
        Texture *t = &g_textures[i];
        if (t->pixels == NULL) {
            const u8 filter = texture->filter == RR_FILTER_NEAREST ? GX_NEAR : GX_LINEAR;

            /* The chip wants its textures at addresses that are multiples of 32, which
             * the package does not give. */
            t->pixels = memalign(32, (size + 31u) & ~31u);
            if (t->pixels == NULL) {
                printf("retro: no memory for a texture of %u x %u\n", (unsigned)texture->width,
                       (unsigned)texture->height);
                return RR_NONE32;
            }
            memcpy(t->pixels, texture + 1, size);
            DCFlushRange(t->pixels, (size + 31u) & ~31u);
            GX_InitTexObj(&t->object, t->pixels, texture->width, texture->height, format, GX_REPEAT,
                          GX_REPEAT, GX_FALSE);
            GX_InitTexObjFilterMode(&t->object, filter, filter);
            GX_InvalidateTexAll();
            return i;
        }
    }
    printf("retro: more than %d textures, one is missing\n", MAX_TEXTURES);
    return RR_NONE32;
}

void rr_gfx_texture_unload(uint32_t handle)
{
    if (handle < MAX_TEXTURES && g_textures[handle].pixels != NULL) {
        /* What is still on its way to the screen may use the texture. */
        GX_DrawDone();
        free(g_textures[handle].pixels);
        g_textures[handle].pixels = NULL;
        g_state = 0xFFFFFFFFu;
    }
}
