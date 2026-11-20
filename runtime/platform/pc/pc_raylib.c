/* PC platform layer on top of raylib. Everything is drawn through rlgl's immediate
 * mode, which is plenty for a development target. */
#include <stdlib.h>

#include <raylib.h>
#include <raymath.h>
#include <rlgl.h>

#include "../../core/rr_platform.h"

#define MAX_TEXTURES 256
#define MAX_SOUNDS   64

static bool      g_in_3d;
static bool      g_in_particles;
static float     g_eye[3];             /* where the camera is */
static uint32_t  g_translucent_count;  /* translucent surfaces held back this frame */
static Texture2D g_textures[MAX_TEXTURES];
static Sound     g_sounds[MAX_SOUNDS];
static Music     g_music;
static bool      g_music_on;
static bool      g_sound_loops[MAX_SOUNDS];

uint8_t rr_plat_id(void)
{
    return RR_PLATFORM_PC;
}

bool rr_plat_init(const char *title)
{
    SetTraceLogLevel(LOG_WARNING);
    InitWindow(RR_SCREEN_WIDTH, RR_SCREEN_HEIGHT, title);
    if (!IsWindowReady()) {
        rr_set_error("cannot open a window");
        return false;
    }
    SetExitKey(KEY_NULL); /* Escape belongs to the game's input map */
    SetTargetFPS(60);
    InitAudioDevice(); /* without a device the sounds stay silent, which is fine */
    if (getenv("RR_MUTE") != NULL) {
        SetMasterVolume(0.0f); /* for automated runs */
    }
    return true;
}

void rr_plat_shutdown(void)
{
    if (IsAudioDeviceReady()) {
        CloseAudioDevice();
    }
    CloseWindow();
}

bool rr_plat_should_quit(void)
{
    return WindowShouldClose();
}

uint32_t rr_plat_time_us(void)
{
    return (uint32_t)(GetTime() * 1000000.0);
}

float rr_plat_frame_time(void)
{
    return GetFrameTime();
}

bool rr_plat_screenshot(const char *path)
{
    Image image;
    bool  ok;

    rlDrawRenderBatchActive();
    image = LoadImageFromScreen();
    ok = ExportImage(image, path);
    UnloadImage(image);
    return ok;
}

/* Binding codes are raylib's own key, button and axis numbers (see pc_baker.gd). */
float rr_plat_binding_value(const RRPkgBinding *binding)
{
    float v;

    switch (binding->kind) {
    case RR_BIND_KEY:
        return IsKeyDown(binding->code) ? 1.0f : 0.0f;
    case RR_BIND_PAD_BUTTON:
        return IsGamepadAvailable(0) && IsGamepadButtonDown(0, binding->code) ? 1.0f : 0.0f;
    default:
        if (!IsGamepadAvailable(0)) {
            return 0.0f;
        }
        v = GetGamepadAxisMovement(0, binding->code);
        if (binding->code == GAMEPAD_AXIS_LEFT_TRIGGER || binding->code == GAMEPAD_AXIS_RIGHT_TRIGGER) {
            v = (v + 1.0f) * 0.5f; /* raylib reports triggers as -1..1 */
        } else {
            v *= (float)binding->sign;
        }
        return v > 0.0f ? v : 0.0f;
    }
}

/* --- graphics ------------------------------------------------------------------------ */

void rr_gfx_begin_frame(uint32_t clear_rgba)
{
    BeginDrawing();
    ClearBackground((Color){ (unsigned char)(clear_rgba >> 24), (unsigned char)(clear_rgba >> 16),
                             (unsigned char)(clear_rgba >> 8), 255 });
    rlEnableBackfaceCulling();
}

/* Fog needs a shader with this OpenGL version. It is raylib's default shader with the
 * fog mixed in by the distance from the camera, which is the w of the clip position. */
static const char *const g_fog_vs =
    "#version 330\n"
    "in vec3 vertexPosition; in vec2 vertexTexCoord; in vec4 vertexColor;\n"
    "uniform mat4 mvp;\n"
    "out vec2 fragTexCoord; out vec4 fragColor; out float fragDepth;\n"
    "void main() {\n"
    "    fragTexCoord = vertexTexCoord; fragColor = vertexColor;\n"
    "    gl_Position = mvp * vec4(vertexPosition, 1.0);\n"
    "    fragDepth = gl_Position.w;\n"
    "}\n";
