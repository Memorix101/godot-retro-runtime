/* Keyframe animation: the clips of AnimationPlayer nodes.
 *
 * A clip is a list of tracks. Most of them drive one part of one node's local
 * transform (position, rotation or scale, in 3D or 2D), its visibility, the frame of a
 * sprite or the value of a progress bar or path follower. A method track calls a handler
 * of the node's component when the clip passes one of its keys. The parts a clip has no
 * track for keep the value they have, so a component may move a node while a clip
 * turns it. One player plays one clip at a time; while a new clip takes over from the
 * one before it (the blend time), the parts both of them move are blended. */
#include <math.h>
#include <string.h>

#include "rr_internal.h"

static const RRPkgAnimations *clips_of(const Node *player)
{
    const RRPkgAnimationPlayerNode *data = (const RRPkgAnimationPlayerNode *)player->data;
    return (const RRPkgAnimations *)rr_package_asset(player->scene->pkg, data->animations);
}

static RRAnimState *state_of(const Node *player)
{
    RRScene       *scene = player->scene;
    const uint16_t index = (uint16_t)(player - scene->nodes);
    uint32_t       i;

    if (player->type != RR_NODE_ANIMATION_PLAYER) {
        return NULL;
    }
    for (i = 0; i < scene->anim_count; i++) {
        if (scene->anims[i].node == index) {
            return &scene->anims[i];
        }
    }
    return NULL;
}

void rr_anim_init(RRScene *scene, Node *player)
{
    RRAnimState *state = &scene->anims[scene->anim_count++];

    state->node = (uint16_t)(player - scene->nodes);
    rr_anim_reset(player);
}

void rr_anim_reset(Node *player)
{
    const RRPkgAnimations *set = clips_of(player);
    RRAnimState           *state = state_of(player);

    state->clip    = set->autoplay == RR_NONE32 ? -1 : (int16_t)set->autoplay;
    state->last    = -1.0f;
    state->playing = set->autoplay != RR_NONE32;
    state->time    = 0.0f;
    state->speed      = ((const RRPkgAnimationPlayerNode *)player->data)->speed_scale;
    state->blend_time = ((const RRPkgAnimationPlayerNode *)player->data)->blend_time;
    state->blend_left = 0.0f;
    state->from_clip  = -1;
}

/* --- sampling ------------------------------------------------------------------------- */

/* Value of a track at a time: the keys around it, blended unless the track steps. */
static void sample(const RRPkgTrack *track, const RRPkgKey *keys, float time, float *out)
{
    uint32_t        k = 0, i;
    const RRPkgKey *a, *b;
    float           t, dot = 0.0f, wa, wb;

    while (k + 1 < track->key_count && keys[k + 1].time <= time) {
        k++;
    }
    a = &keys[k];
    b = k + 1 < track->key_count ? &keys[k + 1] : a;
    if (a == b || time <= a->time || track->interp == RR_INTERP_NEAREST || !(b->time > a->time)) {
        memcpy(out, a->value, sizeof(a->value));
        return;
    }
    t  = (time - a->time) / (b->time - a->time);
    wa = 1.0f - t;
    wb = t;
    if (track->kind == RR_TRACK_ROTATION) {
        /* Spherical interpolation along the shorter way. */
        for (i = 0; i < 4; i++) {
            dot += a->value[i] * b->value[i];
        }
        if (fabsf(dot) < 0.9995f) {
            const float angle = acosf(fabsf(dot));
            const float inv = 1.0f / sinf(angle);
            wa = sinf((1.0f - t) * angle) * inv;
            wb = sinf(t * angle) * inv;
        }
        if (dot < 0.0f) {
            wb = -wb;
        }
    }
    for (i = 0; i < 4; i++) {
        out[i] = a->value[i] * wa + b->value[i] * wb;
    }
}

/* Splits the basis of a node into unit axes and their lengths, replaces one of the two
 * by the track's value and puts the basis together again. */
