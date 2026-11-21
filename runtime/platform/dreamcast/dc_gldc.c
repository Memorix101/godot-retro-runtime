/* Dreamcast graphics on GLdc, the older of the two backends (make GFX=gldc). GLdc does
 * the transform and the near-plane clipping. It is kept for comparison with dc_pvr.c. */
#include <math.h>
#include <string.h>

#include <kos.h>

#include <GL/gl.h>
#include <GL/glkos.h>
#include <GL/glu.h>

#include "../../core/rr_platform.h"
#include "dc_gfx.h"

#define MAX_TEXTURES 256

typedef struct {
    float    pos[3];
    float    uv[2];
    uint32_t argb;
} QuadVertex;

static GLuint g_textures[MAX_TEXTURES];
static float  g_quad_z;
static GLuint g_white_texture; /* for plain-colour quads, see rr_gfx_draw_quad */

bool dc_gfx_init(void)
{
    GLdcConfig config;

    glKosInitConfig(&config);
    /* With twiddling on, GLdc 1.1 reorders the texels by writing them one by one into
     * video memory. On a real console such 16-bit writes lose bits (wrong colours and
     * stripes in every texture); emulators do not show it. Without twiddling the baked
     * pixels are copied over in one block. */
    config.texture_twiddle = GL_FALSE;
    glKosInitEx(&config);
    glFrontFace(GL_CCW);
    glShadeModel(GL_SMOOTH);
    glDisable(GL_LIGHTING);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    {
        /* 8x8 opaque white, ARGB4444. */
        static uint16_t white[64];
        memset(white, 0xFF, sizeof(white));
        glGenTextures(1, &g_white_texture);
        glBindTexture(GL_TEXTURE_2D, g_white_texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 8, 8, 0, GL_BGRA, GL_UNSIGNED_SHORT_4_4_4_4_REV, white);
    }
    return true;
}

void dc_gfx_shutdown(void)
{
    glKosShutdown();
}


void rr_gfx_set_fog(const RRPkgEnvironmentNode *environment)
{
    GLfloat color[4];

    if (environment == NULL || !environment->fog_enabled) {
        glDisable(GL_FOG);
        return;
    }
    color[0] = (float)(environment->fog_color >> 24) * (1.0f / 255.0f);
    color[1] = (float)((environment->fog_color >> 16) & 0xFFu) * (1.0f / 255.0f);
    color[2] = (float)((environment->fog_color >> 8) & 0xFFu) * (1.0f / 255.0f);
    color[3] = 1.0f;
    glFogfv(GL_FOG_COLOR, color);
    if (environment->fog_mode == RR_FOG_DEPTH) {
        glFogi(GL_FOG_MODE, GL_LINEAR);
        glFogf(GL_FOG_START, environment->fog_begin);
        glFogf(GL_FOG_END, environment->fog_end);
    } else {
        glFogi(GL_FOG_MODE, GL_EXP);
        glFogf(GL_FOG_DENSITY, environment->fog_density);
    }
    glEnable(GL_FOG);
}

