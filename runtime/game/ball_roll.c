/* Component of the ball demo (scenes/ball_roll.tscn).
 * Placeholder script: components/ball_roller.gd.
 *
 * The ball is a RigidBody3D with a sphere shape. The runtime lets it fall, keeps it out
 * of the floors (StaticBody3D nodes with box shapes, flat, sloped or banked) and takes
 * the speed into a floor away, which is all it takes for a ball to run down a slope and
 * drop off an edge. This component adds the push of the stick, as if the floor were
 * leaned that way, and everything one sees: the camera behind the ball that swings round
 * to where the ball is going and leans with the stick, the turning of the ball's shell,
 * and the rider inside who walks as fast as the ball rolls.
 *
 * Coins, checkpoints and the goal are areas in the groups "coin", "checkpoint" and
 * "goal", reported by the `area_entered` signal of the ball's own hitbox area. A ball
 * that falls below `fall_limit` costs a life and starts again at the last checkpoint. */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "../core/rr_scene.h"
#include "game_state.h"

#define PI_F 3.14159265f

typedef struct {
    float       push;         /* exported: metres per second the stick adds per second */
    float       max_speed;    /* exported: over the ground, metres per second */
    float       tilt_degrees; /* exported: how far the view leans with the stick */
    float       radius;       /* exported: of the ball, for the turning of the shell */
    float       fall_limit;   /* exported: below this height the ball starts again */
    Node       *shell;        /* exported: the mesh that is turned as the ball rolls */
    Node       *rider;        /* exported: turned to face the way the ball rolls */
    Node       *anim;         /* exported: AnimationPlayer with the clips idle and walk */
    Node       *camera;       /* exported */
    Node       *coin_label;   /* exported */
    Node       *coin_sound;   /* exported */
    Node       *coin_burst;   /* exported: particle system fired at every collected coin */
    Node       *shadow_ray;   /* exported: RayCast3D that points down from the ball */
    Node       *shadow;       /* exported: Sprite3D that is put on the floor below */
    Node       *status_label; /* exported: shows lives, time and best time */
    const char *menu_scene;   /* exported */

    int   act_left, act_right, act_up, act_down, act_cancel;
    float start[3], start_yaw; /* where the ball starts again, and the view there */
    float yaw;                 /* the way the camera looks, around Y; 0 is along -Z */
    float distance, elevation; /* of the camera from the ball: metres, and radians above it */
    float lean[2];             /* the stick, smoothed: right and forward, -1..1 */
    float face;                /* the way the rider looks, around Y */
    float spin[9];             /* how the shell is turned: its three axes */
    int   coins, coin_total;
    char  coin_text[24];
    int   lives;
    float time, best; /* seconds; best is 0 before the first finished run */
    float done;       /* seconds since the goal was reached, 0 before */
    char  status_text[64];
} BallRoller;

/* What is kept between runs of the program. */
typedef struct {
    uint32_t magic;
    float    best;
} BallSave;

#define SAVE_NAME  "RR_BALL"
#define SAVE_MAGIC 0x424C4C31u

static float dot(const float *a, const float *b)
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static void cross(const float *a, const float *b, float *out)
{
    out[0] = a[1] * b[2] - a[2] * b[1];
    out[1] = a[2] * b[0] - a[0] * b[2];
    out[2] = a[0] * b[1] - a[1] * b[0];
}

static void normalize(float *v)
{
    const float length = sqrtf(dot(v, v));
    if (length > 0.00001f) {
        v[0] /= length;
        v[1] /= length;
        v[2] /= length;
    }
}

/* The difference between two directions, brought into -PI..PI. */
static float turn(float from, float to)
{
    float d = to - from;
    while (d > PI_F) {
        d -= 2.0f * PI_F;
    }
    while (d < -PI_F) {
        d += 2.0f * PI_F;
    }
    return d;
}

static void show_coins(BallRoller *b)
{
    if (b->coin_label != NULL) {
        snprintf(b->coin_text, sizeof(b->coin_text), "COINS %d/%d", b->coins, b->coin_total);
        rr_node_set_text(b->coin_label, b->coin_text);
    }
}

