/* Tweens: a property of a node moves to a target value over a time, after Godot's
 * Tween.tween_property() with its transition and ease types.
 *
 * The scene has a fixed number of tween slots, so nothing is allocated when one starts.
 * A tween reads its start value when its delay is over, like Godot's does when its turn
 * comes. Starting a tween for a property that is already being tweened replaces the old
 * one. The curves are the usual easing equations; "in" is the form written out below,
 * the other three eases are made from it. */
#include <math.h>

#include "rr_internal.h"

#define PI 3.14159265f

/* The "ease in" shape of a transition: 0 at t = 0, 1 at t = 1. */
static float ease_in(int trans, float t)
{
    switch (trans) {
    case RR_TRANS_SINE:
        return 1.0f - cosf(t * PI * 0.5f);
    case RR_TRANS_QUAD:
        return t * t;
    case RR_TRANS_CUBIC:
        return t * t * t;
    case RR_TRANS_QUART:
        return t * t * t * t;
    case RR_TRANS_QUINT:
        return t * t * t * t * t;
    case RR_TRANS_EXPO:
        return t <= 0.0f ? 0.0f : powf(2.0f, 10.0f * (t - 1.0f));
    case RR_TRANS_CIRC:
        return 1.0f - sqrtf(1.0f - t * t);
    case RR_TRANS_BACK:
        return t * t * (2.70158f * t - 1.70158f);
    case RR_TRANS_ELASTIC:
        if (t <= 0.0f || t >= 1.0f) {
            return t <= 0.0f ? 0.0f : 1.0f;
        }
        return -powf(2.0f, 10.0f * (t - 1.0f)) * sinf((t - 1.075f) * (2.0f * PI) / 0.3f);
    case RR_TRANS_BOUNCE: {
        /* Written for "out"; mirrored here. */
        float u = 1.0f - t, out;
        if (u < 1.0f / 2.75f) {
            out = 7.5625f * u * u;
        } else if (u < 2.0f / 2.75f) {
            u -= 1.5f / 2.75f;
            out = 7.5625f * u * u + 0.75f;
        } else if (u < 2.5f / 2.75f) {
            u -= 2.25f / 2.75f;
            out = 7.5625f * u * u + 0.9375f;
        } else {
            u -= 2.625f / 2.75f;
            out = 7.5625f * u * u + 0.984375f;
        }
        return 1.0f - out;
    }
    default:
        return t;
    }
}

static float curve(int trans, int ease, float t)
{
    switch (ease) {
    case RR_EASE_OUT:
        return 1.0f - ease_in(trans, 1.0f - t);
    case RR_EASE_IN_OUT:
        return t < 0.5f ? ease_in(trans, t * 2.0f) * 0.5f
                        : 1.0f - ease_in(trans, 2.0f - t * 2.0f) * 0.5f;
    case RR_EASE_OUT_IN:
        return t < 0.5f ? (1.0f - ease_in(trans, 1.0f - t * 2.0f)) * 0.5f
                        : 0.5f + ease_in(trans, t * 2.0f - 1.0f) * 0.5f;
    default:
        return ease_in(trans, t);
    }
}

static float axis_length(const float *m, int axis)
{
    return sqrtf(m[axis * 3] * m[axis * 3] + m[axis * 3 + 1] * m[axis * 3 + 1] +
                 m[axis * 3 + 2] * m[axis * 3 + 2]);
}

static void read_property(const Node *node, int property, float *v)
{
    const float *m = node->local.m;
    int          i;

    v[0] = v[1] = v[2] = 0.0f;
    switch (property) {
    case RR_TWEEN_POSITION:
        for (i = 0; i < 3; i++) {
            v[i] = m[9 + i];
        }
        break;
    case RR_TWEEN_SCALE:
        for (i = 0; i < 3; i++) {
            v[i] = axis_length(m, i);
        }
        break;
    case RR_TWEEN_ROTATION_2D:
        v[0] = atan2f(m[1], m[0]);
        break;
    default:
        v[0] = node->value;
        break;
    }
}