void rr_gfx_begin_frame(uint32_t clear_rgba)
{
    glClearColor((float)(clear_rgba >> 24) * (1.0f / 255.0f),
                 (float)((clear_rgba >> 16) & 0xFFu) * (1.0f / 255.0f),
                 (float)((clear_rgba >> 8) & 0xFFu) * (1.0f / 255.0f), 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    /* Texturing stays on for the whole frame; surfaces and quads without a texture use
     * the white one (see rr_gfx_draw_quad for the reason). */
    glEnable(GL_TEXTURE_2D);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
}

void rr_gfx_set_camera(const RRMat34 *world, float fov_y_degrees, float z_near, float z_far,
                       float ortho_size)
{
    const float *m = world->m;
    const float  half_height = z_near * tanf(fov_y_degrees * (F_PI / 360.0f));
    const float  half_width = half_height * ((float)RR_SCREEN_WIDTH / (float)RR_SCREEN_HEIGHT);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    if (ortho_size > 0.0f) {
        const float half = ortho_size * 0.5f;
        const float aspect = (float)RR_SCREEN_WIDTH / (float)RR_SCREEN_HEIGHT;
        glOrtho(-half * aspect, half * aspect, -half, half, z_near, z_far);
    } else {
        /* The GLdc port has no gluPerspective, so the frustum is set up by hand. */
        glFrustum(-half_width, half_width, -half_height, half_height, z_near, z_far);
    }
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    /* Eye at the origin of the camera node, looking along its -Z, up along its Y. */
    gluLookAt(m[9], m[10], m[11], m[9] - m[6], m[10] - m[7], m[11] - m[8], m[3], m[4], m[5]);
}

void rr_gfx_draw_mesh(const RRPkgMesh *mesh, const RRMat34 *world, const uint32_t *textures)
{
    const float *m = world->m;
    const float  matrix[16] = { m[0], m[1], m[2],  0.0f, m[3], m[4],  m[5],  0.0f,
                                m[6], m[7], m[8],  0.0f, m[9], m[10], m[11], 1.0f };
    uint32_t     s, i;

    glPushMatrix();
    glMultMatrixf(matrix);
    for (s = 0; s < mesh->surface_count; s++) {
        const RRPkgSurface *surf = &mesh->surfaces[s];
        const uint16_t     *strips = rr_surface_strips(mesh, surf);
        const RRVertexDC   *v = (const RRVertexDC *)rr_surface_verts(mesh, surf);
        const uint32_t      texture = surf->texture != RR_NONE32 ? textures[surf->texture] : RR_NONE32;
        GLint               first = 0;

        glBindTexture(GL_TEXTURE_2D, texture < MAX_TEXTURES && g_textures[texture] != 0
                                         ? g_textures[texture]
                                         : g_white_texture);

        /* GLdc sorts polygons into the PVR lists by this state: blending selects the
         * translucent list, the alpha test the punch-through list. */
        if (surf->blend == RR_BLEND_TRANSLUCENT) {
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        } else if (surf->blend == RR_BLEND_PUNCH_THROUGH) {
            glEnable(GL_ALPHA_TEST);
        }

        glVertexPointer(3, GL_FLOAT, sizeof(RRVertexDC), v->pos);
        glTexCoordPointer(2, GL_FLOAT, sizeof(RRVertexDC), v->uv);
        glColorPointer(GL_BGRA, GL_UNSIGNED_BYTE, sizeof(RRVertexDC), &v->argb);
        for (i = 0; i < surf->strip_count; i++) {
            glDrawArrays(GL_TRIANGLE_STRIP, first, strips[i]);
            first += strips[i];
        }

        glDisable(GL_BLEND);
        glDisable(GL_ALPHA_TEST);
    }
    glPopMatrix();
}

void rr_gfx_mesh_changed(const RRPkgMesh *mesh, uint32_t size)
{
    (void)mesh;
    (void)size;
}

void rr_gfx_draw_sky(uint32_t texture, const float *xyz, const float *uv)
{
    static const int  order[4] = { 0, 1, 3, 2 }; /* as a strip, see rr_gfx_draw_quad_3d */
    static QuadVertex v[4];
    int               i;

    for (i = 0; i < 4; i++) {
        const int c = order[i];
        v[i].pos[0] = xyz[c * 3];
        v[i].pos[1] = xyz[c * 3 + 1];
        v[i].pos[2] = xyz[c * 3 + 2];
        v[i].uv[0]  = uv[c * 2];
        v[i].uv[1]  = uv[c * 2 + 1];
        v[i].argb   = 0xFFFFFFFFu;
    }
    /* Opaque, and neither tested against the depth nor written to it: the meshes that
     * follow in the same list cover it wherever they are. */
    glDisable(GL_BLEND);
    glDisable(GL_ALPHA_TEST);
    glDisable(GL_FOG);
    glDisable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glBindTexture(GL_TEXTURE_2D, texture < MAX_TEXTURES && g_textures[texture] != 0 ? g_textures[texture]
                                                                                  : g_white_texture);
    glTexCoordPointer(2, GL_FLOAT, sizeof(QuadVertex), v[0].uv);
    glVertexPointer(3, GL_FLOAT, sizeof(QuadVertex), v[0].pos);
    glColorPointer(GL_BGRA, GL_UNSIGNED_BYTE, sizeof(QuadVertex), &v[0].argb);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
}

void rr_gfx_draw_quad_3d(uint32_t texture, const float *xyz, const float *uv, uint32_t rgba)
{
    /* As a strip: top left, top right, bottom left, bottom right. The model-view matrix
     * is the camera's at this point, so world coordinates go in as they are. */
    static const int  order[4] = { 0, 1, 3, 2 };
    static QuadVertex v[4];
    const uint32_t    argb = (rgba >> 8) | (rgba << 24);
    const bool        textured = texture < MAX_TEXTURES && g_textures[texture] != 0;
    int               i;

    for (i = 0; i < 4; i++) {
        const int c = order[i];
        v[i].pos[0] = xyz[c * 3];
        v[i].pos[1] = xyz[c * 3 + 1];
        v[i].pos[2] = xyz[c * 3 + 2];
        v[i].uv[0]  = textured ? uv[c * 2] : 0.5f;
        v[i].uv[1]  = textured ? uv[c * 2 + 1] : 0.5f;
        v[i].argb   = argb;
    }
    /* Blending puts the quad into the PVR's translucent list. */
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_CULL_FACE);
    glDepthMask(GL_FALSE);
    glBindTexture(GL_TEXTURE_2D, textured ? g_textures[texture] : g_white_texture);
    glTexCoordPointer(2, GL_FLOAT, sizeof(QuadVertex), v[0].uv);
    glVertexPointer(3, GL_FLOAT, sizeof(QuadVertex), v[0].pos);
    glColorPointer(GL_BGRA, GL_UNSIGNED_BYTE, sizeof(QuadVertex), &v[0].argb);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glDepthMask(GL_TRUE);
}

