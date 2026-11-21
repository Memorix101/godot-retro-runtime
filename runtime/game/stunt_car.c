/* Component of the stunt demo (scenes/stunt_track.tscn).
 * Placeholder script: components/stunt_car.gd.
 *
 * A car after the raycast vehicle that Godot's own VehicleBody3D is (which came from the
 * Bullet physics library): a body on four springs whose tyres each push at the place
 * where they touch the road. The runtime's bodies do not turn, so the body of the car is
 * not one of them: it is a box in picophysics (see below), which moves it, lets it fall
 * and turn, and keeps it out of the rails and the road. The node is a plain node that is
 * stood where the body is. What the wheels and the air do to the car is worked out here
 * and handed to the body as a change of its velocity and its spin.
 *
 * - Four RayCast3D nodes, one per wheel, point out of the car's underside. A wheel whose
 *   ray finds the road within the length of its spring pushes the car away from the road
 *   there, the more the further the spring is pressed in, and a damper calms it. Four
 *   such pushes carry the car, lay it the way the road lies, let it dip and roll, and
 *   tip it over the edge of the road when the wheels of one side reach into the air.
 *   What a spring pushes with is the load on its wheel.
 * - Each tyre pushes against sliding across the way its wheel points, and along that way
 *   with the engine (rear wheels) or the brake (all four), together no more than the load
 *   on the wheel times the grip lets through. The front wheels are turned by the
 *   steering, less the faster the car goes. Nothing says how the car is to turn: it
 *   turns because of where the tyres push. Steered too hard for its speed it runs wide.
 * - The drift is the one thing that is set and not left to the tyres. The brake in a
 *   turn starts it, at speed. While it lasts all four tyres slide and hold only a part
 *   of what they did, so the car's way bends in a wide curve and its speed stays, and
 *   the car stands at an angle to its way that the steering says. It ends when the car
 *   runs straight again.
 * - The air presses a fast car onto the road, which puts load on the wheels: more grip,
 *   and what holds the car in the loop.
 * - Without a wheel on the road the car flies: gravity is all that moves it, it keeps the
 *   spin it took off with, and the air turns its nose slowly into the wind.
 *
 * The camera looks where the car is going, not where its nose points, so a drift is seen
 * from the side.
 *
 * Gates are areas in the groups "checkpoint" and "goal", reported by `area_entered` of
 * the car's hitbox area. The clock starts with the first touch of the gas; the best time
 * is kept in a saved game. A car that falls below `fall_limit`, that lies still without a
 * wheel on the road, or whose driver asks for it (the action "respawn"), is put back at
 * the last checkpoint. The action "leave" goes back to the menu. Both are actions of
 * their own, because on a pad the buttons of jump and ui_cancel are gas and brake. */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "../core/rr_scene.h"

#define MAX_CHECKPOINTS 8
#define WHEELS          4
#define MAX_CRATES      6

/* How hard it is to turn the car around its three axes, for a car of mass 1: pitching
 * (around the axis to its right), turning (around its up) and rolling (around its nose). */
#define INERTIA_PITCH 1.5f
#define INERTIA_YAW   1.5f
#define INERTIA_ROLL  0.7f
/* The part of its sliding across that one tyre takes away per step (Godot's and Bullet's
 * value), and how much of the height of the road below the car's middle counts when a
 * tyre pushes sideways: Godot's wheel_roll_influence, which keeps a car from rolling
 * over in every hard turn. */
#define TYRE_HOLD      0.2f
#define ROLL_INFLUENCE 0.2f
/* How much of the angle between nose and way the front wheels take up by themselves, and
 * how fast a car on its wheels stops turning when nothing turns it, per second. */
#define CASTER   0.6f
#define YAW_CALM 1.2f
/* The drift: the angle between nose and way with the stick right over, radians; how fast
 * the nose goes to the angle that is asked for, per second; and the speed below which a
 * car does not drift, metres per second. */
#define DRIFT_ANGLE 0.6f
#define DRIFT_TURN  4.0f
#define DRIFT_SPEED 14.0f

typedef struct {
    float       engine;       /* exported: metres per second gained per second */
    float       brake;        /* exported */
    float       max_speed;    /* exported: metres per second */
    float       steer;        /* exported: how far the front wheels turn at a crawl, radians */
    float       steer_fast;   /* exported: and at top speed */
    float       grip;         /* exported: what a tyre holds, as a part of the load on its wheel */
    float       drift;        /* exported: the part of that which the rear tyres keep in a drift */
    float       suspension;   /* exported: length of a spring that carries nothing, metres */
    float       spring;       /* exported: push of one spring per metre it is pressed in, m/s2 */
    float       damping;      /* exported: what one damper takes per m/s, 1/s */
    float       downforce;    /* exported: press of the air per (m/s) squared, 1/m */
    float       gravity;      /* exported: what the car falls with, m/s2 */
    float       fall_limit;   /* exported: below this height the car is put back */
    float       crate_size;   /* exported: edge of the crates in the group "crate", metres */
    Node       *model;        /* exported: shows more of an angle in a slide */
    Node       *wheel_fl;     /* exported: the four RayCast3D nodes, where the wheels are */
    Node       *wheel_fr;     /* exported */
    Node       *wheel_rl;     /* exported */
    Node       *wheel_rr;     /* exported */
    Node       *camera;       /* exported */
    Node       *engine_sound; /* exported: its pitch follows the speed */
    Node       *speed_label;  /* exported */
    Node       *time_label;   /* exported */
    Node       *info_label;   /* exported: checkpoints, and the result at the finish */
    const char *menu_scene;   /* exported */

    int   act_gas, act_brake, act_left, act_right, act_reset, act_cancel;
    float forward[3], up[3];       /* the way the nose points and what is up for the car, unit length */
    float spin[3];                 /* how the car turns: axis times radians per second */
    void *body;                    /* the car's body in picophysics (a PPBody) */
    Node *crates[MAX_CRATES];      /* the nodes in the group "crate", and their bodies */
    void *crate_bodies[MAX_CRATES];
    int   crate_count;
    float speed;                   /* along the nose, metres per second */
    float steering;                /* the stick, smoothed: -1 left .. 1 right */
    float slip;                    /* how far the tail slides out: the angle of the rear wheels to their way, radians */
    bool  drifting;
    float loose;                   /* how far the rear tyres have let go, 0..1 */
    int   on_road;                 /* wheels that carry the car */
    float stuck;                   /* seconds it has been lying still without a wheel on the road */
    float model_scale;
    float cam_forward[3], cam_up[3]; /* the directions of the camera, which follow more slowly */
    float cam_back, cam_height;    /* where the camera is, in those directions */
    float home[3], home_forward[3], home_up[3]; /* where the car is put back */
    Node *passed[MAX_CHECKPOINTS]; /* the checkpoints gone through, each once */
    int   passed_count, checkpoint_total;
    bool  running;                 /* the clock runs */
    float time, best, done;        /* seconds; done counts from the finish */
    char  speed_text[24], time_text[48], info_text[48];
} StuntCar;