static void show_status(BallRoller *b)
{
    if (b->status_label != NULL) {
        const int now = (int)(b->time * 10.0f), best = (int)(b->best * 10.0f);
        if (b->done > 0.0f) {
            snprintf(b->status_text, sizeof(b->status_text), "GOAL!  TIME %d.%d", now / 10, now % 10);
        } else if (best > 0) {
            snprintf(b->status_text, sizeof(b->status_text), "LIVES %d  TIME %d.%d  BEST %d.%d",
                     b->lives, now / 10, now % 10, best / 10, best % 10);
        } else {
            snprintf(b->status_text, sizeof(b->status_text), "LIVES %d  TIME %d.%d", b->lives,
                     now / 10, now % 10);
        }
        rr_node_set_text(b->status_label, b->status_text);
    }
}

static void ball_roller_ready(Node *self, BallRoller *b)
{
    GameState *game = game_state(self->scene);
    BallSave   save;
    Node      *n = NULL;
    int        i;

    b->act_left   = rr_action(self->scene, "move_left");
    b->act_right  = rr_action(self->scene, "move_right");
    b->act_up     = rr_action(self->scene, "move_up");
    b->act_down   = rr_action(self->scene, "move_down");
    b->act_cancel = rr_action(self->scene, "ui_cancel");
    for (i = 0; i < 3; i++) {
        b->start[i] = self->local.m[9 + i];
    }
    b->distance  = 7.0f;
    b->elevation = 0.45f;
    if (b->camera != NULL) {
        /* Where the camera is in the scene says how far behind and above the ball it
         * stays. The two must have the same parent. */
        const float d[3] = { b->camera->local.m[9] - self->local.m[9], b->camera->local.m[10] - self->local.m[10],
                             b->camera->local.m[11] - self->local.m[11] };
        b->distance  = sqrtf(dot(d, d));
        b->elevation = atan2f(d[1], sqrtf(d[0] * d[0] + d[2] * d[2]));
        b->yaw       = atan2f(d[0], d[2]);
    }
    b->start_yaw = b->yaw;
    b->face      = b->yaw + PI_F;
    b->spin[0] = b->spin[4] = b->spin[8] = 1.0f;
    while ((n = rr_scene_find_tagged(self->scene, "coin", n)) != NULL) {
        b->coin_total++;
    }
    b->lives = game != NULL && game->start_lives > 0 ? game->start_lives : 3;
    if (rr_save_read(SAVE_NAME, &save, sizeof(save)) && save.magic == SAVE_MAGIC) {
        b->best = save.best;
    }
    show_coins(b);
    show_status(b);
}

/* Turns the shell by `angle` around `axis` (unit length) and writes it to the node. */
static void roll_shell(BallRoller *b, const float *axis, float angle)
{
    const float s = sinf(angle), c = cosf(angle);
    float      *x = &b->spin[0], *y = &b->spin[3], *z = &b->spin[6];
    int         i, k;

    for (i = 0; i < 3; i++) {
        float *v = &b->spin[i * 3], side[3];
        const float along = dot(axis, v) * (1.0f - c);
        cross(axis, v, side);
        for (k = 0; k < 3; k++) {
            v[k] = v[k] * c + side[k] * s + axis[k] * along;
        }
    }
    /* Many small turns add up their rounding: make the axes square again. */
    normalize(x);
    {
        const float xy = dot(x, y);
        for (k = 0; k < 3; k++) {
            y[k] -= x[k] * xy;
        }
    }
    normalize(y);
    cross(x, y, z);
    if (b->shell != NULL) {
        memcpy(b->shell->local.m, b->spin, sizeof(b->spin));
    }
}

static void place_camera(BallRoller *b, const float *pos)
{
    static const float up[3] = { 0.0f, 1.0f, 0.0f };
    const float        tilt = b->tilt_degrees * PI_F / 180.0f;
    /* Pushing forward leans the floor away from the viewer, which shows more of its top:
     * the camera goes up. Pushing sideways leans the picture that way. */
    const float elevation = b->elevation + tilt * b->lean[1], roll = tilt * b->lean[0];
    const float forward[3] = { -sinf(b->yaw), 0.0f, -cosf(b->yaw) };
    const float target[3] = { pos[0], pos[1] + 0.6f, pos[2] };
    const float cs = cosf(roll), sn = sinf(roll);
    float       eye[3], x[3], y[3], z[3], *m;
    int         k;

    if (b->camera == NULL) {
        return;
    }
    for (k = 0; k < 3; k++) {
        eye[k] = pos[k] - forward[k] * cosf(elevation) * b->distance + up[k] * sinf(elevation) * b->distance;
        z[k]   = eye[k] - target[k]; /* a camera looks along its -Z */
    }
    normalize(z);
    cross(up, z, x);
    normalize(x);
    cross(z, x, y);
    m = b->camera->local.m;
    for (k = 0; k < 3; k++) {
        m[k]     = x[k] * cs + y[k] * sn;
        m[3 + k] = y[k] * cs - x[k] * sn;
        m[6 + k] = z[k];
        m[9 + k] = eye[k];
    }
}