static const char *const g_fog_fs =
    "#version 330\n"
    "in vec2 fragTexCoord; in vec4 fragColor; in float fragDepth;\n"
    "uniform sampler2D texture0; uniform vec4 colDiffuse;\n"
    "uniform vec3 fogColor; uniform vec3 fogParams;\n" /* mode, then density or begin, end */
    "out vec4 finalColor;\n"
    "void main() {\n"
    "    vec4 c = texture(texture0, fragTexCoord) * colDiffuse * fragColor;\n"
    "    float f = fogParams.x < 0.5 ? 1.0 - exp(-fogParams.y * fragDepth)\n"
    "        : clamp((fragDepth - fogParams.y) / max(fogParams.z - fogParams.y, 0.0001), 0.0, 1.0);\n"
    "    finalColor = vec4(mix(c.rgb, fogColor, f), c.a);\n"
    "}\n";
static Shader g_fog_shader;
static bool   g_fog_on;

static void fog_off(void)
{
    if (g_fog_on) {
        EndShaderMode();
        g_fog_on = false;
    }
}

void rr_gfx_set_fog(const RRPkgEnvironmentNode *environment)
{
    float color[3], params[3];

    fog_off();
    if (environment == NULL || !environment->fog_enabled) {
        return;
    }
    if (g_fog_shader.id == 0) {
        g_fog_shader = LoadShaderFromMemory(g_fog_vs, g_fog_fs);
    }
    color[0]  = (float)(environment->fog_color >> 24) * (1.0f / 255.0f);
    color[1]  = (float)((environment->fog_color >> 16) & 0xFFu) * (1.0f / 255.0f);
    color[2]  = (float)((environment->fog_color >> 8) & 0xFFu) * (1.0f / 255.0f);
    params[0] = (float)environment->fog_mode;
    params[1] = environment->fog_mode == RR_FOG_DEPTH ? environment->fog_begin : environment->fog_density;
    params[2] = environment->fog_end;
    SetShaderValue(g_fog_shader, GetShaderLocation(g_fog_shader, "fogColor"), color, SHADER_UNIFORM_VEC3);
    SetShaderValue(g_fog_shader, GetShaderLocation(g_fog_shader, "fogParams"), params, SHADER_UNIFORM_VEC3);
    BeginShaderMode(g_fog_shader);
    g_fog_on = true;
}

void rr_gfx_set_camera(const RRMat34 *world, float fov_y_degrees, float z_near, float z_far,
                       float ortho_size)
{
    const float  *m = world->m;
    const Vector3 eye = { m[9], m[10], m[11] };
    const Vector3 target = { m[9] - m[6], m[10] - m[7], m[11] - m[8] }; /* looks along -Z */
    const Vector3 up = { m[3], m[4], m[5] };
    const Matrix  view = MatrixLookAt(eye, target, up);
    const double  aspect = (double)RR_SCREEN_WIDTH / (double)RR_SCREEN_HEIGHT;
    const double  half = (double)ortho_size * 0.5;
    const Matrix  projection = ortho_size > 0.0f
        ? MatrixOrtho(-half * aspect, half * aspect, -half, half, (double)z_near, (double)z_far)
        : MatrixPerspective((double)(fov_y_degrees * DEG2RAD), aspect, (double)z_near, (double)z_far);

    /* Same steps as raylib's BeginMode3D, but with our own clip planes. */
    rlDrawRenderBatchActive();
    rlMatrixMode(RL_PROJECTION);
    rlPushMatrix();
    rlLoadIdentity();
    rlMultMatrixf(MatrixToFloat(projection));
    rlMatrixMode(RL_MODELVIEW);
    rlLoadIdentity();
    rlMultMatrixf(MatrixToFloat(view));
    rlEnableDepthTest();
    g_eye[0] = m[9];
    g_eye[1] = m[10];
    g_eye[2] = m[11];
    g_translucent_count = 0;
    g_in_3d = true;
}

static void emit_vertex(const RRVertexPC *v)
{
    rlColor4ub(v->rgba[0], v->rgba[1], v->rgba[2], v->rgba[3]);
    rlTexCoord2f(v->uv[0], v->uv[1]);
    rlVertex3f(v->pos[0], v->pos[1], v->pos[2]);
}

/* Translucent surfaces are not drawn where they come in the tree: as in Godot they wait
 * until everything solid is there, and are then drawn from the far ones to the near ones
 * without writing the depth, so that what is behind or inside one stays visible. The
 * consoles' hardware sorts its translucent list by itself. */