typedef struct {
    uint32_t magic;
    float    best;
} StuntSave;

#define SAVE_NAME  "RR_STUNT"
#define SAVE_MAGIC 0x53544E31u

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

/* Moves `v` a part of the way to `to` and brings it back to unit length. */
static void lean(float *v, const float *to, float part)
{
    int k;
    for (k = 0; k < 3; k++) {
        v[k] += (to[k] - v[k]) * part;
    }
    normalize(v);
}

/* Takes out of `v` what points along `axis` and brings it back to unit length. */
static void square(float *v, const float *axis)
{
    const float along = dot(v, axis);
    int         k;
    for (k = 0; k < 3; k++) {
        v[k] -= axis[k] * along;
    }
    normalize(v);
}

/* A push on the car at a place that is `arm` away from its middle: it changes the
 * velocity, and the spin around each of the car's own axes by how easy that axis turns.
 * `push` is a change of velocity for a car of mass 1. */
static void kick(StuntCar *c, float *v, const float *right, const float *arm, const float *push)
{
    float twist[3], pitch, yaw, roll;
    int   k;

    cross(arm, push, twist);
    pitch = dot(twist, right) / INERTIA_PITCH;
    yaw   = dot(twist, c->up) / INERTIA_YAW;
    roll  = dot(twist, c->forward) / INERTIA_ROLL;
    for (k = 0; k < 3; k++) {
        v[k]       += push[k];
        c->spin[k] += right[k] * pitch + c->up[k] * yaw + c->forward[k] * roll;
    }
}

/* --- the body of the car: picophysics --------------------------------------------------
 * The runtime's bodies do not turn, so the car's body is one of picophysics', a small
 * physics engine in one header (runtime/third_party/picophysics): a box that falls, turns,
 * and is kept out of a world of triangles, with friction where it touches. The triangles
 * are made here from the box shapes of the scene's static bodies in the group "track",
 * the same boxes the wheels' rays of the runtime are cast at. picophysics looks at every
 * triangle it has for every body, so it is only ever given the ones near the car.
 *
 * The header is compiled here, as its one implementation, with the warnings off that this
 * project asks for and it does not heed. */
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdouble-promotion"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wstrict-prototypes"
#pragma GCC diagnostic ignored "-Wold-style-definition"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif
#ifndef M_PI
#define M_PI 3.14159265358979323846 /* plain C11 has none */
#endif
#define PICOPHYSICS_MAX_OBJECTS   8
#define PICOPHYSICS_MAX_TRIANGLES 256
#define PICOPHYSICS_IMPLEMENTATION
#include "../third_party/picophysics/picophysics.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

/* Every triangle of the track, with the box around it. */
#define MAX_TRACK_TRIANGLES 3072
/* Triangles within this many metres of the car are handed to picophysics, and they are
 * picked again when the car has moved this far from where they were picked. */
#define NEAR_REACH 6.5f
#define NEAR_STALE 2.0f
/* The box that is the car's body, metres. It sits around the node, well above the road. */
#define BODY_WIDTH  1.5f
#define BODY_HEIGHT 0.5f
#define BODY_LENGTH 2.8f
/* A crate weighs this, where the car weighs 1. */
#define CRATE_MASS 0.12f

typedef struct {
    float v[3][3];
    float lo[3], hi[3];
} TrackTriangle;

static TrackTriangle g_track[MAX_TRACK_TRIANGLES];
static int           g_track_count;
static bool          g_track_full; /* the warning about that was printed */
static float         g_near_at[1 + MAX_CRATES][3];
static int           g_near_count;
static bool          g_near_valid;

/* One side of a box as two triangles that face `out`. picophysics' triangles stop only
 * what comes from the side their corners go round counter-clockwise. */
static void track_side(const float *a, const float *b, const float *c, const float *d, const float *out)
{
    const float *quad[4] = { a, b, c, d };
    float        e1[3], e2[3], n[3];
    int          t, i, k;

    for (k = 0; k < 3; k++) {
        e1[k] = b[k] - a[k];
        e2[k] = c[k] - a[k];
    }
    n[0] = e1[1] * e2[2] - e1[2] * e2[1];
    n[1] = e1[2] * e2[0] - e1[0] * e2[2];
    n[2] = e1[0] * e2[1] - e1[1] * e2[0];
    if (n[0] * out[0] + n[1] * out[1] + n[2] * out[2] < 0.0f) {
        quad[1] = d;
        quad[3] = b;
    }
    for (t = 0; t < 2; t++) {
        TrackTriangle *tri;
        const float   *corner[3];
        if (g_track_count >= MAX_TRACK_TRIANGLES) {
            if (!g_track_full) {
                printf("stunt: more than %d triangles of track, the rest is not solid for the car's body\n",
                       MAX_TRACK_TRIANGLES);
                g_track_full = true;
            }
            return;
        }
        tri       = &g_track[g_track_count++];
        corner[0] = quad[0];
        corner[1] = quad[1 + t];
        corner[2] = quad[2 + t];
        for (i = 0; i < 3; i++) {
            for (k = 0; k < 3; k++) {
                tri->v[i][k] = corner[i][k];
                tri->lo[k]   = i == 0 || corner[i][k] < tri->lo[k] ? corner[i][k] : tri->lo[k];
                tri->hi[k]   = i == 0 || corner[i][k] > tri->hi[k] ? corner[i][k] : tri->hi[k];
            }
        }
    }
}

