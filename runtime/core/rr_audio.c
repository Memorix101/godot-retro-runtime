/* AudioStreamPlayer nodes: plain, 2D and 3D.
 *
 * A plain player plays its sound as it is. A 2D or 3D player is heard from the active
 * camera: the volume falls with the distance and the sound moves between the left and the
 * right speaker, updated every frame while it plays. The rules are Godot's, without the
 * distance filter, the doppler effect and the emission angle.
 *
 * Sounds live in memory. Music is streamed from a file next to the package by the
 * platform layer, one piece at a time.
 *
 * A player that does not loop emits `finished` when its sound has run out. The runtime
 * works that out from the length of the sound instead of asking the platform. */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "rr_internal.h"
#include "rr_platform.h"

/* --- buses ---------------------------------------------------------------------------
 * The volume and the mute switch of every audio bus, by name. The packages hold what the
 * project's bus layout says; what the game changes is kept here and stays across scene
 * changes, as it does in Godot's AudioServer. */

typedef struct {
    char        name[32];
    float       volume;
    bool        mute;
} BusState;

static BusState g_buses[RR_MAX_BUSES];
static int      g_bus_count;
static bool     g_bus_changed; /* what plays has to be set again */

/* The state of a package's bus. A bus that was not heard of yet starts as the package
 * has it. NULL only if there are more bus names than RR_MAX_BUSES. */
static BusState *bus_state(const RRPackage *pkg, uint32_t bus)
{
    const char *name = rr_package_string(pkg, pkg->buses[bus].name);
    int         i;

    for (i = 0; i < g_bus_count; i++) {
        if (strcmp(g_buses[i].name, name) == 0) {
            return &g_buses[i];
        }
    }
    if (g_bus_count == RR_MAX_BUSES) {
        return NULL;
    }
    snprintf(g_buses[g_bus_count].name, sizeof(g_buses[g_bus_count].name), "%s", name);
    g_buses[g_bus_count].volume = pkg->buses[bus].volume;
    g_buses[g_bus_count].mute   = pkg->buses[bus].mute != 0;
    return &g_buses[g_bus_count++];
}

/* What a bus does to the volume of its players: its own volume and that of every bus on
 * the way to Master. */
static float bus_gain(const RRPackage *pkg, uint32_t bus)
{
    float gain = 1.0f;

    while (bus < pkg->header->buses.count) {
        const BusState *state = bus_state(pkg, bus);
        if (state != NULL) {
            gain *= state->mute ? 0.0f : state->volume;
        }
        bus = pkg->buses[bus].send;
    }
    return gain;
}

int rr_audio_bus_index(const RRScene *scene, const char *name)
{
    uint32_t i;
    for (i = 0; i < scene->pkg->header->buses.count; i++) {
        if (strcmp(rr_package_string(scene->pkg, scene->pkg->buses[i].name), name) == 0) {
            return (int)i;
        }
    }
    return -1;
}

static BusState *bus_of(const RRScene *scene, int bus)
{
    return bus >= 0 && (uint32_t)bus < scene->pkg->header->buses.count
               ? bus_state(scene->pkg, (uint32_t)bus) : NULL;
}

void rr_audio_bus_set_volume(RRScene *scene, int bus, float volume)
{
    BusState *state = bus_of(scene, bus);
    if (state != NULL) {
        state->volume = volume < 0.0f ? 0.0f : volume;
        g_bus_changed = true;
    }
}

float rr_audio_bus_get_volume(const RRScene *scene, int bus)
{
    const BusState *state = bus_of(scene, bus);
    return state != NULL ? state->volume : 0.0f;
}

void rr_audio_bus_set_mute(RRScene *scene, int bus, bool mute)
{
    BusState *state = bus_of(scene, bus);
    if (state != NULL) {
        state->mute   = mute;
        g_bus_changed = true;
    }
}

bool rr_audio_bus_is_mute(const RRScene *scene, int bus)
{
    const BusState *state = bus_of(scene, bus);
    return state != NULL && state->mute;
}

/* --- players ------------------------------------------------------------------------- */

static bool is_player(const Node *n)
{
    return n->type == RR_NODE_AUDIO || n->type == RR_NODE_AUDIO_SPATIAL;
}

static RRAudioState *state_of(const Node *player)
{
    RRScene       *scene = player->scene;
    const uint16_t index = (uint16_t)(player - scene->nodes);
    uint32_t       i;

    if (!is_player(player)) {
        return NULL;
    }
    for (i = 0; i < scene->audio_count; i++) {
        if (scene->audios[i].node == index) {
            return &scene->audios[i];
        }
    }
    return NULL;
}