static void apply_basis(Node *node, uint8_t kind, const float *v)
{
    float *m = node->local.m;
    float  axis[9], scale[3];
    int    i, k;

    for (i = 0; i < 3; i++) {
        scale[i] = sqrtf(m[i * 3] * m[i * 3] + m[i * 3 + 1] * m[i * 3 + 1] + m[i * 3 + 2] * m[i * 3 + 2]);
        for (k = 0; k < 3; k++) {
            /* An axis that was scaled to nothing has no direction left. */
            axis[i * 3 + k] = scale[i] > 0.000001f ? m[i * 3 + k] / scale[i] : (i == k ? 1.0f : 0.0f);
        }
    }
    if (kind == RR_TRACK_SCALE) {
        for (i = 0; i < 3; i++) {
            /* Never quite zero, so the rotation survives a scale track that ends at 0. */
            scale[i] = fabsf(v[i]) < 0.0001f ? 0.0001f : v[i];
        }
    } else {
        const float x = v[0], y = v[1], z = v[2], w = v[3];
        const float n = x * x + y * y + z * z + w * w;
        const float s = n > 0.0f ? 2.0f / n : 0.0f;
        axis[0] = 1.0f - s * (y * y + z * z);
        axis[1] = s * (x * y + z * w);
        axis[2] = s * (x * z - y * w);
        axis[3] = s * (x * y - z * w);
        axis[4] = 1.0f - s * (x * x + z * z);
        axis[5] = s * (y * z + x * w);
        axis[6] = s * (x * z + y * w);
        axis[7] = s * (y * z - x * w);
        axis[8] = 1.0f - s * (x * x + y * y);
    }
    for (i = 0; i < 3; i++) {
        for (k = 0; k < 3; k++) {
            m[i * 3 + k] = axis[i * 3 + k] * scale[i];
        }
    }
}

/* The same for a 2D node: an angle and two lengths. */
static void apply_basis_2d(Node *node, uint8_t kind, const float *v)
{
    float *m = node->local.m;
    float  sx = sqrtf(m[0] * m[0] + m[1] * m[1]), sy = sqrtf(m[3] * m[3] + m[4] * m[4]);
    float  angle = atan2f(m[1], m[0]);

    if (kind == RR_TRACK_ROTATION_2D) {
        angle = v[0];
    } else {
        sx = v[0];
        sy = v[1];
    }
    m[0] = cosf(angle) * sx;
    m[1] = sinf(angle) * sx;
    m[3] = -sinf(angle) * sy;
    m[4] = cosf(angle) * sy;
}

/* --- blending ------------------------------------------------------------------------- */

static bool has_track(const RRPkgAnimations *set, const RRPkgClip *clip, uint16_t node, uint8_t kind)
{
    const RRPkgTrack *tracks = (const RRPkgTrack *)((const uint8_t *)set + clip->tracks_off);
    uint32_t          t;

    for (t = 0; t < clip->track_count; t++) {
        if (tracks[t].node == node && tracks[t].kind == kind) {
            return true;
        }
    }
    return false;
}