/* The sides of one box shape that the car's body can meet: the top of a piece of road,
 * and of anything that stands on the road (a rail, a wall) the top and the four sides. */
static void track_box(const Node *shape)
{
    const RRPkgShapeNode *data = (const RRPkgShapeNode *)shape->data;
    const float          *m = shape->world.m;
    const bool            floor = strncmp(shape->name, "Floor", 5) == 0;
    float                 corner[8][3], out[3];
    int                   i, k;

    if (data->kind != RR_SHAPE_BOX || data->disabled) {
        return;
    }
    /* Corner i: bit 0 is +X, bit 1 is +Y, bit 2 is +Z of the box. */
    for (i = 0; i < 8; i++) {
        for (k = 0; k < 3; k++) {
            corner[i][k] = m[9 + k] + m[k] * ((i & 1) ? data->size[0] : -data->size[0]) +
                           m[3 + k] * ((i & 2) ? data->size[1] : -data->size[1]) +
                           m[6 + k] * ((i & 4) ? data->size[2] : -data->size[2]);
        }
    }
    for (k = 0; k < 3; k++) {
        out[k] = m[3 + k];
    }
    track_side(corner[2], corner[3], corner[7], corner[6], out); /* top */
    if (floor) {
        return;
    }
    for (k = 0; k < 3; k++) {
        out[k] = m[k];
    }
    track_side(corner[1], corner[3], corner[7], corner[5], out); /* +X */
    for (k = 0; k < 3; k++) {
        out[k] = -m[k];
    }
    track_side(corner[0], corner[2], corner[6], corner[4], out); /* -X */
    if (strncmp(shape->name, "Rail", 4) == 0) {
        return; /* the ends of a rail come in from the side: nothing meets them */
    }
    for (k = 0; k < 3; k++) {
        out[k] = m[6 + k];
    }
    track_side(corner[4], corner[5], corner[7], corner[6], out); /* +Z */
    for (k = 0; k < 3; k++) {
        out[k] = -m[6 + k];
    }
    track_side(corner[0], corner[1], corner[3], corner[2], out); /* -Z */
}

/* Makes the triangles of everything in the group "track". */
static void track_collect(RRScene *scene)
{
    Node *body = NULL, *shape;

    g_track_count = 0;
    g_track_full  = false;
    g_near_valid  = false;
    while ((body = rr_scene_find_tagged(scene, "track", body)) != NULL) {
        for (shape = rr_node_child(body, NULL); shape != NULL; shape = rr_node_child(body, shape)) {
            if (shape->type == RR_NODE_SHAPE) {
                track_box(shape);
            }
        }
    }
}

/* Hands picophysics the triangles near any of `count` places (the bodies), if the ones
 * it has were picked for places too far from these. */
static void track_near(float (*at)[3], int count)
{
    int  i, k, b;
    bool stale = !g_near_valid || count != g_near_count;

    for (b = 0; b < count && !stale; b++) {
        const float d[3] = { at[b][0] - g_near_at[b][0], at[b][1] - g_near_at[b][1], at[b][2] - g_near_at[b][2] };
        stale = d[0] * d[0] + d[1] * d[1] + d[2] * d[2] > NEAR_STALE * NEAR_STALE;
    }
    if (!stale) {
        return;
    }
    tri_count = 0; /* picophysics has no call that drops its triangles and keeps its bodies */
    for (i = 0; i < g_track_count && tri_count < PICOPHYSICS_MAX_TRIANGLES; i++) {
        const TrackTriangle *t = &g_track[i];
        PPVec3               v[3];
        PPTriangle          *made;
        bool                 near = false;
        for (b = 0; b < count && !near; b++) {
            near = true;
            for (k = 0; k < 3; k++) {
                near = near && t->lo[k] <= at[b][k] + NEAR_REACH && t->hi[k] >= at[b][k] - NEAR_REACH;
            }
        }
        if (!near) {
            continue;
        }
        for (k = 0; k < 3; k++) {
            pp_vec3_set(&v[k], t->v[k][0], t->v[k][1], t->v[k][2]);
        }
        made = pp_physics_create_triangle(&v[0], &v[1], &v[2], 2);
        /* picophysics rubs with the product of the two frictions: this times the car's
         * is next to nothing, this times a crate's is a crate that stays where it is put. */
        made->friction = 0.5f;
        made->bounce   = 0.0f;
    }
    for (b = 0; b < count; b++) {
        for (k = 0; k < 3; k++) {
            g_near_at[b][k] = at[b][k];
        }
    }
    g_near_count = count;
    g_near_valid = true;
}

/* Seconds since the body last met something that is not the road under it: a rail or a
 * wall. picophysics tells of every contact; the ones that push along the car's up are the
 * road (or the roof on it) and do not count. */
static float g_hit_ago = 100.0f;
static float g_body_up[3];

static bool body_contact(const void *lhs, const void *rhs, BodyKind k0, BodyKind k1, const PPCollision *hit,
                         const void *user)
{
    const float along = hit->n.xyz[0] * g_body_up[0] + hit->n.xyz[1] * g_body_up[1] + hit->n.xyz[2] * g_body_up[2];
    (void)lhs;
    (void)rhs;
    (void)k0;
    (void)k1;
    (void)user;
    if (along < 0.7f && along > -0.7f) {
        g_hit_ago = 0.0f;
    }
    return true; /* and picophysics is to stop the body there */
}

/* The rotation that stands a body with its X along `right`, its Y along `up` and its Z
 * against `forward`. */