#define MAX_TRANSLUCENT 256

typedef struct {
    const RRPkgMesh    *mesh;
    const RRPkgSurface *surface;
    const uint32_t     *textures;
    RRMat34             world;
    float               distance; /* squared, from the camera to the middle of the mesh */
} Translucent;

static Translucent g_translucent[MAX_TRANSLUCENT];
static uint16_t    g_translucent_order[MAX_TRANSLUCENT];

static void draw_surface(const RRPkgMesh *mesh, const RRPkgSurface *surf, const RRMat34 *world,
                         const uint32_t *textures)
{
    const float      *m = world->m;
    const float       matrix[16] = { m[0], m[1], m[2],  0.0f, m[3], m[4],  m[5],  0.0f,
                                     m[6], m[7], m[8],  0.0f, m[9], m[10], m[11], 1.0f };
    const uint16_t   *strips = rr_surface_strips(mesh, surf);
    const RRVertexPC *v = (const RRVertexPC *)rr_surface_verts(mesh, surf);
    const uint32_t    texture = surf->texture != RR_NONE32 ? textures[surf->texture] : RR_NONE32;
    uint32_t          i, k;

    rlPushMatrix();
    rlMultMatrixf(matrix);
    /* raylib blends by alpha already, so all blend modes draw the same here. The texture
     * colour is multiplied with the vertex colour. */
    rlSetTexture(texture < MAX_TEXTURES && g_textures[texture].id != 0 ? g_textures[texture].id
                                                                      : rlGetTextureIdDefault());
    rlBegin(RL_TRIANGLES);
    for (i = 0; i < surf->strip_count; i++) {
        for (k = 0; k + 2 < strips[i]; k++) {
            /* Every second triangle of a strip has its first two vertices swapped. */
            emit_vertex(&v[k + (k & 1)]);
            emit_vertex(&v[k + 1 - (k & 1)]);
            emit_vertex(&v[k + 2]);
        }
        v += strips[i];
    }
    rlEnd();
    rlSetTexture(0);
    rlPopMatrix();
}

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
            draw_surface(mesh, surf, world, textures);
        }
    }
}

/* Draws the translucent surfaces that were held back. Called when the meshes of the
 * frame are through: before the first particle, or at the end of the 3D part. */
static void draw_translucent(void)
{
    uint32_t i, k;

    if (g_translucent_count == 0) {
        return;
    }
    for (i = 0; i < g_translucent_count; i++) {
        /* Sorted by insertion, the furthest first; equal ones stay in tree order. */
        for (k = i; k > 0 && g_translucent[g_translucent_order[k - 1]].distance < g_translucent[i].distance; k--) {
            g_translucent_order[k] = g_translucent_order[k - 1];
        }
        g_translucent_order[k] = (uint16_t)i;
    }
    rlDrawRenderBatchActive();
    rlDisableDepthMask();
    for (i = 0; i < g_translucent_count; i++) {
        const Translucent *t = &g_translucent[g_translucent_order[i]];
        draw_surface(t->mesh, t->surface, &t->world, t->textures);
    }
    rlDrawRenderBatchActive();
    rlEnableDepthMask();
    g_translucent_count = 0;
}

void rr_gfx_draw_sky(uint32_t texture, const float *xyz, const float *uv)
{
    static const int order[4] = { 0, 3, 2, 1 }; /* counter-clockwise for rlgl */
    int              i;

    /* Drawn at once, before the meshes, without looking at or writing the depth. */
    rlDrawRenderBatchActive();
    rlDisableDepthTest();
    rlDisableDepthMask();
    rlDisableBackfaceCulling();
    rlSetTexture(texture < MAX_TEXTURES ? g_textures[texture].id : rlGetTextureIdDefault());
    rlBegin(RL_QUADS);
    rlColor4ub(255, 255, 255, 255);
    for (i = 0; i < 4; i++) {
        const int c = order[i];
        rlTexCoord2f(uv[c * 2], uv[c * 2 + 1]);
        rlVertex3f(xyz[c * 3], xyz[c * 3 + 1], xyz[c * 3 + 2]);
    }
    rlEnd();
    rlSetTexture(0);
    rlDrawRenderBatchActive();
    rlEnableBackfaceCulling();
    rlEnableDepthMask();
    rlEnableDepthTest();
}