/* What a track's part of a node is right now, in the form the keys of such a track have. */
static void current_value(const Node *node, uint8_t kind, float *v)
{
    const float *m = node->local.m;
    float        r[9], length;
    int          i, k;

    v[0] = v[1] = v[2] = v[3] = 0.0f;
    switch (kind) {
    case RR_TRACK_POSITION:
        v[2] = m[11];
        /* fall through */
    case RR_TRACK_POSITION_2D:
        v[0] = m[9];
        v[1] = m[10];
        break;
    case RR_TRACK_SCALE:
        v[2] = sqrtf(m[6] * m[6] + m[7] * m[7] + m[8] * m[8]);
        /* fall through */
    case RR_TRACK_SCALE_2D:
        v[0] = sqrtf(m[0] * m[0] + m[1] * m[1] + m[2] * m[2]);
        v[1] = sqrtf(m[3] * m[3] + m[4] * m[4] + m[5] * m[5]);
        break;
    case RR_TRACK_ROTATION_2D:
        v[0] = atan2f(m[1], m[0]);
        break;
    case RR_TRACK_VALUE:
        v[0] = node->value;
        break;
    case RR_TRACK_ROTATION:
        /* The quaternion of the basis without its scale. r[column * 3 + row]. */
        for (i = 0; i < 3; i++) {
            length = sqrtf(m[i * 3] * m[i * 3] + m[i * 3 + 1] * m[i * 3 + 1] + m[i * 3 + 2] * m[i * 3 + 2]);
            for (k = 0; k < 3; k++) {
                r[i * 3 + k] = length > 0.000001f ? m[i * 3 + k] / length : (i == k ? 1.0f : 0.0f);
            }
        }
        if (r[0] + r[4] + r[8] > 0.0f) {
            const float s = sqrtf(r[0] + r[4] + r[8] + 1.0f) * 2.0f;
            v[3] = s * 0.25f;
            v[0] = (r[5] - r[7]) / s;
            v[1] = (r[6] - r[2]) / s;
            v[2] = (r[1] - r[3]) / s;
        } else if (r[0] > r[4] && r[0] > r[8]) {
            const float s = sqrtf(1.0f + r[0] - r[4] - r[8]) * 2.0f;
            v[3] = (r[5] - r[7]) / s;
            v[0] = s * 0.25f;
            v[1] = (r[3] + r[1]) / s;
            v[2] = (r[6] + r[2]) / s;
        } else if (r[4] > r[8]) {
            const float s = sqrtf(1.0f + r[4] - r[0] - r[8]) * 2.0f;
            v[3] = (r[6] - r[2]) / s;
            v[0] = (r[3] + r[1]) / s;
            v[1] = s * 0.25f;
            v[2] = (r[7] + r[5]) / s;
        } else {
            const float s = sqrtf(1.0f + r[8] - r[0] - r[4]) * 2.0f;
            v[3] = (r[1] - r[3]) / s;
            v[0] = (r[6] + r[2]) / s;
            v[1] = (r[7] + r[5]) / s;
            v[2] = s * 0.25f;
        }
        break;
    default:
        break;
    }
}

/* The rotation that is `part` (0..1) of the way from no rotation to `q`, the shorter way
 * round. */
static void part_of_rotation(const float *q, float part, float *out)
{
    const float sign = q[3] < 0.0f ? -1.0f : 1.0f;
    const float w = q[3] * sign > 1.0f ? 1.0f : q[3] * sign;
    const float half = acosf(w), sine = sinf(half);
    /* Almost no rotation at all: its vector part scales with it. */
    const float scale = sine > 0.0001f ? sinf(half * part) / sine : part;
    int         i;

    for (i = 0; i < 3; i++) {
        out[i] = q[i] * sign * scale;
    }
    out[3] = cosf(half * part);
}

/* Goes `weight` of the way from `from` to `to`, and leaves the result in `to`. A frame
 * and a visibility have nothing in between: they are the new clip's at once. */
static void blend_value(uint8_t kind, const float *from, float *to, float weight)
{
    float sign = 1.0f, turn;
    int   i;

    switch (kind) {
    case RR_TRACK_FRAME:
    case RR_TRACK_VISIBLE:
        break;
    case RR_TRACK_ROTATION_2D:
        /* Along the shorter way round. */
        turn = fmodf(to[0] - from[0], 6.2831853f);
        turn = turn > 3.1415927f ? turn - 6.2831853f : (turn < -3.1415927f ? turn + 6.2831853f : turn);
        to[0] = from[0] + turn * weight;
        break;
    case RR_TRACK_ROTATION: {
        /* As Godot's mixer does it: each clip turns the node by its share of its own
         * rotation, the new clip first. */
        float a[4], b[4];
        part_of_rotation(to, weight, a);
        part_of_rotation(from, 1.0f - weight, b);
        to[0] = a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1];
        to[1] = a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0];
        to[2] = a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3];
        to[3] = a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2];
        break;
    }
    default:
        for (i = 0; i < 4; i++) {
            to[i] = from[i] * sign * (1.0f - weight) + to[i] * weight;
        }
        break;
    }
}