static void quat_of(const float *right, const float *up, const float *forward, PPQuaternion *q)
{
    const float m00 = right[0], m11 = up[1], m22 = -forward[2];
    const float trace = m00 + m11 + m22;
    /* Columns of the matrix are right, up, back: m[row][column]. */
    const float m01 = up[0], m02 = -forward[0], m10 = right[1], m12 = -forward[1], m20 = right[2], m21 = up[2];

    if (trace > 0.0f) {
        const float s = sqrtf(trace + 1.0f) * 2.0f;
        q->w = 0.25f * s;
        q->x = (m21 - m12) / s;
        q->y = (m02 - m20) / s;
        q->z = (m10 - m01) / s;
    } else if (m00 > m11 && m00 > m22) {
        const float s = sqrtf(1.0f + m00 - m11 - m22) * 2.0f;
        q->w = (m21 - m12) / s;
        q->x = 0.25f * s;
        q->y = (m01 + m10) / s;
        q->z = (m02 + m20) / s;
    } else if (m11 > m22) {
        const float s = sqrtf(1.0f + m11 - m00 - m22) * 2.0f;
        q->w = (m02 - m20) / s;
        q->x = (m01 + m10) / s;
        q->y = 0.25f * s;
        q->z = (m12 + m21) / s;
    } else {
        const float s = sqrtf(1.0f + m22 - m00 - m11) * 2.0f;
        q->w = (m10 - m01) / s;
        q->x = (m02 + m20) / s;
        q->y = (m12 + m21) / s;
        q->z = 0.25f * s;
    }
}

static void show_time(StuntCar *c)
{
    if (c->time_label != NULL) {
        const int now = (int)(c->time * 100.0f), best = (int)(c->best * 100.0f);
        if (best > 0) {
            snprintf(c->time_text, sizeof(c->time_text), "TIME %d.%02d  BEST %d.%02d", now / 100, now % 100,
                     best / 100, best % 100);
        } else {
            snprintf(c->time_text, sizeof(c->time_text), "TIME %d.%02d", now / 100, now % 100);
        }
        rr_node_set_text(c->time_label, c->time_text);
    }
}

static void show_info(StuntCar *c)
{
    if (c->info_label != NULL) {
        if (c->done > 0.0f) {
            snprintf(c->info_text, sizeof(c->info_text), "FINISH!");
        } else if (!c->running) {
            snprintf(c->info_text, sizeof(c->info_text), "GAS TO START");
        } else {
            snprintf(c->info_text, sizeof(c->info_text), "CHECKPOINT %d/%d", c->passed_count, c->checkpoint_total);
        }
        rr_node_set_text(c->info_label, c->info_text);
    }
}

static void stunt_car_ready(Node *self, StuntCar *c)
{
    const float *m = self->local.m;
    StuntSave    save;
    Node        *n = NULL;
    int          k;

    c->act_gas    = rr_action(self->scene, "accelerate");
    c->act_brake  = rr_action(self->scene, "brake");
    c->act_left   = rr_action(self->scene, "steer_left");
    c->act_right  = rr_action(self->scene, "steer_right");
    c->act_reset  = rr_action(self->scene, "respawn");
    c->act_cancel = rr_action(self->scene, "leave");
    /* The car starts the way its node stands in the scene: -Z of the node is forward. */
    for (k = 0; k < 3; k++) {
        c->forward[k] = c->cam_forward[k] = c->home_forward[k] = -m[6 + k];
        c->up[k] = c->cam_up[k] = c->home_up[k] = m[3 + k];
        c->home[k] = m[9 + k];
    }
    c->model_scale = 1.0f;
    if (c->model != NULL) {
        c->model_scale = sqrtf(dot(c->model->local.m, c->model->local.m));
    }
    c->cam_back   = 6.5f;
    c->cam_height = 2.6f;
    if (c->camera != NULL) {
        /* Where the camera is in the scene says how far behind and above the car it stays.
         * The two must have the same parent. */
        const float d[3] = { c->camera->local.m[9] - m[9], c->camera->local.m[10] - m[10],
                             c->camera->local.m[11] - m[11] };
        c->cam_back   = -dot(d, c->forward);
        c->cam_height = dot(d, c->up);
    }
    while ((n = rr_scene_find_tagged(self->scene, "checkpoint", n)) != NULL) {
        c->checkpoint_total++;
    }
    if (rr_save_read(SAVE_NAME, &save, sizeof(save)) && save.magic == SAVE_MAGIC) {
        c->best = save.best;
    }
    {
        /* The world of picophysics is one for the whole program: it is emptied and made
         * again for this scene. The body is a box about as large as the car, of mass 1, so
         * that a push on it is a change of its velocity. */
        PPVec3  at, down;
        PPBody *body;
        float   right[3];

        pp_physics_clear();
        pp_physics_collision_map_add(1, 2, NULL, body_contact); /* the body is kind 1, the track kind 2 */
        g_hit_ago = 100.0f;
        track_collect(self->scene);
        pp_vec3_set(&down, 0.0f, -c->gravity, 0.0f);
        pp_physics_set_gravity(&down);
        pp_vec3_set(&at, m[9], m[10], m[11]);
        body = pp_physics_create_box(BODY_WIDTH, BODY_HEIGHT, BODY_LENGTH, &at, 1.0f, 1);
        pp_body_set_bounce(body, 0.0f);
        pp_body_set_friction(body, 0.012f);
        pp_body_set_damping(body, 0.1f);
        pp_body_set_angular_damping(body, 0.05f);
        cross(c->forward, c->up, right);
        quat_of(right, c->up, c->forward, &body->rot);
        c->body = body;

        /* Crates: every node in the group "crate" gets a body of its own, a light box
         * that the car can ram and that falls, tumbles and stacks by picophysics alone.
         * The node is stood where its body is; it must not have a parent that moves. */
        n              = NULL;
        c->crate_count = 0;
        while (c->crate_count < MAX_CRATES && (n = rr_scene_find_tagged(self->scene, "crate", n)) != NULL) {
            const float *w = n->world.m;
            float        x[3], y[3], f[3];
            PPBody      *crate;
            for (k = 0; k < 3; k++) {
                x[k] = w[k];
                y[k] = w[3 + k];
                f[k] = -w[6 + k];
            }
            pp_vec3_set(&at, w[9], w[10], w[11]);
            crate = pp_physics_create_box(c->crate_size, c->crate_size, c->crate_size, &at, CRATE_MASS, 3);
            pp_body_set_bounce(crate, 0.1f);
            pp_body_set_friction(crate, 1.0f);
            pp_body_set_damping(crate, 0.2f);
            pp_body_set_angular_damping(crate, 0.6f);
            quat_of(x, y, f, &crate->rot);
            c->crates[c->crate_count]       = n;
            c->crate_bodies[c->crate_count] = crate;
            c->crate_count++;
        }
    }
    show_time(c);
    show_info(c);
}