static void ball_roller_update(Node *self, BallRoller *b, float dt)
{
    float *pos = &self->local.m[9];
    float  v[3], forward[3], right[3], x, z, length, speed, blend;
    int    i;

    if (rr_action_pressed(b->act_cancel)) {
        rr_change_scene(b->menu_scene);
        return;
    }
    rr_rigid_body_get_linear_velocity(self, v);
    if (pos[1] < b->fall_limit) {
        /* Fallen off: back to the last checkpoint with one life less. */
        if (--b->lives <= 0) {
            rr_change_scene(b->menu_scene);
            return;
        }
        for (i = 0; i < 3; i++) {
            pos[i] = b->start[i];
            v[i]   = 0.0f;
        }
        b->yaw     = b->start_yaw;
        b->lean[0] = b->lean[1] = 0.0f;
    }

    x = rr_action_strength(b->act_right) - rr_action_strength(b->act_left);
    z = rr_action_strength(b->act_up) - rr_action_strength(b->act_down);
    length = sqrtf(x * x + z * z);
    if (length > 1.0f) {
        x /= length;
        z /= length;
    }
    if (b->done > 0.0f) {
        /* At the goal the ball comes to rest by itself, and a moment later the level ends. */
        const float keep = 1.0f - 6.0f * dt;
        x = z = 0.0f;
        v[0] *= keep > 0.0f ? keep : 0.0f;
        v[2] *= keep > 0.0f ? keep : 0.0f;
        b->done += dt;
        if (b->done > 2.5f) {
            rr_change_scene(b->menu_scene);
            return;
        }
    } else {
        b->time += dt;
    }
    show_status(b);

    /* The stick pushes along the view: up is away from the camera. */
    forward[0] = -sinf(b->yaw); forward[1] = 0.0f; forward[2] = -cosf(b->yaw);
    right[0]   = cosf(b->yaw);  right[1]   = 0.0f; right[2]   = -sinf(b->yaw);
    v[0] += (right[0] * x + forward[0] * z) * b->push * dt;
    v[2] += (right[2] * x + forward[2] * z) * b->push * dt;
    speed = sqrtf(v[0] * v[0] + v[2] * v[2]);
    if (speed > b->max_speed && speed > 0.0f) {
        v[0] *= b->max_speed / speed;
        v[2] *= b->max_speed / speed;
        speed = b->max_speed;
    }
    rr_rigid_body_set_linear_velocity(self, v);

    blend = 6.0f * dt < 1.0f ? 6.0f * dt : 1.0f;
    b->lean[0] += (x - b->lean[0]) * blend;
    b->lean[1] += (z - b->lean[1]) * blend;

    if (speed > 0.05f) {
        /* A ball that rolls turns around the axis across its way. */
        const float axis[3] = { v[2] / speed, 0.0f, -v[0] / speed };
        roll_shell(b, axis, speed * dt / (b->radius > 0.01f ? b->radius : 0.5f));
    }
    if (speed > 1.0f) {
        /* The camera swings round to look where the ball is going: fast when the ball is,
         * and not at all for a ball that comes straight back at it, so that pulling the
         * stick back brakes without turning the view round. */
        const float along = (v[0] * forward[0] + v[2] * forward[2]) / speed;
        float       rate = 2.5f * (speed < 6.0f ? speed / 6.0f : 1.0f) * 0.5f * (1.0f + along) * dt;
        b->yaw += turn(b->yaw, atan2f(-v[0], -v[2])) * (rate < 1.0f ? rate : 1.0f);
    }
    place_camera(b, pos);

    if (b->rider != NULL) {
        float *m = b->rider->local.m;
        if (speed > 0.5f) {
            b->face += turn(b->face, atan2f(v[0], v[2])) * blend; /* the model looks along +Z */
        }
        m[0] = cosf(b->face); m[1] = 0.0f; m[2] = -sinf(b->face);
        m[3] = 0.0f;          m[4] = 1.0f; m[5] = 0.0f;
        m[6] = sinf(b->face); m[7] = 0.0f; m[8] = cosf(b->face);
    }
    if (b->anim != NULL) {
        /* The rider keeps up with the ball: the faster it rolls, the faster the walk. */
        const char *clip = speed > 0.6f ? "walk" : "idle";
        const char *current = rr_anim_current(b->anim);
        float       pace = speed / 3.0f;
        if (current == NULL || strcmp(current, clip) != 0) {
            rr_anim_play(b->anim, clip);
        }
        pace = pace < 0.6f ? 0.6f : (pace > 4.0f ? 4.0f : pace);
        rr_anim_set_speed_scale(b->anim, speed > 0.6f ? pace : 1.0f);
    }
    if (b->shadow_ray != NULL && b->shadow != NULL) {
        rr_raycast_force_update(b->shadow_ray);
        if (rr_raycast_is_colliding(b->shadow_ray)) {
            float ground[3];
            rr_raycast_get_collision_point(b->shadow_ray, ground);
            b->shadow->local.m[9]  = ground[0];
            b->shadow->local.m[10] = ground[1] + 0.03f; /* just above, so it does not flicker */
            b->shadow->local.m[11] = ground[2];
            b->shadow->flags |= RR_NODE_FLAG_VISIBLE;
        } else {
            b->shadow->flags &= (uint8_t)~RR_NODE_FLAG_VISIBLE;
        }
    }
}