void rr_gfx_begin_2d(void)
{
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glDisable(GL_FOG);
    glOrtho(0.0f, (float)RR_SCREEN_WIDTH, (float)RR_SCREEN_HEIGHT, 0.0f, -1.0f, 1.0f);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    /* All quads go to the translucent list, which the PVR draws after the opaque 3D
     * scene. Each quad is a little closer than the one before, so call order holds. */
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    g_quad_z = 0.0f;
}

uint32_t rr_gfx_texture_load(const RRPkgTexture *texture)
{
    GLenum   format = GL_BGRA, type = GL_UNSIGNED_SHORT_4_4_4_4_REV;
    GLint    internal = GL_RGBA;
    uint32_t i;

    if (texture->format == RR_TEX_RGB565) {
        format = GL_RGB;
        type = GL_UNSIGNED_SHORT_5_6_5;
        internal = GL_RGB;
    } else if (texture->format == RR_TEX_ARGB1555) {
        type = GL_UNSIGNED_SHORT_1_5_5_5_REV;
    } else if (texture->format != RR_TEX_ARGB4444) {
        return RR_NONE32;
    }
    for (i = 0; i < MAX_TEXTURES; i++) {
        if (g_textures[i] == 0) {
            glGenTextures(1, &g_textures[i]);
            glBindTexture(GL_TEXTURE_2D, g_textures[i]);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                            texture->filter == RR_FILTER_NEAREST ? GL_NEAREST : GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,
                            texture->filter == RR_FILTER_NEAREST ? GL_NEAREST : GL_LINEAR);
            glTexImage2D(GL_TEXTURE_2D, 0, internal, texture->width, texture->height, 0, format,
                         type, texture + 1);
            return i;
        }
    }
    return RR_NONE32;
}

void rr_gfx_texture_unload(uint32_t handle)
{
    if (handle < MAX_TEXTURES && g_textures[handle] != 0) {
        glDeleteTextures(1, &g_textures[handle]);
        g_textures[handle] = 0;
    }
}

void rr_gfx_draw_quad(uint32_t texture, const float *xy, const float *uv, uint32_t rgba)
{
    /* As a strip: top left, top right, bottom left, bottom right. GLdc copies the
     * vertices during glDrawArrays, so one buffer serves all quads. */
    static const int order[4] = { 0, 1, 3, 2 };
    static QuadVertex v[4];
    const uint32_t    argb = (rgba >> 8) | (rgba << 24);
    const bool        textured = texture < MAX_TEXTURES && g_textures[texture] != 0;
    int               i;

    g_quad_z += 0.0004f;
    for (i = 0; i < 4; i++) {
        const int c = order[i];
        v[i].pos[0] = xy[c * 2];
        v[i].pos[1] = xy[c * 2 + 1];
        v[i].pos[2] = g_quad_z;
        v[i].uv[0]  = textured ? uv[c * 2] : 0.5f;
        v[i].uv[1]  = textured ? uv[c * 2 + 1] : 0.5f;
        v[i].argb   = argb;
    }
    /* Plain-colour quads use a white texture instead of switching texturing off: with
     * GLdc 1.1, toggling GL_TEXTURE_2D between quads of the translucent list left
     * some of them with the previous texture and wrong colours. */
    glBindTexture(GL_TEXTURE_2D, textured ? g_textures[texture] : g_white_texture);
    glTexCoordPointer(2, GL_FLOAT, sizeof(QuadVertex), v[0].uv);
    glVertexPointer(3, GL_FLOAT, sizeof(QuadVertex), v[0].pos);
    glColorPointer(GL_BGRA, GL_UNSIGNED_BYTE, sizeof(QuadVertex), &v[0].argb);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

void rr_gfx_end_frame(void)
{
    glKosSwapBuffers();
}