/* In a slide the model shows more of an angle than the car really has: a drift should
 * look like one. Its own scale is kept. */
static void pose_model(StuntCar *c)
{
    if (c->model != NULL) {
        const float yaw = 0.6f * c->slip, s = sinf(yaw) * c->model_scale, k = cosf(yaw) * c->model_scale;
        float      *m = c->model->local.m;
        m[0] = k;    m[1] = 0.0f;           m[2] = -s;
        m[3] = 0.0f; m[4] = c->model_scale; m[5] = 0.0f;
        m[6] = s;    m[7] = 0.0f;           m[8] = k;
    }
}

static void place_camera(StuntCar *c, const float *pos, const float *v, float dt)
{
    const float moving = sqrtf(dot(v, v));
    const float part = 5.0f * dt < 1.0f ? 5.0f * dt : 1.0f;
    float       look[3], eye[3], x[3], y[3], z[3], *m;
    int         k;

    if (c->camera == NULL) {
        return;
    }
    /* The camera looks where the car is going; a car that stands or reverses is looked
     * at from behind. What is up for the camera is what is up for the car as long as a
     * wheel is on the road: it goes through the loop with it, but does not tumble with a
     * car that falls. */
    for (k = 0; k < 3; k++) {
        look[k] = moving > 4.0f && c->speed > 0.0f ? v[k] / moving : c->forward[k];
    }
    lean(c->cam_forward, look, part);
    if (c->on_road > 0) {
        lean(c->cam_up, c->up, part * 1.4f < 1.0f ? part * 1.4f : 1.0f);
    }
    square(c->cam_up, c->cam_forward);
    for (k = 0; k < 3; k++) {
        eye[k] = pos[k] - c->cam_forward[k] * c->cam_back + c->cam_up[k] * c->cam_height;
        z[k]   = eye[k] - (pos[k] + c->cam_up[k] * 1.0f); /* a camera looks along its -Z */
    }
    normalize(z);
    cross(c->cam_up, z, x);
    normalize(x);
    cross(z, x, y);
    m = c->camera->local.m;
    for (k = 0; k < 3; k++) {
        m[k]     = x[k];
        m[3 + k] = y[k];
        m[6 + k] = z[k];
        m[9 + k] = eye[k];
    }
}

/* Takes from the body where the car is, how it stands, how it moves and how it turns, and
 * stands the node the same way: X to its right, Y up, -Z forward. */
static void body_read(StuntCar *c, Node *self, float *v, float *right)
{
    PPBody *body = (PPBody *)c->body;
    PPVec3  x, y, z;
    int     k;

    pp_body_get_right(body, &x);
    pp_body_get_up(body, &y);
    pp_body_get_forward(body, &z); /* what picophysics calls forward is the body's +Z, the car's back */
    for (k = 0; k < 3; k++) {
        right[k]             = x.xyz[k];
        c->up[k]             = y.xyz[k];
        c->forward[k]        = -z.xyz[k];
        v[k]                 = body->vel.xyz[k];
        c->spin[k]           = body->a_vel.xyz[k];
        self->local.m[k]     = x.xyz[k];
        self->local.m[3 + k] = y.xyz[k];
        self->local.m[6 + k] = z.xyz[k];
        self->local.m[9 + k] = body->pos.xyz[k];
    }
}

/* Puts the car back at the last checkpoint, standing still. The clock runs on. */
static void put_back(StuntCar *c)
{
    PPBody *body = (PPBody *)c->body;
    float   right[3];
    int     k;

    cross(c->home_forward, c->home_up, right);
    quat_of(right, c->home_up, c->home_forward, &body->rot);
    pp_vec3_set(&body->pos, c->home[0], c->home[1], c->home[2]);
    pp_vec3_init(&body->vel);
    pp_vec3_init(&body->a_vel);
    for (k = 0; k < 3; k++) {
        c->cam_forward[k] = c->home_forward[k];
        c->cam_up[k]      = c->home_up[k];
    }
    c->speed = c->slip = c->steering = c->stuck = c->loose = 0.0f;
    c->drifting  = false;
    g_near_valid = false;
}