/* Connected to the `area_entered` signal of the ball's hitbox area. */
static void ball_roller_on_area(Node *self, BallRoller *b, Node *area)
{
    if (area == NULL || !(area->flags & RR_NODE_FLAG_VISIBLE)) {
        return;
    }
    if (rr_node_has_tag(area, "coin")) {
        Node *anim = rr_node_child(area, NULL);
        while (anim != NULL && anim->type != RR_NODE_ANIMATION_PLAYER) {
            anim = rr_node_child(area, anim);
        }
        if (anim != NULL) {
            /* The coin's own "collect" clip shows it off and hides it at the end. That
             * clip being the current one also marks the coin as taken. */
            const char *current = rr_anim_current(anim);
            if (current != NULL && strcmp(current, "collect") == 0) {
                return;
            }
            rr_anim_play(anim, "collect");
        } else {
            area->flags &= (uint8_t)~RR_NODE_FLAG_VISIBLE;
        }
        if (b->coin_burst != NULL) {
            b->coin_burst->local.m[9]  = area->world.m[9];
            b->coin_burst->local.m[10] = area->world.m[10] + 0.3f;
            b->coin_burst->local.m[11] = area->world.m[11];
            rr_particles_restart(b->coin_burst);
        }
        b->coins++;
        show_coins(b);
        if (b->coin_sound != NULL) {
            rr_audio_play(b->coin_sound);
        }
    } else if (rr_node_has_tag(area, "checkpoint")) {
        /* From now on a fall leads back here, looking the way the ball came through. */
        b->start[0]  = area->world.m[9];
        b->start[1]  = area->world.m[10] + b->radius + 0.1f;
        b->start[2]  = area->world.m[11];
        b->start_yaw = b->yaw;
    } else if (rr_node_has_tag(area, "goal") && b->done <= 0.0f) {
        if (b->best <= 0.0f || b->time < b->best) {
            const BallSave save = { SAVE_MAGIC, b->time };
            rr_save_write(SAVE_NAME, &save, sizeof(save));
        }
        b->done = 0.001f;
        rr_input_vibrate(0.4f, 0.8f, 0.3f);
    }
    (void)self;
}

COMPONENT_BEGIN(BallRoller, "BallRoller")
    EXPORT_FLOAT(push)
    EXPORT_FLOAT(max_speed)
    EXPORT_FLOAT(tilt_degrees)
    EXPORT_FLOAT(radius)
    EXPORT_FLOAT(fall_limit)
    EXPORT_NODE(shell)
    EXPORT_NODE(rider)
    EXPORT_NODE(anim)
    EXPORT_NODE(camera)
    EXPORT_NODE(coin_label)
    EXPORT_NODE(coin_sound)
    EXPORT_NODE(coin_burst)
    EXPORT_NODE(shadow_ray)
    EXPORT_NODE(shadow)
    EXPORT_NODE(status_label)
    EXPORT_SCENE(menu_scene)
    ON_READY(ball_roller_ready)
    ON_UPDATE(ball_roller_update)
    ON_SIGNAL_NODE("_on_hitbox_area_entered", ball_roller_on_area)
COMPONENT_END()