void rr_gfx_draw_quad_3d(uint32_t texture, const float *xyz, const float *uv, uint32_t rgba)
{
    static const int order[4] = { 0, 3, 2, 1 }; /* counter-clockwise for rlgl */
    int              i;

    if (!g_in_particles) {
        draw_translucent();
        /* Particles are tested against the depth buffer but do not write to it, so they
         * do not cut holes into each other. */
        rlDrawRenderBatchActive();
        rlDisableDepthMask();
        rlDisableBackfaceCulling();
        g_in_particles = true;
    }
    rlSetTexture(texture < MAX_TEXTURES && g_textures[texture].id != 0 ? g_textures[texture].id
                                                                      : rlGetTextureIdDefault());
    rlBegin(RL_QUADS);
    rlColor4ub((unsigned char)(rgba >> 24), (unsigned char)(rgba >> 16), (unsigned char)(rgba >> 8),
               (unsigned char)rgba);
    for (i = 0; i < 4; i++) {
        const int c = order[i];
        rlTexCoord2f(uv[c * 2], uv[c * 2 + 1]);
        rlVertex3f(xyz[c * 3], xyz[c * 3 + 1], xyz[c * 3 + 2]);
    }
    rlEnd();
    rlSetTexture(0);
}

void rr_gfx_begin_2d(void)
{
    if (g_in_3d) {
        draw_translucent();
    }
    if (g_in_particles) {
        rlDrawRenderBatchActive();
        rlEnableDepthMask();
        g_in_particles = false;
    }
    fog_off();
    if (g_in_3d) {
        EndMode3D();
        g_in_3d = false;
    }
    rlDisableBackfaceCulling(); /* mirrored sprites turn their quads around */
}

uint32_t rr_gfx_texture_load(const RRPkgTexture *texture)
{
    const Image image = { (void *)(texture + 1), texture->width, texture->height, 1,
                          PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 };
    uint32_t    i;

    for (i = 0; i < MAX_TEXTURES; i++) {
        if (g_textures[i].id == 0) {
            g_textures[i] = LoadTextureFromImage(image);
            SetTextureFilter(g_textures[i], texture->filter == RR_FILTER_NEAREST ? TEXTURE_FILTER_POINT
                                                                               : TEXTURE_FILTER_BILINEAR);
            return i;
        }
    }
    TraceLog(LOG_WARNING, "retro: more than %d textures, one is missing", MAX_TEXTURES);
    return RR_NONE32;
}

void rr_gfx_texture_unload(uint32_t handle)
{
    if (handle < MAX_TEXTURES && g_textures[handle].id != 0) {
        UnloadTexture(g_textures[handle]);
        g_textures[handle].id = 0;
    }
}

void rr_gfx_draw_quad(uint32_t texture, const float *xy, const float *uv, uint32_t rgba)
{
    /* rlgl wants quads counter-clockwise on screen: top left, bottom left, bottom
     * right, top right. */
    static const int order[4] = { 0, 3, 2, 1 };
    int              i;

    rlSetTexture(texture < MAX_TEXTURES && g_textures[texture].id != 0 ? g_textures[texture].id
                                                                      : rlGetTextureIdDefault());
    rlBegin(RL_QUADS);
    rlColor4ub((unsigned char)(rgba >> 24), (unsigned char)(rgba >> 16), (unsigned char)(rgba >> 8),
               (unsigned char)rgba);
    for (i = 0; i < 4; i++) {
        const int c = order[i];
        rlTexCoord2f(uv[c * 2], uv[c * 2 + 1]);
        rlVertex2f(xy[c * 2], xy[c * 2 + 1]);
    }
    rlEnd();
    rlSetTexture(0);
}

void rr_gfx_end_frame(void)
{
    uint32_t i;

    rr_gfx_begin_2d(); /* leaves 3D mode if the scene had no 2D pass */
    EndDrawing();

    if (g_music_on) {
        UpdateMusicStream(g_music);
    }
    /* raylib sounds do not loop by themselves. */
    for (i = 0; i < MAX_SOUNDS; i++) {
        if (g_sound_loops[i] && !IsSoundPlaying(g_sounds[i])) {
            PlaySound(g_sounds[i]);
        }
    }
}

/* --- sound --------------------------------------------------------------------------- */