static void stunt_car_update(Node *self, StuntCar *c, float frame_dt)
{
    /* A long frame must not blow up the springs. */
    const float dt = frame_dt < 1.0f / 30.0f ? frame_dt : 1.0f / 30.0f;
    PPBody     *body = (PPBody *)c->body;
    Node       *wheels[WHEELS];
    float      *pos = &self->local.m[9];
    float       v[3], right[3], gas, braking, steer_to, blend;
    /* The wheels that carry the car: where each touches the road, seen from the world and
     * from the middle of the car, what is up for the road there, and its load. */
    float       point[WHEELS][3], arm[WHEELS][3], normal[WHEELS][3], load[WHEELS];
    bool        on[WHEELS];
    int         i, k;

    if (rr_action_pressed(c->act_cancel)) {
        rr_change_scene(c->menu_scene);
        return;
    }
    if (body == NULL) {
        return;
    }
    if (pos[1] < c->fall_limit || c->stuck > 1.3f || (rr_action_pressed(c->act_reset) && c->done <= 0.0f)) {
        put_back(c);
    }
    body_read(c, self, v, right);

    gas      = rr_action_strength(c->act_gas);
    braking  = rr_action_strength(c->act_brake);
    steer_to = rr_action_strength(c->act_right) - rr_action_strength(c->act_left);
    if (c->done > 0.0f) {
        /* Past the finish the car brakes by itself, and a moment later the level ends. */
        gas      = 0.0f;
        braking  = 1.0f;
        steer_to = 0.0f;
        c->done += frame_dt;
        if (c->done > 3.0f) {
            rr_change_scene(c->menu_scene);
            return;
        }
    } else if (c->running) {
        c->time += frame_dt;
    } else if (gas > 0.0f) {
        c->running = true;
        show_info(c);
    }
    show_time(c);
    /* The wheels do not jump to where the stick is. */
    blend = 7.0f * dt < 1.0f ? 7.0f * dt : 1.0f;
    c->steering += (steer_to - c->steering) * blend;

    cross(c->forward, c->up, right);

    /* The wheels, after the raycast vehicle that Godot's VehicleBody3D is too. Each wheel
     * is where its ray node is below the car, in the car's own directions: X to the right,
     * Y up, Z to the back. First the springs: a wheel whose ray finds the road within the
     * length of its spring pushes the car away from the road, at the place where it
     * touches. What it pushes with is its load, which says below how much its tyre holds. */
    wheels[0] = c->wheel_fl;
    wheels[1] = c->wheel_fr;
    wheels[2] = c->wheel_rl;
    wheels[3] = c->wheel_rr;
    c->on_road = 0;
    for (i = 0; i < WHEELS; i++) {
        float mount[3], at[3], length, pressed, closing, force;

        on[i]   = false;
        load[i] = 0.0f;
        if (wheels[i] == NULL) {
            continue;
        }
        rr_raycast_force_update(wheels[i]);
        if (!rr_raycast_is_colliding(wheels[i])) {
            continue;
        }
        rr_raycast_get_collision_normal(wheels[i], normal[i]);
        rr_raycast_get_collision_point(wheels[i], point[i]);
        if (dot(normal[i], c->up) < 0.3f) {
            continue; /* the side of a rail is no road */
        }
        for (k = 0; k < 3; k++) {
            mount[k]  = right[k] * wheels[i]->local.m[9] + c->up[k] * wheels[i]->local.m[10] -
                        c->forward[k] * wheels[i]->local.m[11];
            at[k]     = pos[k] + mount[k] - point[i][k];
            arm[i][k] = point[i][k] - pos[k];
        }
        length  = dot(at, c->up);
        pressed = c->suspension - length;
        if (pressed <= 0.0f) {
            continue; /* the wheel hangs in the air */
        }
        if (pressed > 0.55f) {
            continue; /* no road but something too high to roll onto, like a rail: the body stops at that */
        }
        on[i] = true;
        c->on_road++;
        if (pressed > 0.25f) {
            pressed += 4.0f * (pressed - 0.25f); /* the stop at the end of the spring */
        }
        /* How fast this corner of the car comes towards the road. */
        cross(c->spin, mount, at);
        closing = (v[0] + at[0]) * normal[i][0] + (v[1] + at[1]) * normal[i][1] + (v[2] + at[2]) * normal[i][2];
        force   = c->spring * pressed - c->damping * closing;
        if (force <= 0.0f) {
            continue; /* a spring pushes, it does not pull */
        }
        if (force > 150.0f) {
            force = 150.0f; /* a hard landing must not throw the car */
        }
        load[i] = force;
        for (k = 0; k < 3; k++) {
            at[k] = normal[i][k] * force * dt;
        }
        kick(c, v, right, mount, at);
    }

    if (c->on_road > 0) {
        /* The tyres. Nothing here says how the car is to turn: every tyre pushes at the
         * place where it touches the road, and the car turns because the front ones are
         * turned. Sideways a tyre pushes against sliding across its own direction;
         * along it, the rear ones push with the engine and all four with the brake.
         * Together that is no more than the load on the wheel times the grip of the tyre
         * lets through: asked for more, the tyre slides and passes on what it can. So a
         * car that is steered too hard for its speed runs wide, one that is braked hard
         * in a turn slides, and one that is pressed down by the air holds more. */
        const float along = dot(v, c->forward);
        const float fast = fabsf(along) < c->max_speed ? fabsf(along) / c->max_speed : 1.0f;
        /* Where the car is going, as an angle to where its nose points: right is positive. */
        const float going = along > 5.0f ? atan2f(dot(v, right), along) : 0.0f;
        float       side_push[WHEELS], along_push[WHEELS], axle[WHEELS][3], roll[WHEELS][3];
        float       lift, lock, before[3];

        /* The front wheels turn less the faster the car goes. And like the wheels of a real
         * car, which want to roll the way the car moves, they turn a part of the way
         * towards where the car is going by themselves: in a slide that is steering
         * against it, which is what lets a drift be held instead of ending in a spin. */
        lock = c->steering * (c->steer + (c->steer_fast - c->steer) * fast) + CASTER * going;
        lock = lock > c->steer ? c->steer : (lock < -c->steer ? -c->steer : lock);

        for (i = 0; i < WHEELS; i++) {
            const bool front = i < 2;
            float      wheel_way[3], at[3], there[3], limit, across, wish;

            side_push[i] = along_push[i] = 0.0f;
            if (!on[i]) {
                continue;
            }
            for (k = 0; k < 3; k++) {
                wheel_way[k] = front ? c->forward[k] * cosf(lock) + right[k] * sinf(lock) : c->forward[k];
            }
            cross(wheel_way, normal[i], axle[i]);
            normalize(axle[i]);
            cross(normal[i], axle[i], roll[i]); /* the way the wheel rolls, in the road */
            cross(c->spin, arm[i], at);
            for (k = 0; k < 3; k++) {
                there[k] = v[k] + at[k]; /* how the car moves where this wheel is */
            }
            across       = dot(there, axle[i]);
            side_push[i] = -TYRE_HOLD * across;

            if (braking > 0.0f && (along > 0.5f || c->done > 0.0f)) {
                /* The brake stops the wheel's rolling, as hard as it is pressed, the rear
                 * wheels a little harder than the front ones: braking into a turn makes
                 * the tail light, the way one starts a drift. */
                wish          = braking * c->brake * (front ? 0.2f : 0.3f) * dt;
                along_push[i] = -dot(there, roll[i]);
                along_push[i] = along_push[i] > wish ? wish : (along_push[i] < -wish ? -wish : along_push[i]);
            } else if (!front && gas > 0.0f && along < c->max_speed) {
                along_push[i] = gas * c->engine / 2.0f * dt;
            } else if (!front && braking > 0.0f && along > -8.0f) {
                along_push[i] = -braking * c->engine * 0.5f / 2.0f * dt; /* standing, the brake is the reverse gear */
            }

            /* What the tyre lets through. In a drift all four tyres slide and hold only the
             * part `drift`, so the car goes on in a wide curve instead of turning on the
             * spot. And sliding does not take the engine away there: what the tyre pushes
             * sideways is cut down to what it holds, what it pushes along is kept. */
            limit = load[i] * dt * c->grip * (1.0f - (1.0f - c->drift) * c->loose);
            if (c->loose > 0.5f) {
                wish          = load[i] * dt * c->grip;
                side_push[i]  = side_push[i] > limit ? limit : (side_push[i] < -limit ? -limit : side_push[i]);
                along_push[i] = along_push[i] > wish ? wish : (along_push[i] < -wish ? -wish : along_push[i]);
            } else {
                wish = sqrtf(0.25f * along_push[i] * along_push[i] + side_push[i] * side_push[i]);
                if (wish > limit && wish > 0.0f) {
                    along_push[i] *= limit / wish;
                    side_push[i]  *= limit / wish;
                }
            }
        }
        memcpy(before, v, sizeof(before));
        for (i = 0; i < WHEELS; i++) {
            float push[3], lever[3];
            if (!on[i]) {
                continue;
            }
            /* The tyres push nearly at the height of the car's middle, or every hard turn
             * would lay the car on its side and every hard braking stand it on its nose. */
            lift = dot(arm[i], c->up) * (1.0f - ROLL_INFLUENCE);
            for (k = 0; k < 3; k++) {
                push[k]  = roll[i][k] * along_push[i] + axle[i][k] * side_push[i];
                lever[k] = arm[i][k] - c->up[k] * lift;
            }
            kick(c, v, right, lever, push);
        }
        if (g_hit_ago < 0.4f && along > 5.0f) {
            /* Right after it met a rail the nose comes round to where the car is going,
             * so that it runs along the rail and does not go on pushing into it. */
            lift = (-9.0f * going - dot(c->spin, c->up)) * (12.0f * dt < 1.0f ? 12.0f * dt : 1.0f);
            for (k = 0; k < 3; k++) {
                c->spin[k] += c->up[k] * lift;
            }
        }
        /* A car on its wheels does not go on turning by itself for long. */
        lift = dot(c->spin, c->up) * (YAW_CALM * dt < 1.0f ? YAW_CALM * dt : 1.0f);
        for (k = 0; k < 3; k++) {
            c->spin[k] -= c->up[k] * lift;
        }

        /* The drift: a slide at speed that is held, not a turn on the spot. The brake in a
         * turn starts it, above a speed below which a car only turns. While it lasts the
         * tyres slide (above), so the car's way bends in a wide curve and its speed stays,
         * and the car stands at an angle to its way that the steering says: the further
         * the stick is over, the further the nose points into the turn; the stick let go
         * brings the nose back to the way, and steering the other way throws the tail
         * round to the other side. For that the nose is turned as fast as the way bends,
         * plus what is missing to the angle that is asked for. When the car runs
         * straight again the tyres catch. */
        c->slip = going;
        lift = sqrtf(dot(v, v)); /* the speed over the road, whichever way the nose points */
        if (along > 0.0f &&
            ((lift > DRIFT_SPEED && braking > 0.0f && fabsf(c->steering) > 0.3f) ||
             (lift > DRIFT_SPEED * 0.6f && c->loose > 0.5f && (fabsf(c->steering) > 0.25f || fabsf(going) > 0.12f)))) {
            c->loose += (1.0f - c->loose) * (8.0f * dt < 1.0f ? 8.0f * dt : 1.0f);
        } else {
            c->loose -= c->loose * (4.0f * dt < 1.0f ? 4.0f * dt : 1.0f);
        }
        c->drifting = c->loose > 0.5f;
        if (c->loose > 0.05f) {
            /* How fast the way bends: the angle between the velocity before the tyres
             * pushed and after, around what is up for the car. Left is positive. */
            float bend[3], asked, turning, part;
            cross(before, v, bend);
            lift    = sqrtf(dot(before, before) * dot(v, v));
            asked   = lift > 1.0f ? dot(bend, c->up) / lift / dt : 0.0f;
            asked  += DRIFT_TURN * (-c->steering * DRIFT_ANGLE - going);
            turning = dot(c->spin, c->up);
            part    = 10.0f * c->loose * dt;
            part    = part < 1.0f ? part : 1.0f;
            for (k = 0; k < 3; k++) {
                c->spin[k] += c->up[k] * (asked - turning) * part;
            }
        }

        /* The air takes a little speed all the time, and presses a fast car onto the road. */
        lift = fabsf(along) < c->max_speed * 1.2f ? along : c->max_speed * 1.2f;
        for (k = 0; k < 3; k++) {
            v[k] -= c->forward[k] * along * 0.12f * dt + c->up[k] * c->downforce * lift * lift * dt;
        }
        c->speed = dot(v, c->forward);
    } else {
        /* In the air: the car keeps its spin, and the air turns its nose into the wind.
         * A car that lies on its side or its roof is not in the air: there the body rubs on
         * the road, which stops its sliding and its rolling. */
        const float flying = sqrtf(dot(v, v));
        if (flying > 5.0f) {
            const float way[3] = { v[0] / flying, v[1] / flying, v[2] / flying };
            float       wind[3];
            cross(c->forward, way, wind);
            for (k = 0; k < 3; k++) {
                c->spin[k] += wind[k] * 0.055f * flying * dt;
            }
        }
        for (k = 0; k < 3; k++) {
            c->spin[k] -= c->spin[k] * 0.4f * dt;
        }
        c->speed    = dot(v, c->forward);
        c->slip    -= c->slip * blend;
        c->loose   -= c->loose * blend;
        c->drifting = false;
    }

    /* A car that does not stand on its wheels and hardly moves does not get going again:
     * it lies on its roof, or hangs on the edge of the road with a wheel or two in the air.
     * A moment of that and it is put back. */
    c->stuck = c->on_road < 3 && dot(v, v) < 4.0f ? c->stuck + frame_dt : 0.0f;

    /* What the wheels and the air did goes to the body, and picophysics takes it from
     * there for this frame: gravity, the move, the turn, and whatever the body meets on
     * its way, a rail, a wall, or the road when the car comes down on its side or roof. */
    pp_vec3_set(&body->vel, v[0], v[1], v[2]);
    pp_vec3_set(&body->a_vel, c->spin[0], c->spin[1], c->spin[2]);
    {
        /* The triangles are picked for the car and for every crate that is still there. */
        float at[1 + MAX_CRATES][3];
        int   count = 1;
        memcpy(at[0], pos, sizeof(at[0]));
        for (i = 0; i < c->crate_count; i++) {
            const PPBody *crate = (const PPBody *)c->crate_bodies[i];
            if (crate != NULL) {
                memcpy(at[count++], crate->pos.xyz, sizeof(at[0]));
            }
        }
        track_near(at, count);
    }
    memcpy(g_body_up, c->up, sizeof(g_body_up));
    g_hit_ago += dt;
    pp_physics_step(dt, 6, 3);
    body_read(c, self, v, right);
    for (i = 0; i < c->crate_count; i++) {
        PPBody *crate = (PPBody *)c->crate_bodies[i];
        PPVec3  x, y, z;
        float  *m = c->crates[i]->local.m;
        if (crate == NULL) {
            continue;
        }
        if (crate->pos.y < c->fall_limit) {
            /* Fallen off the world: the crate is gone until the scene is loaded again. */
            pp_physics_destroy_body(crate);
            c->crate_bodies[i] = NULL;
            c->crates[i]->flags &= (uint8_t)~RR_NODE_FLAG_VISIBLE;
            continue;
        }
        pp_body_get_right(crate, &x);
        pp_body_get_up(crate, &y);
        pp_body_get_forward(crate, &z);
        for (k = 0; k < 3; k++) {
            m[k]     = x.xyz[k];
            m[3 + k] = y.xyz[k];
            m[6 + k] = z.xyz[k];
            m[9 + k] = crate->pos.xyz[k];
        }
    }
    pose_model(c);
    place_camera(c, pos, v, dt);

    if (c->engine_sound != NULL) {
        const float rev = fabsf(c->speed) / (c->max_speed > 1.0f ? c->max_speed : 1.0f);
        rr_audio_set_pitch_scale(c->engine_sound, 0.6f + 1.4f * (rev < 1.2f ? rev : 1.2f));
    }
    if (c->speed_label != NULL) {
        snprintf(c->speed_text, sizeof(c->speed_text), "%d KM/H", (int)(fabsf(c->speed) * 3.6f));
        rr_node_set_text(c->speed_label, c->speed_text);
    }
}