/* Shows a clip at a time. With `from`, the clip before it, the parts that one moves as
 * well go only `weight` of the way from where they are to where this clip has them. */
static void apply_clip(RRScene *scene, const RRPkgAnimations *set, const RRPkgClip *clip, float time,
                       const RRPkgClip *from, float weight)
{
    const RRPkgTrack *tracks = (const RRPkgTrack *)((const uint8_t *)set + clip->tracks_off);
    uint32_t          t;

    for (t = 0; t < clip->track_count; t++) {
        const RRPkgKey *keys = (const RRPkgKey *)((const uint8_t *)set + tracks[t].keys_off);
        Node           *node = &scene->nodes[tracks[t].node];
        float           v[4];

        if (tracks[t].kind == RR_TRACK_METHOD) {
            continue; /* fired by fire_methods */
        }
        sample(&tracks[t], keys, time, v);
        if (from != NULL && has_track(set, from, tracks[t].node, tracks[t].kind)) {
            float now[4];
            current_value(node, tracks[t].kind, now);
            blend_value(tracks[t].kind, now, v, weight);
        }
        switch (tracks[t].kind) {
        case RR_TRACK_POSITION:
            node->local.m[9]  = v[0];
            node->local.m[10] = v[1];
            node->local.m[11] = v[2];
            break;
        case RR_TRACK_POSITION_2D:
            node->local.m[9]  = v[0];
            node->local.m[10] = v[1];
            break;
        case RR_TRACK_ROTATION_2D:
        case RR_TRACK_SCALE_2D:
            apply_basis_2d(node, tracks[t].kind, v);
            break;
        case RR_TRACK_FRAME:
            node->frame = (uint16_t)(v[0] + 0.5f);
            break;
        case RR_TRACK_VALUE:
            node->value = v[0];
            break;
        case RR_TRACK_VISIBLE:
            if (v[0] >= 0.5f) {
                node->flags |= RR_NODE_FLAG_VISIBLE;
            } else {
                node->flags &= (uint8_t)~RR_NODE_FLAG_VISIBLE;
            }
            break;
        default:
            apply_basis(node, tracks[t].kind, v);
            break;
        }
    }
}

/* Calls the handlers of the method keys with from < time <= to. */
static void fire_methods(RRScene *scene, const RRPkgAnimations *set, const RRPkgClip *clip,
                         float from, float to)
{
    const RRPkgTrack *tracks = (const RRPkgTrack *)((const uint8_t *)set + clip->tracks_off);
    uint32_t          t, k;

    for (t = 0; t < clip->track_count; t++) {
        const RRPkgKey *keys = (const RRPkgKey *)((const uint8_t *)set + tracks[t].keys_off);
        if (tracks[t].kind != RR_TRACK_METHOD) {
            continue;
        }
        for (k = 0; k < tracks[t].key_count; k++) {
            if (keys[k].time > from && keys[k].time <= to) {
                rr_node_call(&scene->nodes[tracks[t].node],
                             rr_package_string(scene->pkg, (uint32_t)keys[k].value[0]), NULL);
            }
        }
    }
}

/* Moves a time on inside a clip: round again for a clip that loops, else up to its end. */
static float advance(const RRPkgClip *clip, float time, float step)
{
    time += step;
    if (time >= clip->length) {
        time = clip->loop && clip->length > 0.0f ? fmodf(time, clip->length) : clip->length;
    }
    return time;
}