uint32_t rr_snd_load(const RRPkgSound *sound)
{
    const Wave wave = { sound->sample_count, sound->sample_rate, 16, sound->channels,
                        (void *)(sound + 1) };
    uint32_t   i;

    if (!IsAudioDeviceReady() || sound->format != RR_SOUND_PCM16) {
        return RR_NONE32;
    }
    for (i = 0; i < MAX_SOUNDS; i++) {
        if (g_sounds[i].frameCount == 0) {
            g_sounds[i] = LoadSoundFromWave(wave);
            return i;
        }
    }
    TraceLog(LOG_WARNING, "retro: more than %d sounds, one is missing", MAX_SOUNDS);
    return RR_NONE32;
}

void rr_snd_unload(uint32_t handle)
{
    if (handle < MAX_SOUNDS && g_sounds[handle].frameCount != 0) {
        UnloadSound(g_sounds[handle]);
        g_sounds[handle].frameCount = 0;
        g_sound_loops[handle] = false;
    }
}

void rr_snd_play(uint32_t handle, float volume, float pan, float pitch, bool loop)
{
    if (handle < MAX_SOUNDS && g_sounds[handle].frameCount != 0) {
        SetSoundVolume(g_sounds[handle], volume);
        SetSoundPan(g_sounds[handle], pan);
        SetSoundPitch(g_sounds[handle], pitch);
        PlaySound(g_sounds[handle]);
        g_sound_loops[handle] = loop;
    }
}

void rr_snd_set(uint32_t handle, float volume, float pan, float pitch)
{
    if (handle < MAX_SOUNDS && g_sounds[handle].frameCount != 0) {
        SetSoundVolume(g_sounds[handle], volume);
        SetSoundPan(g_sounds[handle], pan);
        SetSoundPitch(g_sounds[handle], pitch);
    }
}

bool rr_music_play(const char *path, float volume, bool loop)
{
    rr_music_stop();
    if (!IsAudioDeviceReady()) {
        return false;
    }
    g_music = LoadMusicStream(path);
    if (g_music.frameCount == 0) {
        TraceLog(LOG_WARNING, "retro: cannot play music '%s'", path);
        return false;
    }
    g_music.looping = loop;
    SetMusicVolume(g_music, volume);
    PlayMusicStream(g_music);
    g_music_on = true;
    return true;
}

void rr_music_set_volume(float volume)
{
    if (g_music_on) {
        SetMusicVolume(g_music, volume);
    }
}

void rr_music_stop(void)
{
    if (g_music_on) {
        StopMusicStream(g_music);
        UnloadMusicStream(g_music);
        g_music_on = false;
    }
}

void rr_snd_stop(uint32_t handle)
{
    if (handle < MAX_SOUNDS && g_sounds[handle].frameCount != 0) {
        g_sound_loops[handle] = false;
        StopSound(g_sounds[handle]);
    }
}

/* --- saved games: one file per block, in RR_SAVE_DIR or the working directory ------- */

static void save_path(char *path, size_t size, const char *name)
{
    const char *dir = getenv("RR_SAVE_DIR");
    snprintf(path, size, "%s/%s.sav", dir != NULL ? dir : ".", name);
}

bool rr_plat_save_write(const char *name, const void *data, uint32_t size)
{
    char  path[300];
    FILE *f;
    bool  ok;

    save_path(path, sizeof(path), name);
    f = fopen(path, "wb");
    if (f == NULL) {
        return false;
    }
    ok = fwrite(data, 1, size, f) == size;
    return fclose(f) == 0 && ok;
}

bool rr_plat_save_read(const char *name, void *data, uint32_t size)
{
    char  path[300];
    FILE *f;
    long  length;
    bool  ok;

    save_path(path, sizeof(path), name);
    f = fopen(path, "rb");
    if (f == NULL) {
        return false;
    }
    fseek(f, 0, SEEK_END);
    length = ftell(f);
    fseek(f, 0, SEEK_SET);
    ok = length == (long)size && fread(data, 1, size, f) == size;
    fclose(f);
    return ok;
}

const char *rr_plat_locale(void)
{
    /* The PC build is the development target: the language is asked for, not found out. */
    return getenv("RR_LOCALE");
}

void rr_plat_vibrate(float weak, float strong)
{
    /* raylib's desktop build (GLFW) cannot shake a pad: SetGamepadVibration only prints
     * a warning there, every time. So the PC has no rumble. */
    (void)weak;
    (void)strong;
}