/* Connected to the `area_entered` signal of the car's hitbox area. */
static void stunt_car_on_area(Node *self, StuntCar *c, Node *area)
{
    int i, k;

    if (area == NULL || c->done > 0.0f) {
        return;
    }
    if (rr_node_has_tag(area, "checkpoint")) {
        for (i = 0; i < c->passed_count; i++) {
            if (c->passed[i] == area) {
                return;
            }
        }
        if (c->passed_count < MAX_CHECKPOINTS) {
            c->passed[c->passed_count++] = area;
        }
        /* From now on the car is put back here, standing the way the gate does. The area
         * hangs two metres above the road; the car is put a little above where it rides. */
        for (k = 0; k < 3; k++) {
            c->home_forward[k] = -area->world.m[6 + k];
            c->home_up[k]      = area->world.m[3 + k];
        }
        normalize(c->home_forward);
        normalize(c->home_up);
        for (k = 0; k < 3; k++) {
            c->home[k] = area->world.m[9 + k] - c->home_up[k] * (2.0f - c->suspension);
        }
        show_info(c);
        /* A line for whoever reads the output: the tests do. */
        printf("stunt: checkpoint %d/%d\n", c->passed_count, c->checkpoint_total);
    } else if (rr_node_has_tag(area, "goal") && c->passed_count >= c->checkpoint_total) {
        /* Only a run through every checkpoint counts. */
        if (c->best <= 0.0f || c->time < c->best) {
            const StuntSave save = { SAVE_MAGIC, c->time };
            rr_save_write(SAVE_NAME, &save, sizeof(save));
        }
        c->done = 0.001f;
        show_info(c);
        rr_input_vibrate(0.4f, 0.8f, 0.3f);
    }
    (void)self;
}