void rr_audio_init(RRScene *scene, Node *player)
{
    RRAudioState *state = &scene->audios[scene->audio_count++];

    state->node    = (uint16_t)(player - scene->nodes);
    state->playing = false;
    state->left    = 0.0f;
    state->pitch   = ((const RRPkgAudioNode *)player->data)->pitch;
    state->volume  = ((const RRPkgAudioNode *)player->data)->volume;
    state->changed = false;
}

static bool is_music(const Node *player)
{
    const RRPkgAudioNode *a = (const RRPkgAudioNode *)player->data;
    return player->scene->pkg->assets[a->sound].type == RR_ASSET_MUSIC;
}

static float length_of(const Node *player)
{
    const RRPackage      *pkg = player->scene->pkg;
    const RRPkgAudioNode *a = (const RRPkgAudioNode *)player->data;

    if (is_music(player)) {
        return ((const RRPkgMusic *)rr_package_asset(pkg, a->sound))->length;
    } else {
        const RRPkgSound *s = (const RRPkgSound *)rr_package_asset(pkg, a->sound);
        return s->sample_rate > 0 ? (float)s->sample_count / (float)s->sample_rate : 0.0f;
    }
}

void rr_audio_mix(const Node *player, float *volume, float *pan)
{
    const RRScene        *scene = player->scene;
    const RRPkgAudioNode *a = (const RRPkgAudioNode *)player->data;
    const float          *m = player->world.m;
    const RRAudioState   *state = state_of(player);
    float                 gain = state != NULL ? state->volume : a->volume, side = 0.0f;

    if (a->mode == RR_AUDIO_3D && scene->camera != NULL) {
        const float *c = scene->camera->world.m;
        const float  d[3] = { m[9] - c[9], m[10] - c[10], m[11] - c[11] };
        const float  dist = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
        const float  x = dist / (a->unit_size > 0.0f ? a->unit_size : 1.0f) + 0.00001f;
        const float  right = sqrtf(c[0] * c[0] + c[1] * c[1] + c[2] * c[2]);

        switch (a->model) {
        case RR_ATTENUATION_INVERSE:
            gain /= x;
            break;
        case RR_ATTENUATION_INVERSE_SQUARE:
            gain /= x * x;
            break;
        case RR_ATTENUATION_LOGARITHMIC:
            /* Godot: -20 * ln(x) dB, which is x to the power of -ln(10). */
            gain *= powf(x, -2.302585f);
            break;
        default:
            break;
        }
        if (gain > a->max_gain) {
            gain = a->max_gain;
        }
        if (a->max_distance > 0.0f) {
            const float fade = 1.0f - dist / a->max_distance;
            gain *= fade > 0.0f ? fade : 0.0f;
        }
        /* How far to the right of the camera the sound is, as a part of its distance. */
        if (dist > 0.0001f && right > 0.0f) {
            side = (d[0] * c[0] + d[1] * c[1] + d[2] * c[2]) / (dist * right);
        }
    } else if (a->mode == RR_AUDIO_2D) {
        /* The listener is the middle of the screen. */
        float lx = (float)RR_SCREEN_WIDTH * 0.5f, ly = (float)RR_SCREEN_HEIGHT * 0.5f;
        float half = (float)RR_SCREEN_WIDTH * 0.5f, dist, fade;

        if (scene->camera2d != NULL) {
            const RRPkgCamera2DNode *cam = (const RRPkgCamera2DNode *)scene->camera2d->data;
            lx = scene->camera2d->world.m[9] + cam->offset[0];
            ly = scene->camera2d->world.m[10] + cam->offset[1];
            if (cam->top_left) {
                lx += half / cam->zoom[0];
                ly += (float)RR_SCREEN_HEIGHT * 0.5f / cam->zoom[1];
            }
            half /= cam->zoom[0];
        }
        dist = sqrtf((m[9] - lx) * (m[9] - lx) + (m[10] - ly) * (m[10] - ly));
        fade = a->max_distance > 0.0f ? 1.0f - dist / a->max_distance : 1.0f;
        gain *= fade > 0.0f ? powf(fade, a->attenuation) : 0.0f;
        side = (m[9] - lx) / half;
    }
    side *= a->pan_strength;
    *volume = gain;
    *pan    = side < -1.0f ? -1.0f : (side > 1.0f ? 1.0f : side);
}