void rr_anims_update(RRScene *scene, float dt)
{
    uint32_t i;

    for (i = 0; i < scene->anim_count; i++) {
        RRAnimState           *state = &scene->anims[i];
        const RRPkgAnimations *set;
        const RRPkgClip       *clip, *from = NULL;
        const float            step = dt * state->speed;
        float                  weight = 1.0f;

        if (!state->playing || (scene->nodes[state->node].flags & RR_NODE_FLAG_FREE)) {
            continue;
        }
        set  = clips_of(&scene->nodes[state->node]);
        clip = &set->clips[state->clip];
        if (state->blend_left > 0.0f && state->from_clip >= 0 && state->blend_time > 0.0f) {
            /* The clip before goes on while this one takes over: it is shown first, and
             * this one is laid over it. The weights are Godot's: what is left of the
             * change counts for the clip before, what was done of it a frame ago for this
             * one, and the two are scaled so that they add up to one. */
            const float before = state->blend_left;
            float       after = before - step / state->blend_time;

            after  = after > 0.0f ? after : 0.0f;
            weight = 1.0f - before + after > 0.0f ? (1.0f - before) / (1.0f - before + after) : 1.0f;
            from   = &set->clips[state->from_clip];
            apply_clip(scene, set, from, state->from_time, NULL, 1.0f);
            state->from_time  = advance(from, state->from_time, step);
            state->blend_left = after;
        }
        /* The clip is shown at its current time first and advanced afterwards, so a clip
         * that was just started shows its first frame. */
        apply_clip(scene, set, clip, state->time, from, weight);
        fire_methods(scene, set, clip, state->last, state->time);
        state->last = state->time;
        if (state->time >= clip->length && !clip->loop) {
            /* The end of the clip ends a change that is still under way, as in Godot. */
            state->blend_left = 0.0f;
            state->playing    = false;
            rr_emit(&scene->nodes[state->node], "animation_finished", NULL);
            continue;
        }
        state->time += step;
        if (state->time >= clip->length) {
            if (clip->loop) {
                /* The keys up to the end of this round, then on from the start. */
                fire_methods(scene, set, clip, state->last, clip->length);
                state->last = -1.0f;
                state->time = fmodf(state->time, clip->length);
            } else {
                state->time = clip->length;
            }
        }
    }
}

/* --- for game components -------------------------------------------------------------- */

void rr_anim_play(Node *player, const char *clip)
{
    RRAnimState           *state = state_of(player);
    const RRPkgAnimations *set;
    uint32_t               i;

    if (state == NULL) {
        return;
    }
    set = clips_of(player);
    for (i = 0; i < set->clip_count; i++) {
        if (strcmp(rr_package_string(player->scene->pkg, set->clips[i].name), clip) == 0) {
            /* Another clip that is playing hands over to this one, if the player has a
             * blend time. */
            if (state->playing && state->clip >= 0 && state->clip != (int16_t)i && state->blend_time > 0.0f) {
                state->from_clip  = state->clip;
                state->from_time  = state->time;
                state->blend_left = 1.0f;
            } else {
                state->blend_left = 0.0f;
            }
            state->clip    = (int16_t)i;
            state->time    = 0.0f;
            state->last    = -1.0f;
            state->playing = true;
            return;
        }
    }
}

void rr_anim_set_speed_scale(Node *player, float speed)
{
    RRAnimState *state = state_of(player);
    if (state != NULL && speed >= 0.0f) {
        state->speed = speed;
    }
}

float rr_anim_get_speed_scale(const Node *player)
{
    const RRAnimState *state = state_of(player);
    return state != NULL ? state->speed : 1.0f;
}

void rr_anim_set_blend_time(Node *player, float seconds)
{
    RRAnimState *state = state_of(player);
    if (state != NULL && seconds >= 0.0f) {
        state->blend_time = seconds;
        if (seconds == 0.0f) {
            state->blend_left = 0.0f; /* a change that is under way is over at once */
        }
    }
}

void rr_anim_stop(Node *player)
{
    RRAnimState *state = state_of(player);
    if (state != NULL) {
        state->playing = false;
    }
}

bool rr_anim_playing(const Node *player)
{
    const RRAnimState *state = state_of(player);
    return state != NULL && state->playing;
}

const char *rr_anim_current(const Node *player)
{
    const RRAnimState *state = state_of(player);
    if (state == NULL || state->clip < 0) {
        return NULL;
    }
    return rr_package_string(player->scene->pkg, clips_of(player)->clips[state->clip].name);
}