COMPONENT_BEGIN(StuntCar, "StuntCar")
    EXPORT_FLOAT(engine)
    EXPORT_FLOAT(brake)
    EXPORT_FLOAT(max_speed)
    EXPORT_FLOAT(steer)
    EXPORT_FLOAT(steer_fast)
    EXPORT_FLOAT(grip)
    EXPORT_FLOAT(drift)
    EXPORT_FLOAT(suspension)
    EXPORT_FLOAT(spring)
    EXPORT_FLOAT(damping)
    EXPORT_FLOAT(downforce)
    EXPORT_FLOAT(gravity)
    EXPORT_FLOAT(fall_limit)
    EXPORT_FLOAT(crate_size)
    EXPORT_NODE(model)
    EXPORT_NODE(wheel_fl)
    EXPORT_NODE(wheel_fr)
    EXPORT_NODE(wheel_rl)
    EXPORT_NODE(wheel_rr)
    EXPORT_NODE(camera)
    EXPORT_NODE(engine_sound)
    EXPORT_NODE(speed_label)
    EXPORT_NODE(time_label)
    EXPORT_NODE(info_label)
    EXPORT_SCENE(menu_scene)
    ON_READY(stunt_car_ready)
    ON_UPDATE(stunt_car_update)
    ON_SIGNAL_NODE("_on_hitbox_area_entered", stunt_car_on_area)
COMPONENT_END()