void rr_audio_play(Node *player)
{
    RRAudioState *state = state_of(player);

    if (state != NULL) {
        const RRScene        *scene = player->scene;
        const RRPkgAudioNode *a = (const RRPkgAudioNode *)player->data;
        float                 volume, pan;

        if (is_music(player)) {
            const RRPkgMusic *music = (const RRPkgMusic *)rr_package_asset(scene->pkg, a->sound);
            char              path[256];
            snprintf(path, sizeof(path), "%s/%s", rr_app_data_dir(),
                     rr_package_string(scene->pkg, music->file));
            rr_music_play(path, state->volume * bus_gain(scene->pkg, a->bus), a->loop != 0);
        } else {
            if (a->mode != RR_AUDIO_PLAIN) {
                /* The node may have been placed just now. */
                rr_node_world_now(player, &player->world);
            }
            rr_audio_mix(player, &volume, &pan);
            rr_snd_play(scene->handles[a->sound], volume * bus_gain(scene->pkg, a->bus), pan,
                        state->pitch, a->loop != 0);
        }
        state->playing = true;
        state->left    = length_of(player);
    }
}

void rr_audio_stop(Node *player)
{
    RRAudioState *state = state_of(player);

    if (state != NULL && state->playing) {
        const RRPkgAudioNode *a = (const RRPkgAudioNode *)player->data;
        if (is_music(player)) {
            rr_music_stop();
        } else {
            rr_snd_stop(player->scene->handles[a->sound]);
        }
        state->playing = false;
    }
}

bool rr_audio_is_playing(const Node *player)
{
    const RRAudioState *state = state_of(player);
    return state != NULL && state->playing;
}

void rr_audios_update(RRScene *scene, float dt)
{
    uint32_t i;

    for (i = 0; i < scene->audio_count; i++) {
        RRAudioState         *state = &scene->audios[i];
        Node                 *node = &scene->nodes[state->node];
        const RRPkgAudioNode *a = (const RRPkgAudioNode *)node->data;

        if (!state->playing) {
            continue;
        }
        if (node->flags & RR_NODE_FLAG_FREE) {
            /* A freed player falls silent, as in Godot. */
            rr_audio_stop(node);
            continue;
        }
        if (!a->loop) {
            state->left -= dt * state->pitch;
            if (state->left <= 0.0f) {
                state->playing = false;
                rr_emit(node, "finished", NULL);
                continue;
            }
        }
        /* A sound in the world changes as it and the camera move; any other one only when
         * its pitch or a bus was changed. */
        if (is_music(node)) {
            if (g_bus_changed) {
                rr_music_set_volume(state->volume * bus_gain(scene->pkg, a->bus));
            }
        } else if (a->mode != RR_AUDIO_PLAIN || g_bus_changed || state->changed) {
            float volume, pan;
            rr_audio_mix(node, &volume, &pan);
            rr_snd_set(scene->handles[a->sound], volume * bus_gain(scene->pkg, a->bus), pan, state->pitch);
            state->changed = false;
        }
    }
    g_bus_changed = false;
}

void rr_audio_set_pitch_scale(Node *player, float pitch)
{
    RRAudioState *state = state_of(player);
    if (state != NULL && pitch > 0.0f && !is_music(player)) {
        state->pitch   = pitch;
        state->changed = true;
    }
}

float rr_audio_get_pitch_scale(const Node *player)
{
    const RRAudioState *state = state_of(player);
    return state != NULL ? state->pitch : 1.0f;
}

void rr_audio_set_volume_db(Node *player, float db)
{
    RRAudioState *state = state_of(player);
    if (state != NULL) {
        /* Godot's db_to_linear; -80 dB and below is silence there. */
        state->volume  = db <= -80.0f ? 0.0f : powf(10.0f, db / 20.0f);
        state->changed = true;
        if (state->playing && is_music(player)) {
            const RRPkgAudioNode *a = (const RRPkgAudioNode *)player->data;
            rr_music_set_volume(state->volume * bus_gain(player->scene->pkg, a->bus));
        }
    }
}

float rr_audio_get_volume_db(const Node *player)
{
    const RRAudioState *state = state_of(player);
    if (state == NULL) {
        return 0.0f;
    }
    return state->volume > 0.0f ? 20.0f * log10f(state->volume) : -80.0f;
}