static void write_property(Node *node, int property, const float *v)
{
    float *m = node->local.m;
    int    i, k;

    switch (property) {
    case RR_TWEEN_POSITION:
        for (i = 0; i < 3; i++) {
            m[9 + i] = v[i];
        }
        break;
    case RR_TWEEN_SCALE:
        for (i = 0; i < 3; i++) {
            const float length = axis_length(m, i);
            /* Never quite zero, so the direction of the axis survives. */
            const float target = fabsf(v[i]) < 0.0001f ? 0.0001f : v[i];
            for (k = 0; k < 3; k++) {
                m[i * 3 + k] = length > 0.000001f ? m[i * 3 + k] / length * target
                                                  : (i == k ? target : 0.0f);
            }
        }
        break;
    case RR_TWEEN_ROTATION_2D: {
        const float sx = sqrtf(m[0] * m[0] + m[1] * m[1]), sy = sqrtf(m[3] * m[3] + m[4] * m[4]);
        m[0] = cosf(v[0]) * sx;
        m[1] = sinf(v[0]) * sx;
        m[3] = -sinf(v[0]) * sy;
        m[4] = cosf(v[0]) * sy;
        break;
    }
    default:
        node->value = v[0];
        break;
    }
}

bool rr_tween_property(Node *node, int property, const float *to, float duration, float delay,
                       int trans, int ease)
{
    RRScene       *scene = node->scene;
    const uint16_t index = (uint16_t)(node - scene->nodes);
    RRTween       *slot = NULL;
    uint32_t       i;

    for (i = 0; i < RR_MAX_TWEENS; i++) {
        RRTween *t = &scene->tweens[i];
        if (t->active && t->node == index && t->property == property) {
            slot = t;
            break;
        }
        if (!t->active && slot == NULL) {
            slot = t;
        }
    }
    if (slot == NULL) {
        return false; /* all slots are busy */
    }
    slot->active   = true;
    slot->started  = false;
    slot->node     = index;
    slot->property = (uint8_t)property;
    slot->trans    = (uint8_t)trans;
    slot->ease     = (uint8_t)ease;
    slot->to[0]    = to[0];
    slot->to[1]    = property == RR_TWEEN_POSITION || property == RR_TWEEN_SCALE ? to[1] : 0.0f;
    slot->to[2]    = property == RR_TWEEN_POSITION || property == RR_TWEEN_SCALE ? to[2] : 0.0f;
    slot->duration = duration;
    slot->elapsed  = -delay;
    return true;
}

bool rr_tween_is_running(const Node *node)
{
    const RRScene *scene = node->scene;
    const uint16_t index = (uint16_t)(node - scene->nodes);
    uint32_t       i;

    for (i = 0; i < RR_MAX_TWEENS; i++) {
        if (scene->tweens[i].active && scene->tweens[i].node == index) {
            return true;
        }
    }
    return false;
}

void rr_tween_kill(Node *node)
{
    RRScene       *scene = node->scene;
    const uint16_t index = (uint16_t)(node - scene->nodes);
    uint32_t       i;

    for (i = 0; i < RR_MAX_TWEENS; i++) {
        if (scene->tweens[i].node == index) {
            scene->tweens[i].active = false;
        }
    }
}

void rr_tweens_update(RRScene *scene, float dt)
{
    uint32_t i;
    int      k;

    for (i = 0; i < RR_MAX_TWEENS; i++) {
        RRTween *t = &scene->tweens[i];
        Node    *node = &scene->nodes[t->node];
        float    v[3], w;

        if (!t->active) {
            continue;
        }
        if (node->flags & RR_NODE_FLAG_FREE) {
            t->active = false;
            continue;
        }
        t->elapsed += dt;
        if (t->elapsed < 0.0f) {
            continue; /* still in its delay */
        }
        if (!t->started) {
            read_property(node, t->property, t->from);
            t->started = true;
        }
        w = t->duration > 0.0f && t->elapsed < t->duration
                ? curve(t->trans, t->ease, t->elapsed / t->duration)
                : 1.0f;
        for (k = 0; k < 3; k++) {
            v[k] = t->from[k] + (t->to[k] - t->from[k]) * w;
        }
        write_property(node, t->property, v);
        if (t->elapsed >= t->duration) {
            t->active = false;
        }
    }
}
