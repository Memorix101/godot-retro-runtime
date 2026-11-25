/* Components of the highway game (scenes/highway.tscn and highway_title.tscn), a port of
 * the Unity game "Highway Pursuit" (2018-2024), whose scenes tools/make_highway.gd
 * converts. Placeholder scripts: components/highway_*.gd, chase_camera.gd.
 *
 * A police car chases rivals on an endless desert highway. Rivals are taken down by
 * ramming them, grinding along them or pushing them into the rails; traffic in both
 * directions is in the way. Touching traffic wrecks the police car; after the third
 * wreck the game is over.
 *
 * The road runs along Godot's -Z (Unity's +Z). "Along" is the distance in that
 * direction (-z), and a vehicle's `dir` is +1 when it drives that way, -1 when it comes
 * the other way. The cars stay at one height; nothing but the rails is solid:
 *
 * HighwayPlayer  a CharacterBody3D moved with rr_body_move_and_slide against the rails
 *                (StaticBody3D boxes). Its Area3D child "Hitbox" reports traffic and
 *                rivals, its Area3D child "NearMiss" (a thin slice across the car) the
 *                traffic it just passed.
 * HighwayRival   an Area3D moved by code: chases or flees the player, changes lanes,
 *                takes damage from hits; at full damage, or pushed into a rail, it is
 *                taken down.
 * HighwayCar     traffic, an Area3D moved by code: keeps its lane and its distance,
 *                changes lanes when held up.
 * HighwayRoad    places the road segments (a PackedScene) one after another and frees
 *                the ones behind; every new segment brings traffic and rivals from its
 *                spawn markers, as the Unity scripts TrafficSpawn and SpawnManager did.
 * ChaseCamera    stays at a fixed offset from its target and shakes on demand.
 * TitleScreen    a blinking prompt; "start" loads the game.
 *
 * What the vehicles know of each other (HighwayTraffic.cs in Unity) is in the struct
 * Vehicle at the start of each of the three vehicle components; the nodes are found by
 * the group "vehicle". */
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../core/rr_scene.h"

#define DEG2RAD     0.017453292f
#define RIDE_HEIGHT -157.5f /* the height the Unity cars were held at */
#define MAX_LANES   8
#define NO_LANE     1.0e9f

/* A float field of the embedded Vehicle, exported under its own name. */
#define EXPORT_FLOAT_AS(name, field) rr_component_add_field(ct_, (name), 1, offsetof(Self_, field));
#define EXPORT_INT_AS(name, field)   rr_component_add_field(ct_, (name), 2, offsetof(Self_, field));

static float clampf_(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static float move_towards(float v, float to, float step)
{
    return fabsf(to - v) <= step ? to : v + (to > v ? step : -step);
}

static float frand(float lo, float hi)
{
    return lo + (hi - lo) * ((float)rand() / (float)RAND_MAX);
}

static void show(Node *node, bool visible)
{
    if (node != NULL) {
        if (visible) {
            node->flags |= RR_NODE_FLAG_VISIBLE;
        } else {
            node->flags &= (uint8_t)~RR_NODE_FLAG_VISIBLE;
        }
    }
}

static void set_text(Node *label, const char *text)
{
    if (label != NULL) {
        rr_node_set_text(label, text);
    }
}

static void play(Node *player)
{
    if (player != NULL) {
        rr_audio_play(player);
    }
}

/* Unity's yaw (degrees, clockwise seen from above, 0 = along the road) as a basis, in
 * Godot's axes, with a pitch about the car's own X and a scale kept per axis. */
static void set_yaw(float *m, float yaw, float pitch, const float *scale)
{
    const float a = -yaw * DEG2RAD, b = pitch * DEG2RAD;
    const float sa = sinf(a), ca = cosf(a), sb = sinf(b), cb = cosf(b);
    /* Ry(a) * Rx(b) */
    m[0] = ca * scale[0];       m[1] = 0.0f;               m[2] = -sa * scale[0];
    m[3] = sa * sb * scale[1];  m[4] = cb * scale[1];      m[5] = ca * sb * scale[1];
    m[6] = sa * cb * scale[2];  m[7] = -sb * scale[2];     m[8] = ca * cb * scale[2];
}

static float rotate_towards(float yaw, float target, float step)
{
    float d = fmodf(target - yaw + 540.0f, 360.0f) - 180.0f;
    return yaw + clampf_(d, -step, step);
}

static float along_of(const Node *n)
{
    return -n->world.m[11];
}

/* A node's place in the world now, from the local transforms of it and its parents (the
 * world transforms are only worked out at the end of the frame). */
static void world_now(const Node *node, RRMat34 *out)
{
    RRMat34 w = node->local;
    const Node *p;
    for (p = node->parent; p != NULL; p = p->parent) {
        const float *a = p->local.m;
        RRMat34      r;
        int          c, i;
        for (c = 0; c < 4; c++) {
            for (i = 0; i < 3; i++) {
                r.m[c * 3 + i] = a[i] * w.m[c * 3] + a[3 + i] * w.m[c * 3 + 1] + a[6 + i] * w.m[c * 3 + 2] +
                                 (c == 3 ? a[9 + i] : 0.0f);
            }
        }
        w = r;
    }
    *out = w;
}

/* --- Vehicles ------------------------------------------------------------------------- */

typedef struct {
    float half_width;  /* exported: half the width of the car, metres */
    float half_length; /* exported: half its length */
    int32_t wreck_kind; /* exported: which of HighwayRoad's wrecks it becomes: 0 car, 1 truck,
                         * 2 rival, 3 police car, 4 red rival */

    float speed;    /* along its driving direction */
    float lateral;  /* sideways (world X) speed */
    float dir;      /* +1 along the road (-Z), -1 against it */
    float target_x; /* the lane it is moving into, NO_LANE for none */
    float yaw;      /* Unity's yaw, degrees */
    bool  crashed;
    bool  is_player;
    bool  placed;   /* set up on the road */
    /* Set by vehicle_collide when it crashed into another car: the world X and Z
     * velocity it comes out with and how hard it was hit, for its wreck. */
    bool  hit;
    float hit_vel[2];
    float hit_strength;
} Vehicle;

typedef struct {
    float gap_ahead, speed_ahead, ttc_ahead;
    float gap_behind, ttc_behind;
    bool  alongside;
    float speed_alongside;
} LaneScan;

static const char *const VEHICLE_TYPES[] = { "HighwayPlayer", "HighwayRival", "HighwayCar" };

/* The Vehicle at the start of a node's vehicle component, NULL if it has none. */
static Vehicle *vehicle_of(Node *node)
{
    int i;
    if (node == NULL) {
        return NULL;
    }
    for (i = 0; i < 3; i++) {
        Vehicle *v = (Vehicle *)rr_node_component(node, VEHICLE_TYPES[i]);
        if (v != NULL) {
            return v;
        }
    }
    return NULL;
}

/* The road: lanes and rails, set by HighwayRoad from the first segment's markers. */
static struct {
    int   lane_count;
    float lanes[MAX_LANES]; /* world X, sorted */
    float center;
    float rail_left, rail_right; /* the inner faces of the rails */
    bool  known;
} g_road;

static int nearest_lane(float x)
{
    int i, best = 0;
    for (i = 1; i < g_road.lane_count; i++) {
        if (fabsf(g_road.lanes[i] - x) < fabsf(g_road.lanes[best] - x)) {
            best = i;
        }
    }
    return best;
}

static void scan_one(const Vehicle *self, Node *self_node, Node *other_node, float lane_x,
                     float range, LaneScan *s)
{
    const Vehicle *other = vehicle_of(other_node);
    float          reach, rel, gap, other_speed;
    const float    my_along = along_of(self_node) * self->dir;
    const float    my_speed = self->speed;

    if (other == NULL || other == self || !other->placed || other_node->flags & RR_NODE_FLAG_FREE) {
        return;
    }
    reach = other->half_width + self->half_width + 0.3f;
    if (fabsf(other_node->world.m[9] - lane_x) > reach && !(fabsf(other->target_x - lane_x) <= reach)) {
        return;
    }
    rel = along_of(other_node) * self->dir - my_along;
    if (fabsf(rel) > range) {
        return;
    }
    gap         = fabsf(rel) - other->half_length - self->half_length;
    other_speed = other->speed * other->dir * self->dir;
    if (gap <= 1.5f) {
        s->alongside = true;
        if (other_speed < s->speed_alongside) {
            s->speed_alongside = other_speed;
        }
        return;
    }
    if (rel > 0.0f) {
        float closing = my_speed - other_speed;
        if (gap < s->gap_ahead) {
            s->gap_ahead   = gap;
            s->speed_ahead = other_speed;
        }
        if (closing > 0.1f && gap / closing < s->ttc_ahead) {
            s->ttc_ahead = gap / closing;
        }
    } else {
        float closing = other_speed - my_speed;
        if (gap < s->gap_behind) {
            s->gap_behind = gap;
        }
        if (closing > 0.1f && gap / closing < s->ttc_behind) {
            s->ttc_behind = gap / closing;
        }
    }
}

/* What is in a lane ahead of and behind a vehicle (HighwayTraffic.Scan). */
static LaneScan scan_lane(const Vehicle *self, Node *self_node, float lane_x, bool include_player, float range)
{
    LaneScan s = { 1.0e9f, 0.0f, 1.0e9f, 1.0e9f, 1.0e9f, false, 1.0e9f };
    Node    *n = NULL;

    while ((n = rr_scene_find_tagged(self_node->scene, "vehicle", n)) != NULL) {
        const Vehicle *v = vehicle_of(n);
        if (v != NULL && (include_player || !v->is_player)) {
            scan_one(self, self_node, n, lane_x, range, &s);
        }
    }
    return s;
}

static bool safe_to_enter(const LaneScan *s, float min_gap_ahead)
{
    return !s->alongside && s->gap_ahead > min_gap_ahead && s->ttc_ahead > 2.0f && s->gap_behind > 5.0f &&
           s->ttc_behind > 2.5f;
}

/* Nothing within `clearance` along the road of a place (HighwayTraffic.IsFree). */
static bool place_is_free(RRScene *scene, float x, float along, float clearance)
{
    Node *n = NULL;
    while ((n = rr_scene_find_tagged(scene, "vehicle", n)) != NULL) {
        const Vehicle *v = vehicle_of(n);
        if (v != NULL && v->placed && fabsf(n->world.m[9] - x) < v->half_width + 1.5f &&
            fabsf(along_of(n) - along) < v->half_length + clearance) {
            return false;
        }
    }
    return true;
}

/* Moves a vehicle towards a lane and along the road (HighwayTraffic.Steer). The node is
 * a child of the root, so its local transform is its place in the world. */
static void steer(Node *node, Vehicle *v, float target_x, float max_lateral, float lateral_accel,
                  const float *scale, float dt)
{
    float      *m      = node->local.m;
    const float wanted = clampf_((target_x - m[9]) * 2.5f, -max_lateral, max_lateral);

    v->lateral = move_towards(v->lateral, wanted, lateral_accel * dt);
    m[9] += v->lateral * dt;
    m[11] -= v->speed * v->dir * dt;
    m[10] = RIDE_HEIGHT;
    if (v->speed > 1.0f) {
        const float yaw = atan2f(v->lateral, v->speed * v->dir) / DEG2RAD;
        v->yaw          = rotate_towards(v->yaw, yaw, 120.0f * dt);
    }
    set_yaw(m, v->yaw, 0.0f, scale);
}

/* A wrecked vehicle: it slides to a stop, spins and hops, as the Unity cars did once
 * their rigid bodies were let loose. */
typedef struct {
    float spin;     /* degrees per second */
    float up;       /* vertical speed */
    float height;   /* above the ride height */
    float pitch;
    float time;     /* since the crash */
    bool  tried;    /* a wreck (rigid body) was asked for */
} Wreck;

static void wreck_start(Wreck *w, const Vehicle *v, float hop)
{
    w->spin   = frand(-1.0f, 1.0f) * (90.0f + v->speed * 3.0f);
    w->up     = hop;
    w->height = 0.0f;
    w->pitch  = 0.0f;
    w->time   = 0.0f;
}

/* `node` is moved, `turn` is turned (the same node for all but the police car). */
static void wreck_update(Node *node, Node *turn, Vehicle *v, Wreck *w, const float *scale, float dt)
{
    float *m = node->local.m;
    w->time += dt;
    v->speed   = move_towards(v->speed, 0.0f, 25.0f * dt);
    v->lateral = move_towards(v->lateral, 0.0f, 10.0f * dt);
    w->spin    = move_towards(w->spin, 0.0f, 120.0f * dt);
    v->yaw += w->spin * dt;
    w->up -= 20.0f * dt;
    w->height += w->up * dt;
    if (w->height < 0.0f) {
        w->height = 0.0f;
        w->up     = -w->up * 0.3f;
    }
    w->pitch = move_towards(w->pitch, w->height > 0.5f ? 25.0f : 0.0f, 60.0f * dt);
    m[9] += v->lateral * dt;
    m[11] -= v->speed * v->dir * dt;
    m[10] = RIDE_HEIGHT + w->height;
    set_yaw(turn->local.m, v->yaw, w->pitch, scale);
}

static void get_scale(const float *m, float *scale)
{
    int i;
    for (i = 0; i < 3; i++) {
        scale[i] = sqrtf(m[i * 3] * m[i * 3] + m[i * 3 + 1] * m[i * 3 + 1] + m[i * 3 + 2] * m[i * 3 + 2]);
    }
}

/* Puts a pooled vehicle on the road. */
static void vehicle_place(Node *node, Vehicle *v, float x, float along, float dir)
{
    float scale[3];
    get_scale(node->local.m, scale);
    v->dir      = dir;
    v->yaw      = dir > 0.0f ? 0.0f : 180.0f;
    v->lateral  = 0.0f;
    v->crashed  = false;
    v->hit      = false;
    v->target_x = NO_LANE;
    v->placed   = true;
    node->local.m[9]  = x;
    node->local.m[10] = RIDE_HEIGHT;
    node->local.m[11] = -along;
    set_yaw(node->local.m, v->yaw, 0.0f, scale);
}

/* --- Wrecks ------------------------------------------------------------------------------
 * In Unity a wrecked car's rigid body was let loose: it tumbled, rolled and flew. Here a
 * wrecked car is swapped for a wreck: a RigidBody3D with the same model (a PackedScene of
 * HighwayRoad), placed where the car was and thrown the way it was going, with a turn
 * about its own right axis as Unity's AddTorque gave it. The physics engine does the rest
 * against the rails and the ground of the road. */

#define WRECK_KINDS 5
static const RRPackedScene *g_wrecks[WRECK_KINDS]; /* set by HighwayRoad */

/* Two cars crash into each other. In Unity they were rigid bodies of the same mass (a
 * wreck weighs crashMass) that pushed each other: each comes out with the mean of the two
 * velocities and a little bounce, so a head-on crash stops both and a car rammed from
 * behind is shoved ahead. Only the first crash of a car counts. Done before either car is
 * changed, so each sees the other's velocity from before. */
static void vehicle_collide(Vehicle *a, Vehicle *b)
{
    const float bounce = 0.2f;
    const float va[2]  = { a->lateral, -a->speed * a->dir };
    const float vb[2]  = { b->lateral, -b->speed * b->dir };
    const float dx = va[0] - vb[0], dz = va[1] - vb[1];
    const float strength = sqrtf(dx * dx + dz * dz);
    int         i;

    for (i = 0; i < 2; i++) {
        const float mean = 0.5f * (va[i] + vb[i]), half = 0.5f * (va[i] - vb[i]);
        if (!a->hit) {
            a->hit_vel[i] = mean - bounce * half;
        }
        if (!b->hit) {
            b->hit_vel[i] = mean + bounce * half;
        }
    }
    if (!a->hit) {
        a->hit          = true;
        a->hit_strength = strength;
    }
    if (!b->hit) {
        b->hit          = true;
        b->hit_strength = strength;
    }
}

/* A wreck for a car, thrown with its velocity (or what a crash with another car left of
 * it) and `hop` metres per second upwards, more the harder it was hit. NULL if there is
 * no wreck scene or all are in use. `look` is the node that is turned with the car (the
 * car itself, or the police car's "Turn"). */
static Node *spawn_wreck(Node *look, const Vehicle *v, float hop)
{
    Node   *w;
    RRMat34 place;
    float   vel[3], spin[3], right[3], ahead[3], len, along, roll;
    int     i;

    if (v->wreck_kind < 0 || v->wreck_kind >= WRECK_KINDS || g_wrecks[v->wreck_kind] == NULL) {
        return NULL;
    }
    w = rr_instantiate(g_wrecks[v->wreck_kind]);
    if (w == NULL) {
        return NULL;
    }
    world_now(look, &place);
    w->local = place;
    vel[0] = v->hit ? v->hit_vel[0] : v->lateral;
    vel[1] = hop + (v->hit ? v->hit_strength * 0.04f : 0.0f);
    vel[2] = v->hit ? v->hit_vel[1] : -v->speed * v->dir;
    len = sqrtf(place.m[0] * place.m[0] + place.m[1] * place.m[1] + place.m[2] * place.m[2]);
    for (i = 0; i < 3; i++) {
        right[i] = len > 0.0f ? place.m[i] / len : (i == 0 ? 1.0f : 0.0f);
    }
    len = sqrtf(place.m[6] * place.m[6] + place.m[7] * place.m[7] + place.m[8] * place.m[8]);
    for (i = 0; i < 3; i++) {
        ahead[i] = len > 0.0f ? -place.m[6 + i] / len : (i == 2 ? -1.0f : 0.0f);
    }
    /* Unity's AddTorque(right * 10 * velocity.z): head over heels with the speed it still
     * has along its nose (the nose goes down). A hard hit also rolls it and turns it. */
    along = vel[0] * ahead[0] + vel[2] * ahead[2];
    roll  = v->hit ? frand(-1.0f, 1.0f) * v->hit_strength * 0.03f : 0.0f;
    for (i = 0; i < 3; i++) {
        spin[i] = -right[i] * along * 0.04f + ahead[i] * roll;
    }
    spin[1] += frand(-1.0f, 1.0f) * (2.0f + (v->hit ? v->hit_strength * 0.04f : 0.0f));
    rr_rigid_body_set_linear_velocity(w, vel);
    rr_rigid_body_set_angular_velocity(w, spin);
    return w;
}

/* The police car as the wrecks see it, set by HighwayPlayer every frame: where it is, how
 * big, how fast, and whether it drives (not wrecked itself). */
static struct {
    bool  driving;
    float x, along, half_width, half_length;
    float velocity[3];
} g_police;

/* HighwayWreck: the RigidBody3D of a wreck. It fires its burst and its sound when it
 * appears and is freed after `lifetime` seconds (Unity's CollisionParticle did that).
 * The police car shoves it aside: in Unity the police car's rigid body was driven at its
 * speed whatever it hit, so a wreck in the way was knocked off. */
typedef struct {
    float lifetime; /* exported: seconds */
    Node *burst;    /* exported: particle systems fired when it appears, or none */
    Node *sound;    /* exported: played when it appears, or none */
    float radius;   /* exported: metres, how far it reaches out from its centre */

    float age;
} HighwayWreck;

static void highway_wreck_shove(Node *self, const HighwayWreck *w)
{
    float       vel[3], n[2], len, closing, spin[3];
    const float dx = self->world.m[9] - g_police.x;
    const float da = -self->world.m[11] - g_police.along;

    if (!g_police.driving || fabsf(dx) > g_police.half_width + w->radius ||
        fabsf(da) > g_police.half_length + w->radius) {
        return;
    }
    len  = sqrtf(dx * dx + da * da);
    n[0] = len > 0.001f ? dx / len : 0.0f;
    n[1] = len > 0.001f ? -da / len : -1.0f; /* world Z of the direction away from the police car */
    rr_rigid_body_get_linear_velocity(self, vel);
    closing = (g_police.velocity[0] - vel[0]) * n[0] + (g_police.velocity[2] - vel[2]) * n[1];
    if (closing <= 0.0f) {
        return;
    }
    /* Knocked away a little faster than the police car comes, and up. */
    vel[0] += n[0] * closing * 1.2f;
    vel[2] += n[1] * closing * 1.2f;
    vel[1] += closing * 0.1f;
    rr_rigid_body_set_linear_velocity(self, vel);
    rr_rigid_body_get_angular_velocity(self, spin);
    spin[1] += frand(-1.0f, 1.0f) * closing * 0.1f;
    rr_rigid_body_set_angular_velocity(self, spin);
}

static void highway_wreck_ready(Node *self, HighwayWreck *w)
{
    Node *child = NULL;
    (void)self;
    w->age = 0.0f;
    if (w->burst != NULL) {
        if (w->burst->type == RR_NODE_PARTICLES) {
            rr_particles_restart(w->burst);
        }
        while ((child = rr_node_child(w->burst, child)) != NULL) {
            if (child->type == RR_NODE_PARTICLES) {
                rr_particles_restart(child);
            }
        }
    }
    if (w->sound != NULL) {
        rr_audio_play(w->sound);
    }
}

static void highway_wreck_update(Node *self, HighwayWreck *w, float dt)
{
    w->age += dt;
    if (w->age > w->lifetime || self->world.m[10] < RIDE_HEIGHT - 60.0f) {
        rr_queue_free(self);
        return;
    }
    highway_wreck_shove(self, w);
}

COMPONENT_BEGIN(HighwayWreck, "HighwayWreck")
    EXPORT_FLOAT(lifetime)
    EXPORT_FLOAT(radius)
    EXPORT_NODE(burst)
    EXPORT_NODE(sound)
    ON_READY(highway_wreck_ready)
    ON_UPDATE(highway_wreck_update)
COMPONENT_END()

/* --- ChaseCamera ------------------------------------------------------------------------ */

typedef struct {
    Node *target; /* exported */
    float height; /* exported: metres above the target */
    float back;   /* exported: metres behind it (Godot +Z) */

    float shake;
    float base[9]; /* the camera's turn, kept */
} ChaseCamera;

static void chase_camera_ready(Node *self, ChaseCamera *c)
{
    memcpy(c->base, self->local.m, sizeof(c->base));
}

static void chase_camera_update(Node *self, ChaseCamera *c, float dt)
{
    float *m = self->local.m;
    if (c->target == NULL) {
        return;
    }
    memcpy(m, c->base, sizeof(c->base));
    m[9]  = c->target->world.m[9];
    m[10] = c->target->world.m[10] + c->height;
    m[11] = c->target->world.m[11] + c->back;
    if (c->shake > 0.0f) {
        m[9] += frand(-1.0f, 1.0f) * c->shake;
        m[10] += frand(-1.0f, 1.0f) * c->shake;
        m[11] += frand(-1.0f, 1.0f) * c->shake;
        c->shake = move_towards(c->shake, 0.0f, 3.0f * dt);
    }
}

static void camera_shake(RRScene *scene, float amount)
{
    Node *n = rr_scene_find_tagged(scene, "main_camera", NULL);
    if (n != NULL) {
        ChaseCamera *c = (ChaseCamera *)rr_node_component(n, "ChaseCamera");
        if (c != NULL && amount > c->shake) {
            c->shake = amount;
        }
    }
}

COMPONENT_BEGIN(ChaseCamera, "ChaseCamera")
    EXPORT_NODE(target)
    EXPORT_FLOAT(height)
    EXPORT_FLOAT(back)
    ON_READY(chase_camera_ready)
    ON_UPDATE(chase_camera_update)
COMPONENT_END()

/* --- HighwayPlayer ---------------------------------------------------------------------- */

typedef struct {
    Vehicle v;          /* must come first */
    float   max_speed;  /* exported */
    float   boost_speed; /* exported */
    float   accel_time; /* exported: seconds of the speed curve (Unity: 2) */
    float   turn_rate;  /* exported: degrees per second at speed (Unity: 50) */
    int32_t lives;      /* exported */
    float   wall_crash_angle; /* exported: degrees; a rail hit more head-on than this at speed wrecks */
    float   wall_speed_loss;  /* exported: share of speed a square rail hit costs */
    Node   *turn;           /* exported: the child that turns with the car (model, lights); the
                             * body itself stays unturned, as its shape must */
    Node   *speed_label;    /* exported */
    Node   *score_label;    /* exported */
    Node   *takedown_label; /* exported */
    Node   *boost_bar;      /* exported: TextureProgressBar, 0..100 */
    Node   *hud;            /* exported: hidden while wrecked */
    Node   *damage_label;   /* exported: shown on the last life */
    Node   *game_over;      /* exported: the game over screen */
    Node   *result_label;   /* exported: score and takedowns on the game over screen */
    Node   *engine_sound;   /* exported: looping; its pitch follows the speed */
    Node   *crash_sound;    /* exported */
    Node   *boost_sound;    /* exported */
    Node   *takedown_sound; /* exported */
    Node   *near_miss_sound; /* exported */
    Node   *impact_sound;   /* exported: a rival hit */
    const char *title_scene; /* exported */

    int   act_left, act_right, act_gas, act_brake, act_boost, act_start, act_cancel;
    float scale[3];
    float t, last_speed, current_max;
    bool  cold_start, boosting, on_wall;
    float boost; /* 0..1 */
    int   score, takedowns;
    float shove[2];
    Wreck wreck;
    float spawn_x;
    Node *wreck_node; /* the police car's wreck while it flies, or NULL */
    char  speed_text[8], score_text[12], takedown_text[8], result_text[96];
} HighwayPlayer;

static HighwayPlayer *player_of(RRScene *scene, Node **node)
{
    Node *n = rr_scene_find_tagged(scene, "player", NULL);
    if (node != NULL) {
        *node = n;
    }
    return n != NULL ? (HighwayPlayer *)rr_node_component(n, "HighwayPlayer") : NULL;
}

static void player_lose_speed(HighwayPlayer *p, float amount)
{
    if (amount > 0.0f) {
        p->v.speed     = fmaxf(0.0f, p->v.speed - amount);
        p->last_speed = p->v.speed;
        p->t          = 0.0f;
    }
}

static void player_add_boost(HighwayPlayer *p, float amount)
{
    p->boost = clampf_(p->boost + amount, 0.0f, 1.0f);
}

static void player_crash(Node *self, HighwayPlayer *p)
{
    if (p->v.crashed) {
        return;
    }
    p->v.crashed = true;
    printf("highway: crash\n");
    wreck_start(&p->wreck, &p->v, 4.0f);
    /* The car's model goes, its wreck flies; the body stays and follows the wreck, so the
     * camera does. */
    p->wreck_node = spawn_wreck(p->turn, &p->v, 1.0f);
    if (p->wreck_node != NULL) {
        show(p->turn, false);
    }
    show(p->hud, false);
    play(p->crash_sound);
    if (p->engine_sound != NULL) {
        rr_audio_stop(p->engine_sound);
    }
    if (p->boost_sound != NULL) {
        rr_audio_stop(p->boost_sound);
    }
    rr_input_vibrate(1.0f, 1.0f, 0.4f);
    camera_shake(self->scene, 1.0f);
}

/* Called by a rival that was hit: a push, a loss of speed and a shake. */
static void player_rival_contact(Node *self, HighwayPlayer *p, float push_x, float speed_loss, float strength)
{
    if (p->v.crashed) {
        return;
    }
    p->shove[0] += push_x;
    player_lose_speed(p, speed_loss);
    rr_input_vibrate(strength * 0.5f, strength, 0.2f);
    camera_shake(self->scene, strength * 0.6f);
}

static void player_takedown(HighwayPlayer *p, int score)
{
    p->score += score;
    p->takedowns++;
    p->boost = 1.0f;
    play(p->takedown_sound);
}

static void highway_player_ready(Node *self, HighwayPlayer *p)
{
    p->act_left   = rr_action(self->scene, "steer_left");
    p->act_right  = rr_action(self->scene, "steer_right");
    p->act_gas    = rr_action(self->scene, "accelerate");
    p->act_brake  = rr_action(self->scene, "brake");
    p->act_boost  = rr_action(self->scene, "boost");
    p->act_start  = rr_action(self->scene, "start");
    p->act_cancel = rr_action(self->scene, "ui_cancel");
    if (p->turn == NULL) {
        p->turn = self;
    }
    get_scale(p->turn->local.m, p->scale);
    p->v.dir       = 1.0f;
    p->v.yaw       = -atan2f(p->turn->local.m[6], p->turn->local.m[8]) / DEG2RAD;
    p->v.target_x  = NO_LANE;
    p->v.is_player = true;
    p->v.placed    = true;
    p->cold_start  = true;
    p->current_max = p->max_speed;
    p->spawn_x     = self->local.m[9];
    show(p->game_over, false);
    show(p->damage_label, false);
}

/* The speed model of the Unity script: speed eases from where it was towards the top
 * speed while the gas is held, and back towards 0 when it is not, faster when braking. */
static void player_drive(HighwayPlayer *p, float dt)
{
    const bool gas   = rr_action_down(p->act_gas);
    const bool brake = rr_action_down(p->act_brake);
    const bool boost = rr_action_down(p->act_boost);
    float      turn;

    if (gas) {
        if (p->v.speed == 0.0f && p->t < 0.0f) {
            p->t          = 0.0f;
            p->last_speed = 0.0f;
        } else if (p->last_speed != 0.0f) {
            p->t          = 0.0f;
            p->last_speed = p->v.speed;
            p->cold_start = false;
        }
        if (p->t <= 1.0f) {
            p->t += dt / fmaxf(p->accel_time, 0.1f);
        }
        p->v.speed = p->cold_start ? p->current_max * clampf_(p->t, 0.0f, 1.0f)
                                   : p->last_speed + (p->current_max - p->last_speed) * clampf_(p->t, 0.0f, 1.0f);
    } else {
        if (p->last_speed == 0.0f) {
            p->t          = 0.0f;
            p->last_speed = p->v.speed;
        }
        if (p->t >= -1.0f) {
            p->t -= brake ? dt : dt / 10.0f;
        }
        p->v.speed = p->last_speed * (1.0f - clampf_(fabsf(p->t), 0.0f, 1.0f));
    }

    if (boost && p->boost > 0.0f && p->v.speed >= 5.0f) {
        if (!p->boosting) {
            play(p->boost_sound);
        }
        p->boosting    = true;
        p->current_max = p->boost_speed;
        p->boost       = fmaxf(0.0f, p->boost - 0.2f * dt);
    } else {
        if (p->boosting && p->boost_sound != NULL) {
            rr_audio_stop(p->boost_sound);
        }
        p->boosting    = false;
        p->current_max = p->max_speed;
    }

    /* Turning: up to turn_rate degrees per second, slower below 50 (Unity turned by the
     * speed up to 50, one degree per unit). */
    turn = fminf(p->v.speed, 50.0f) / 50.0f * p->turn_rate * dt;
    if (rr_action_down(p->act_right)) {
        p->v.yaw += turn;
    }
    if (rr_action_down(p->act_left)) {
        p->v.yaw -= turn;
    }
}

/* Against a rail: a glancing touch costs a little speed and turns the car along it, a
 * square hit at speed wrecks it (RailContact in the Unity script). */
static void player_rail(Node *self, HighwayPlayer *p, bool impact, float dt)
{
    const float n    = self->world.m[9] < g_road.center ? 1.0f : -1.0f; /* away from the rail */
    const float fx   = sinf(p->v.yaw * DEG2RAD);
    const float into = -fx * n;
    float       along_yaw;

    if (into <= 0.01f) {
        return;
    }
    if (impact) {
        if (into > sinf(p->wall_crash_angle * DEG2RAD) && p->v.speed > 60.0f) {
            player_crash(self, p);
            return;
        }
        player_lose_speed(p, p->v.speed * p->wall_speed_loss * into * into);
        p->shove[0] = n * (2.0f + p->v.speed * into * 0.15f);
        rr_input_vibrate(0.3f + into * 0.5f, 0.3f + into * 0.7f, 0.2f);
        camera_shake(self->scene, 0.2f + into * 0.5f);
    } else {
        player_lose_speed(p, 8.0f * dt);
    }
    /* Turned along the rail, a little away from it. */
    along_yaw = atan2f(n * 0.08f, 1.0f) / DEG2RAD;
    p->v.yaw  = rotate_towards(p->v.yaw, along_yaw, 300.0f * dt);
}

static void player_show(HighwayPlayer *p)
{
    snprintf(p->speed_text, sizeof(p->speed_text), "%d", (int)p->v.speed);
    snprintf(p->score_text, sizeof(p->score_text), "%05d", p->score);
    snprintf(p->takedown_text, sizeof(p->takedown_text), "%d", p->takedowns);
    set_text(p->speed_label, p->speed_text);
    set_text(p->score_label, p->score_text);
    set_text(p->takedown_label, p->takedown_text);
    if (p->boost_bar != NULL) {
        p->boost_bar->value = p->boost * 100.0f;
    }
}

/* Back on the road after a wreck, one life less, in the middle of the road where the
 * car was. */
static void player_respawn(Node *self, HighwayPlayer *p)
{
    float *m = self->local.m;
    if (--p->lives <= 0) {
        printf("highway: game over, score %d, %d takedowns\n", p->score, p->takedowns);
        show(p->game_over, true);
        show(p->damage_label, false);
        snprintf(p->result_text, sizeof(p->result_text), "Current Highscore\n%d Score\n%d Takedowns", p->score,
                 p->takedowns);
        set_text(p->result_label, p->result_text);
        return;
    }
    if (p->wreck_node != NULL) {
        if (!(p->wreck_node->flags & RR_NODE_FLAG_FREE)) {
            rr_queue_free(p->wreck_node);
        }
        p->wreck_node = NULL;
    }
    show(p->turn, true);
    m[9]  = g_road.known ? g_road.center : p->spawn_x;
    m[10] = RIDE_HEIGHT;
    p->v.crashed  = false;
    p->v.hit      = false;
    p->v.yaw      = 0.0f;
    p->v.speed    = 50.0f;
    p->last_speed = 50.0f;
    p->t          = 0.0f;
    p->shove[0]   = 0.0f;
    p->boost      = 0.0f;
    set_yaw(p->turn->local.m, 0.0f, 0.0f, p->scale);
    show(p->hud, true);
    show(p->damage_label, p->lives == 1);
    play(p->engine_sound);
}

static void highway_player_update(Node *self, HighwayPlayer *p, float dt)
{
    float *m = self->local.m;
    float  velocity[3];

    g_police.driving = false;
    if (rr_action_pressed(p->act_cancel) && p->title_scene != NULL) {
        rr_change_scene(p->title_scene);
        return;
    }
    if (p->v.crashed) {
        if (p->lives <= 0) {
            if (rr_action_pressed(p->act_start) && p->title_scene != NULL) {
                rr_change_scene(p->title_scene);
            }
            return;
        }
        if (p->wreck_node != NULL && !(p->wreck_node->flags & RR_NODE_FLAG_FREE)) {
            /* Following the wreck: the camera follows this node. */
            p->wreck.time += dt;
            self->local.m[9]  = p->wreck_node->world.m[9];
            self->local.m[11] = p->wreck_node->world.m[11];
        } else {
            wreck_update(self, p->turn, &p->v, &p->wreck, p->scale, dt);
        }
        if (p->wreck.time >= 5.0f) {
            player_respawn(self, p);
        }
        player_show(p);
        return;
    }
    player_drive(p, dt);

    /* Along the car's heading, plus the push of hits, which fades. */
    velocity[0] = sinf(p->v.yaw * DEG2RAD) * p->v.speed + p->shove[0];
    velocity[1] = 0.0f;
    velocity[2] = -cosf(p->v.yaw * DEG2RAD) * p->v.speed;
    p->shove[0] = move_towards(p->shove[0], 0.0f, 20.0f * dt);
    set_yaw(p->turn->local.m, p->v.yaw, 0.0f, p->scale);
    rr_body_move_and_slide(self, velocity, dt);
    m[10] = RIDE_HEIGHT;
    g_police.driving     = true;
    g_police.x           = m[9];
    g_police.along       = -m[11];
    g_police.half_width  = p->v.half_width;
    g_police.half_length = p->v.half_length;
    memcpy(g_police.velocity, velocity, sizeof(g_police.velocity));
    {
        const bool wall = rr_body_is_on_wall(self);
        if (wall && g_road.known) {
            player_rail(self, p, !p->on_wall, dt);
        }
        p->on_wall = wall;
    }
    /* The lateral speed other cars see. */
    p->v.lateral = velocity[0];
    if (p->engine_sound != NULL) {
        rr_audio_set_pitch_scale(p->engine_sound, clampf_(p->v.speed / 100.0f, 0.5f, 5.0f) * 2.0f);
    }
    player_show(p);
}

/* Connected to the `area_entered` signal of the Hitbox: traffic wrecks the car. */
static void highway_player_on_hit(Node *self, HighwayPlayer *p, Node *area)
{
    if (area != NULL && !p->v.crashed && rr_node_has_tag(area, "traffic")) {
        Vehicle *v = vehicle_of(area);
        if (v != NULL && !v->crashed) {
            vehicle_collide(&p->v, v);
            v->crashed = true; /* the traffic car wrecks too */
        }
        player_crash(self, p);
    }
}

/* Connected to the `area_exited` signal of NearMiss: traffic passed close by. */
static void highway_player_on_near_miss(Node *self, HighwayPlayer *p, Node *area)
{
    Vehicle *v = vehicle_of(area);
    (void)self;
    if (area != NULL && v != NULL && !p->v.crashed && !v->crashed && rr_node_has_tag(area, "traffic")) {
        player_add_boost(p, 0.1f);
        p->score += 25;
        play(p->near_miss_sound);
    }
}

COMPONENT_BEGIN(HighwayPlayer, "HighwayPlayer")
    EXPORT_FLOAT_AS("half_width", v.half_width)
    EXPORT_FLOAT_AS("half_length", v.half_length)
    EXPORT_INT_AS("wreck_kind", v.wreck_kind)
    EXPORT_FLOAT(max_speed)
    EXPORT_FLOAT(boost_speed)
    EXPORT_FLOAT(accel_time)
    EXPORT_FLOAT(turn_rate)
    EXPORT_INT(lives)
    EXPORT_FLOAT(wall_crash_angle)
    EXPORT_FLOAT(wall_speed_loss)
    EXPORT_NODE(turn)
    EXPORT_NODE(speed_label)
    EXPORT_NODE(score_label)
    EXPORT_NODE(takedown_label)
    EXPORT_NODE(boost_bar)
    EXPORT_NODE(hud)
    EXPORT_NODE(damage_label)
    EXPORT_NODE(game_over)
    EXPORT_NODE(result_label)
    EXPORT_NODE(engine_sound)
    EXPORT_NODE(crash_sound)
    EXPORT_NODE(boost_sound)
    EXPORT_NODE(takedown_sound)
    EXPORT_NODE(near_miss_sound)
    EXPORT_NODE(impact_sound)
    EXPORT_SCENE(title_scene)
    ON_READY(highway_player_ready)
    ON_UPDATE(highway_player_update)
    ON_SIGNAL_NODE("_on_hitbox_area_entered", highway_player_on_hit)
    ON_SIGNAL_NODE("_on_near_miss_area_exited", highway_player_on_near_miss)
COMPONENT_END()

/* --- HighwayCar (traffic) --------------------------------------------------------------- */

typedef struct {
    Vehicle v;      /* must come first */
    float   cruise; /* exported: its own speed */
    float   accel;  /* exported */
    float   brake;  /* exported */

    float scale[3];
    float lane_x, lane_cooldown;
    Wreck wreck;
} HighwayCar;

static void highway_car_ready(Node *self, HighwayCar *c)
{
    memset(&c->wreck, 0, sizeof(c->wreck));
    get_scale(self->local.m, c->scale);
    c->v.speed   = c->cruise;
    c->v.crashed = false;
    c->v.hit     = false;
    c->v.placed  = false; /* until HighwayRoad puts it on the road */
}

static void highway_car_change_lane(Node *self, HighwayCar *c, const LaneScan *current, float safe_gap)
{
    const int   idx  = nearest_lane(c->lane_x);
    const float side = g_road.lanes[idx] > g_road.center ? 1.0f : -1.0f;
    int         n;

    for (n = idx - 1; n <= idx + 1; n += 2) {
        LaneScan scan;
        if (n < 0 || n >= g_road.lane_count || (g_road.lanes[n] > g_road.center ? 1.0f : -1.0f) != side) {
            continue; /* traffic keeps to its side of the road */
        }
        scan = scan_lane(&c->v, self, g_road.lanes[n], true, 250.0f);
        if (!safe_to_enter(&scan, safe_gap)) {
            continue;
        }
        if (scan.ttc_ahead > current->ttc_ahead + 0.5f || scan.gap_ahead > current->gap_ahead + 10.0f ||
            current->ttc_behind < 1.5f) {
            c->lane_x        = g_road.lanes[n];
            c->lane_cooldown = frand(2.0f, 3.0f);
            return;
        }
    }
}

static void highway_car_update(Node *self, HighwayCar *c, float dt)
{
    LaneScan scan;
    float    desired, safe_gap, rate;
    bool     fear_ahead, fear_behind, held_up;

    if (!c->v.placed) {
        return;
    }
    if (c->v.crashed) {
        if (!c->wreck.tried) {
            wreck_start(&c->wreck, &c->v, 3.0f);
            c->wreck.tried = true;
            /* A flying wreck takes the car's place, if one is free. */
            if (spawn_wreck(self, &c->v, 0.0f) != NULL) {
                rr_queue_free(self);
                return;
            }
        }
        wreck_update(self, self, &c->v, &c->wreck, c->scale, dt);
        if (c->wreck.time > 5.0f) {
            rr_queue_free(self);
        }
        return;
    }
    if (c->lane_x == 0.0f) {
        c->lane_x        = g_road.lane_count > 0 ? g_road.lanes[nearest_lane(self->local.m[9])] : self->local.m[9];
        c->lane_cooldown = frand(0.5f, 1.5f);
    }
    desired  = c->cruise;
    scan     = scan_lane(&c->v, self, c->lane_x, false, 250.0f);
    safe_gap = 6.0f + 0.8f * c->v.speed;
    if (scan.gap_ahead < safe_gap * 1.5f) {
        desired = fminf(desired, fmaxf(0.0f, scan.speed_ahead + (scan.gap_ahead - safe_gap) * 0.8f));
    }
    fear_ahead  = scan.ttc_ahead < 3.0f;
    fear_behind = scan.ttc_behind < 1.5f;
    held_up     = desired < c->cruise - 5.0f;
    c->lane_cooldown -= dt;
    if ((fear_ahead || fear_behind || held_up) &&
        (c->lane_cooldown <= 0.0f || scan.ttc_ahead < 1.2f || fear_behind)) {
        highway_car_change_lane(self, c, &scan, safe_gap);
    }
    rate       = desired > c->v.speed ? c->accel : (scan.ttc_ahead < 1.0f ? c->brake * 2.5f : c->brake);
    c->v.speed = move_towards(c->v.speed, desired, rate * dt);
    c->v.target_x = c->lane_x;
    steer(self, &c->v, c->lane_x, 3.5f, 8.0f, c->scale, dt);
}

/* Connected to `area_entered`: another car (traffic or a rival) wrecks it. */
static void highway_car_on_area(Node *self, HighwayCar *c, Node *area)
{
    Vehicle *other = area != NULL ? vehicle_of(area) : NULL;
    (void)self;
    if (other != NULL && c->v.placed) {
        vehicle_collide(&c->v, other);
        c->v.crashed = true;
    }
}

/* Connected to `body_entered`: the police car. */
static void highway_car_on_body(Node *self, HighwayCar *c, Node *body)
{
    Node          *player_node = NULL;
    HighwayPlayer *p           = player_of(self->scene, &player_node);
    if (body != NULL && c->v.placed && body == player_node && p != NULL) {
        vehicle_collide(&c->v, &p->v);
        c->v.crashed = true;
        player_crash(player_node, p);
    }
}

COMPONENT_BEGIN(HighwayCar, "HighwayCar")
    EXPORT_FLOAT_AS("half_width", v.half_width)
    EXPORT_FLOAT_AS("half_length", v.half_length)
    EXPORT_INT_AS("wreck_kind", v.wreck_kind)
    EXPORT_FLOAT(cruise)
    EXPORT_FLOAT(accel)
    EXPORT_FLOAT(brake)
    ON_READY(highway_car_ready)
    ON_UPDATE(highway_car_update)
    ON_SIGNAL_NODE("_on_area_entered", highway_car_on_area)
    ON_SIGNAL_NODE("_on_body_entered", highway_car_on_body)
COMPONENT_END()

/* --- HighwayRival ----------------------------------------------------------------------- */

typedef struct {
    Vehicle v;              /* must come first */
    float   top_speed;      /* exported */
    float   accel;          /* exported */
    float   brake;          /* exported */
    float   pass_margin;    /* exported: how much faster than the player it goes to pull past */
    float   flee_margin;    /* exported: how much slower than the player it flees */
    float   takedown_impact; /* exported: a hit this hard (relative speed) wrecks it at once */
    float   grind_time;     /* exported: seconds of grinding until a takedown */
    Node   *explosion;      /* exported: particle systems fired at a takedown */
    Node   *indicator;      /* exported: shown while the player is close */
    Node   *body;           /* exported: the body as the dummies look */
    Node   *paint;          /* exported: the body in the rivals' red, shown instead for real rivals */
    Node   *crash_sound;    /* exported */

    float scale[3];
    float cruise, lane_x, lane_cooldown;
    float damage;
    float time, last_player_hit, last_player_contact, rival_contact;
    bool  player_close, touching_player, dummy, taken_down;
    bool  contact_side; /* touching the police car side by side (else nose to tail) */
    int   rivals_touching;
    Wreck wreck;
} HighwayRival;

static void highway_rival_ready(Node *self, HighwayRival *r)
{
    memset(&r->wreck, 0, sizeof(r->wreck));
    get_scale(self->local.m, r->scale);
    r->cruise              = 70.0f * frand(0.9f, 1.1f);
    r->v.speed             = r->cruise;
    r->v.placed            = false;
    r->v.crashed           = false;
    r->v.hit               = false;
    r->damage              = 0.0f;
    r->time                = 0.0f;
    r->last_player_hit     = -10.0f;
    r->last_player_contact = -10.0f;
    r->rival_contact       = 0.0f;
    r->taken_down          = false;
    r->touching_player     = false;
    r->rivals_touching     = 0;
    r->lane_x              = 0.0f;
    r->dummy               = (rand() & 1) != 0;
    /* Unity gave the real rivals a red texture; here they show a red copy of the body. */
    show(r->body, r->dummy || r->paint == NULL);
    show(r->paint, !r->dummy);
    /* Its wreck in the same paint. */
    r->v.wreck_kind = r->dummy || r->paint == NULL ? 2 : 4;
    show(r->indicator, false);
    if (r->explosion != NULL) {
        show(r->explosion, false);
    }
}

static bool rival_oncoming(const HighwayRival *r, float x)
{
    return (x - g_road.center) * r->v.dir < 0.0f;
}

/* The speed it wants: pass the player, keep alongside or flee (PursuitSpeed). */
static float rival_pursuit_speed(Node *self, HighwayRival *r)
{
    Node          *pn = NULL;
    HighwayPlayer *p  = player_of(self->scene, &pn);
    float          ahead, side, ps;

    if (p == NULL || p->v.crashed) {
        return r->cruise;
    }
    ps    = p->v.speed;
    ahead = (along_of(pn) - along_of(self)) * r->v.dir;
    side  = fabsf(pn->world.m[9] - self->world.m[9]);
    if (side > 15.0f || fabsf(ahead) > 150.0f) {
        return r->cruise;
    }
    if (ahead > 6.0f) {
        float k;
        if (!r->player_close) {
            return r->cruise;
        }
        k = clampf_((ahead - 6.0f) / 94.0f, 0.0f, 1.0f);
        return fmaxf(r->cruise, ps + r->pass_margin + (40.0f - r->pass_margin) * k);
    }
    if (ahead > -8.0f) {
        if (ps < 45.0f) {
            return r->cruise;
        }
        return side < 4.0f ? ps - 1.0f : ps + r->pass_margin;
    }
    if (-ahead < 60.0f) {
        return fmaxf(r->cruise, ps - r->flee_margin);
    }
    return r->cruise;
}

static float rival_lane_score(const HighwayRival *r, const LaneScan *s, float x)
{
    float score = fminf(s->ttc_ahead, 8.0f) + fminf(s->gap_ahead, 120.0f) / 40.0f;
    if (rival_oncoming(r, x)) {
        score -= 3.0f + fabsf(x - g_road.center) * 0.3f;
    }
    return score;
}

static void rival_choose_lane(Node *self, HighwayRival *r, const LaneScan *current, float safe_gap, float dt)
{
    const int idx    = nearest_lane(r->lane_x);
    const bool danger = current->ttc_ahead < (rival_oncoming(r, r->lane_x) ? 3.0f : 1.8f);
    float      best_score;
    int        n, best = idx;

    r->lane_cooldown -= dt;
    if (r->lane_cooldown > 0.0f && !danger) {
        return;
    }
    best_score = rival_lane_score(r, current, g_road.lanes[idx]);
    for (n = idx - 1; n <= idx + 1; n += 2) {
        LaneScan scan;
        float    score;
        if (n < 0 || n >= g_road.lane_count) {
            continue;
        }
        scan = scan_lane(&r->v, self, g_road.lanes[n], true, 400.0f);
        if (!safe_to_enter(&scan, safe_gap * 0.5f)) {
            continue;
        }
        score = rival_lane_score(r, &scan, g_road.lanes[n]);
        if (score > best_score + 0.75f) {
            best_score = score;
            best       = n;
        }
    }
    if (best != idx) {
        r->lane_x        = g_road.lanes[best];
        r->lane_cooldown = frand(0.8f, 1.4f);
    }
}

static void rival_takedown(Node *self, HighwayRival *r, int score)
{
    Node          *pn = NULL;
    HighwayPlayer *p  = player_of(self->scene, &pn);
    Node          *child = NULL;

    if (r->v.crashed) {
        return;
    }
    r->v.crashed  = true;
    r->taken_down = true;
    printf("highway: takedown\n");
    wreck_start(&r->wreck, &r->v, 8.0f);
    show(r->indicator, false);
    play(r->crash_sound);
    if (r->explosion != NULL) {
        show(r->explosion, true);
        if (r->explosion->type == RR_NODE_PARTICLES) {
            rr_particles_restart(r->explosion);
        }
        while ((child = rr_node_child(r->explosion, child)) != NULL) {
            if (child->type == RR_NODE_PARTICLES) {
                rr_particles_restart(child);
            }
        }
    }
    if (p != NULL) {
        p->score += score;
        player_add_boost(p, 0.25f);
        /* Wrecked within 5 seconds of a touch of the police car: a takedown. */
        if (r->time - r->last_player_contact < 5.0f) {
            player_takedown(p, 500);
        }
    }
}

/* A hit by the police car: pushed sideways and along by how hard it was, damaged. */
static void rival_player_impact(Node *self, HighwayRival *r, Node *pn, HighwayPlayer *p)
{
    const float dx = self->world.m[9] - pn->world.m[9];
    const float da = along_of(self) - along_of(pn);
    float       nx = 0.0f, na = 0.0f, impact, pvx, pva, mvx, mva;

    /* The side of the boxes that touch: whichever overlaps less. */
    if (fabsf(dx) / (r->v.half_width + p->v.half_width) > fabsf(da) / (r->v.half_length + p->v.half_length)) {
        nx = dx > 0.0f ? 1.0f : -1.0f;
    } else {
        na = da > 0.0f ? 1.0f : -1.0f;
    }
    pvx    = sinf(p->v.yaw * DEG2RAD) * p->v.speed;
    pva    = cosf(p->v.yaw * DEG2RAD) * p->v.speed;
    mvx    = r->v.lateral;
    mva    = r->v.speed * r->v.dir;
    impact = fmaxf(0.0f, (pvx - mvx) * nx + (pva - mva) * na);
    r->last_player_hit     = r->time;
    r->last_player_contact = r->time;
    if (impact < 1.0f) {
        return;
    }
    r->v.lateral += nx * impact * 0.9f;
    r->v.speed = clampf_(r->v.speed + na * r->v.dir * impact * 0.6f, 0.0f, r->top_speed);
    r->damage += impact / r->takedown_impact;    player_rival_contact(pn, p, -nx * impact * 0.25f, impact * 0.15f, clampf_(impact / 40.0f, 0.0f, 1.0f));
    play(p->impact_sound);
    if (r->damage >= 1.0f) {
        rival_takedown(self, r, 500);
    }
}

/* Touching the police car for a while: grinding (Grind). */
static void rival_grind(Node *self, HighwayRival *r, Node *pn, HighwayPlayer *p, float dt)
{
    const float nx = self->world.m[9] > pn->world.m[9] ? 1.0f : -1.0f;
    r->v.speed   = move_towards(r->v.speed, p->v.speed, 30.0f * dt);
    r->v.lateral = move_towards(r->v.lateral, p->v.lateral + nx * 2.0f, 40.0f * dt);
    r->last_player_hit     = r->time;
    r->last_player_contact = r->time;
    r->damage += dt / r->grind_time;
    player_add_boost(p, 0.1f * dt);
    player_rival_contact(pn, p, 0.0f, 0.0f, 0.15f);
    if (r->damage >= 1.0f) {
        rival_takedown(self, r, 500);
    }
}

/* At a rail: bounced back, or taken down when the player pushed it there (RailContact). */
static void rival_rail(Node *self, HighwayRival *r)
{
    const float x = self->world.m[9];
    float       n, into;

    if (x - r->v.half_width < g_road.rail_left) {
        n = 1.0f;
    } else if (x + r->v.half_width > g_road.rail_right) {
        n = -1.0f;
    } else {
        return;
    }
    into = -r->v.lateral * n;
    if (into <= 0.0f) {
        return;
    }
    if (r->time - r->last_player_hit < 1.5f && into > 6.0f) {
        rival_takedown(self, r, 750);
        return;
    }
    r->v.lateral = n * fmaxf(2.0f, into * 0.3f);
    r->v.speed   = fmaxf(0.0f, r->v.speed - into * 0.5f);
}

/* Where the boxes of a rival and the police car touch: the axis on which they overlap
 * least (nx sideways or na along the road, toward the rival) and by how much. */
typedef struct {
    float nx, na, depth;
} Contact;

/* Unity's police car and rivals were rigid bodies (both of mass 100): they collided as
 * boxes. True while the boxes overlap or nearly touch. */
static bool rival_touch(const Node *self, HighwayRival *r, const Node *pn, const HighwayPlayer *p,
                        Contact *c)
{
    RRMat34     me, him;
    float       dx, da, wx, wa, ox, oa;
    bool        side;

    world_now(self, &me);
    world_now(pn, &him);
    dx = me.m[9] - him.m[9];
    da = him.m[11] - me.m[11]; /* along the road (-Z) */
    wx = r->v.half_width + p->v.half_width;
    wa = r->v.half_length + p->v.half_length;
    ox = wx - fabsf(dx);
    oa = wa - fabsf(da);
    if (ox < -0.1f || oa < -0.1f) {
        return false;
    }
    if (r->touching_player) {
        /* Still together: the side they touch on stays. */
        side = r->contact_side;
    } else {
        /* They have just met: the side is the axis on which they began to overlap last,
         * by how fast they come together on each (a fast car is deep in after a frame). */
        const float pvx = sinf(p->v.yaw * DEG2RAD) * p->v.speed;
        const float pva = cosf(p->v.yaw * DEG2RAD) * p->v.speed;
        const float tx  = fmaxf(ox, 0.0f) / fmaxf(fabsf(r->v.lateral - pvx), 0.5f);
        const float ta  = fmaxf(oa, 0.0f) / fmaxf(fabsf(r->v.speed * r->v.dir - pva), 0.5f);
        side = tx < ta;
    }
    if (side) {
        c->nx    = dx >= 0.0f ? 1.0f : -1.0f;
        c->na    = 0.0f;
        c->depth = ox;
    } else {
        c->nx    = 0.0f;
        c->na    = da >= 0.0f ? 1.0f : -1.0f;
        c->depth = oa;
    }
    r->contact_side = side;
    return true;
}

/* Keeps the boxes apart: the rival is moved out of the police car, and where they move
 * into each other both come out with the mean of their speeds on that axis (equal masses),
 * so the police car shoves a rival ahead of it and is slowed by it. */
static void rival_push_apart(Node *self, HighwayRival *r, HighwayPlayer *p, const Contact *c)
{
    float      *m   = self->local.m;
    const float pvx = sinf(p->v.yaw * DEG2RAD) * p->v.speed;
    const float pva = cosf(p->v.yaw * DEG2RAD) * p->v.speed;

    if (c->depth > 0.0f) {
        m[9] += c->nx * c->depth;
        m[11] -= c->na * c->depth;
    }
    if (c->nx != 0.0f) {
        if ((r->v.lateral - pvx) * c->nx < 0.0f) {
            const float mean = 0.5f * (r->v.lateral + pvx);
            r->v.lateral = mean;
            p->shove[0] += mean - pvx;
        }
    } else {
        const float ra = r->v.speed * r->v.dir;
        if ((ra - pva) * c->na < 0.0f) {
            /* With a little bounce, so that they come apart again as Unity's bodies did:
             * a car rammed from behind is not ground down, only hit. */
            const float apart  = 0.15f * fabsf(ra - pva) * c->na;
            const float mean   = 0.5f * (ra + pva);
            const float player = mean - apart;
            r->v.speed = fmaxf(0.0f, (mean + apart) * r->v.dir);
            if (pva > player) {
                player_lose_speed(p, pva - player);
            } else {
                p->v.speed += player - pva; /* rammed from behind: shoved ahead */
            }
        }
    }
}

static void highway_rival_update(Node *self, HighwayRival *r, float dt)
{
    Node          *pn = NULL;
    HighwayPlayer *p  = player_of(self->scene, &pn);
    LaneScan       scan;
    float          desired, safe_gap, rate, dist, vdx;

    if (!r->v.placed) {
        return;
    }
    r->time += dt;
    if (r->v.crashed) {
        if (!r->wreck.tried) {
            r->wreck.tried = true;
            /* A flying wreck takes the rival's place, if one is free; thrown up when it
             * was taken down. */
            if (spawn_wreck(self, &r->v, r->taken_down ? 7.0f : 0.0f) != NULL) {
                rr_queue_free(self);
                return;
            }
        }
        wreck_update(self, self, &r->v, &r->wreck, r->scale, dt);
        if (r->wreck.time > 5.0f) {
            rr_queue_free(self);
        }
        return;
    }
    if (r->lane_x == 0.0f) {
        r->lane_x        = g_road.lane_count > 0 ? g_road.lanes[nearest_lane(self->local.m[9])] : self->local.m[9];
        r->lane_cooldown = frand(0.0f, 0.5f);
    }
    if (p == NULL) {
        return;
    }
    /* TrackPlayer: close enough to show the arrow; too far to matter any more. */
    vdx  = pn->world.m[9] - self->world.m[9];
    dist = sqrtf(vdx * vdx + (along_of(pn) - along_of(self)) * (along_of(pn) - along_of(self)));
    if (dist < 25.0f && fabsf(vdx) < 15.0f && !p->v.crashed) {
        show(r->indicator, true);
        r->player_close = true;
    } else if (dist > 100.0f) {
        r->player_close = false;
    }
    if (dist > 250.0f) {
        rr_queue_free(self);
        return;
    }
    if (r->rivals_touching > 0) {
        /* Spawned on top of another one. */
        r->rival_contact += dt;
        if (r->rival_contact > 1.5f) {
            r->v.crashed = true;
            wreck_start(&r->wreck, &r->v, 2.0f);
            return;
        }
    }

    /* Drive. */
    desired  = fminf(rival_pursuit_speed(self, r), r->top_speed);
    scan     = scan_lane(&r->v, self, r->lane_x, true, 400.0f);
    safe_gap = 8.0f + 0.6f * r->v.speed;
    if (scan.gap_ahead < safe_gap * 1.5f && scan.speed_ahead > 0.0f) {
        desired = fminf(desired, fmaxf(0.0f, scan.speed_ahead + (scan.gap_ahead - safe_gap) * 1.5f));
    } else if (scan.ttc_ahead < 1.0f) {
        desired = 0.0f;
    }
    if (rival_oncoming(r, r->lane_x)) {
        /* In the oncoming lanes: never ahead of what it would have to slip in behind. */
        const int home = nearest_lane(r->lane_x) + (g_road.center > r->lane_x ? 1 : -1);
        if (home >= 0 && home < g_road.lane_count) {
            const LaneScan back = scan_lane(&r->v, self, g_road.lanes[home], true, 400.0f);
            if (back.alongside) {
                desired = fminf(desired, back.speed_alongside - 15.0f);
            }
        }
    }
    if (r->time - r->last_player_hit < 1.5f) {
        r->lane_x = g_road.lanes[nearest_lane(self->world.m[9])];
    } else {
        rival_choose_lane(self, r, &scan, safe_gap, dt);
    }
    rate       = desired > r->v.speed ? r->accel : (scan.ttc_ahead < 1.0f ? r->brake * 2.0f : r->brake);
    r->v.speed = move_towards(r->v.speed, desired, rate * dt);
    r->v.target_x = r->lane_x;
    steer(self, &r->v, r->lane_x, 7.0f, 14.0f, r->scale, dt);
    rival_rail(self, r);

    /* The police car: a hit when they meet (OnCollisionEnter), grinding while they stay
     * together (OnCollisionStay), and never through each other. */
    if (!r->v.crashed && !p->v.crashed) {
        Contact    c;
        const bool touch = rival_touch(self, r, pn, p, &c);
        if (touch && !r->touching_player) {
            if (r->dummy) {
                /* The blue ones break at the first touch, with a jolt like a crash. */
                play(p->impact_sound);
                r->last_player_contact = r->time;
                rr_input_vibrate(1.0f, 1.0f, 0.4f);
                camera_shake(self->scene, 1.0f);
                rival_takedown(self, r, 100);
            } else {
                rival_player_impact(self, r, pn, p);
            }
        } else if (touch && r->contact_side) {
            /* Grind: side by side and pressed together (nose to tail they bounce apart). */
            rival_grind(self, r, pn, p, dt);
        }
        r->touching_player = touch;
        if (touch && !r->v.crashed) {
            rival_push_apart(self, r, p, &c);
        }
    }
}

/* Connected to `area_entered`: traffic wrecks it, another rival is counted. */
static void highway_rival_on_area(Node *self, HighwayRival *r, Node *area)
{
    if (area == NULL || !r->v.placed || r->v.crashed) {
        return;
    }
    if (rr_node_has_tag(area, "traffic")) {
        Vehicle *v = vehicle_of(area);
        if (v != NULL) {
            vehicle_collide(&r->v, v);
            v->crashed = true;
        }
        r->v.crashed = true;
        wreck_start(&r->wreck, &r->v, 3.0f);
        play(r->crash_sound);
        /* A takedown if the police car pushed it there. */
        if (r->time - r->last_player_contact < 5.0f) {
            HighwayPlayer *p = player_of(self->scene, NULL);
            if (p != NULL) {
                player_takedown(p, 500);
            }
        }
    } else if (rr_node_has_tag(area, "rival")) {
        r->rivals_touching++;
    }
}

static void highway_rival_on_area_exit(Node *self, HighwayRival *r, Node *area)
{
    (void)self;
    if (area != NULL && rr_node_has_tag(area, "rival") && r->rivals_touching > 0) {
        r->rivals_touching--;
        r->rival_contact = 0.0f;
    }
}


COMPONENT_BEGIN(HighwayRival, "HighwayRival")
    EXPORT_FLOAT_AS("half_width", v.half_width)
    EXPORT_FLOAT_AS("half_length", v.half_length)
    EXPORT_INT_AS("wreck_kind", v.wreck_kind)
    EXPORT_FLOAT(top_speed)
    EXPORT_FLOAT(accel)
    EXPORT_FLOAT(brake)
    EXPORT_FLOAT(pass_margin)
    EXPORT_FLOAT(flee_margin)
    EXPORT_FLOAT(takedown_impact)
    EXPORT_FLOAT(grind_time)
    EXPORT_NODE(explosion)
    EXPORT_NODE(indicator)
    EXPORT_NODE(body)
    EXPORT_NODE(paint)
    EXPORT_NODE(crash_sound)
    ON_READY(highway_rival_ready)
    ON_UPDATE(highway_rival_update)
    ON_SIGNAL_NODE("_on_area_entered", highway_rival_on_area)
    ON_SIGNAL_NODE("_on_area_exited", highway_rival_on_area_exit)
COMPONENT_END()

/* --- HighwayRoad ------------------------------------------------------------------------ */

#define MAX_SEGMENTS 8
#define MAX_SPAWNS   32

typedef struct {
    Node *player;          /* exported */
    Node *first;           /* exported: Marker3D where the first segment's root goes */
    float segment_length;  /* exported: metres from one segment's root to the next */
    int32_t ahead;         /* exported: segments kept in front of the player's */
    float rail_half_width; /* exported: half the width of a rail */
    const RRPackedScene *road;      /* exported */
    int32_t              road_max;  /* exported */
    const RRPackedScene *car;       /* exported */
    int32_t              car_max;   /* exported */
    const RRPackedScene *truck;     /* exported */
    int32_t              truck_max; /* exported */
    const RRPackedScene *rival;     /* exported */
    int32_t              rival_max; /* exported */
    const RRPackedScene *wreck_car;       /* exported: RigidBody3D wrecks, by wreck_kind */
    int32_t              wreck_car_max;   /* exported */
    const RRPackedScene *wreck_truck;     /* exported */
    int32_t              wreck_truck_max; /* exported */
    const RRPackedScene *wreck_rival;     /* exported */
    int32_t              wreck_rival_max; /* exported */
    const RRPackedScene *wreck_police;    /* exported */
    int32_t              wreck_police_max; /* exported */
    const RRPackedScene *wreck_rival_red;     /* exported: a real rival's wreck, in its red */
    int32_t              wreck_rival_red_max; /* exported */

    Node *segments[MAX_SEGMENTS];
    float starts[MAX_SEGMENTS]; /* along, of each segment's root */
    int   count;
    float next_along;
    float x, y;
    bool  started;
} HighwayRoad;

/* The markers below a segment's node "TrafficSpawn" (all but the last, as in Unity). */
static int segment_spawns(Node *segment, Node **out)
{
    Node *holder = NULL, *child = NULL;
    int   n = 0;

    while ((holder = rr_node_child(segment, holder)) != NULL) {
        if (strcmp(holder->name, "TrafficSpawn") == 0) {
            break;
        }
    }
    if (holder == NULL) {
        return 0;
    }
    while ((child = rr_node_child(holder, child)) != NULL && n < MAX_SPAWNS) {
        out[n++] = child;
    }
    return n > 0 ? n - 1 : 0;
}

/* The lanes are where the spawn markers stand across the road; the rails are the two
 * nodes in the group "guardrail". */
static void learn_road(Node *segment)
{
    Node *spawns[MAX_SPAWNS], *n = NULL;
    int   count = segment_spawns(segment, spawns), i, j;
    float lo = 1.0e9f, hi = -1.0e9f;

    g_road.lane_count = 0;
    for (i = 0; i < count + 1 && i < MAX_SPAWNS; i++) {
        RRMat34 w;
        float   x;
        world_now(spawns[i], &w);
        x = w.m[9];
        bool  seen = false;
        for (j = 0; j < g_road.lane_count; j++) {
            seen = seen || fabsf(g_road.lanes[j] - x) < 1.0f;
        }
        if (!seen && g_road.lane_count < MAX_LANES) {
            g_road.lanes[g_road.lane_count++] = x;
        }
    }
    /* sort */
    for (i = 1; i < g_road.lane_count; i++) {
        for (j = i; j > 0 && g_road.lanes[j - 1] > g_road.lanes[j]; j--) {
            float t = g_road.lanes[j];
            g_road.lanes[j]     = g_road.lanes[j - 1];
            g_road.lanes[j - 1] = t;
        }
    }
    if (g_road.lane_count == 0) {
        return;
    }
    g_road.center = (g_road.lanes[0] + g_road.lanes[g_road.lane_count - 1]) * 0.5f;
    while ((n = rr_scene_find_tagged(segment->scene, "guardrail", n)) != NULL) {
        RRMat34 w;
        float   x;
        world_now(n, &w);
        x = w.m[9];
        if (x < lo) {
            lo = x;
        }
        if (x > hi) {
            hi = x;
        }
    }
    g_road.rail_left  = lo;
    g_road.rail_right = hi;
    g_road.known      = true;
}

static Node *spawn_vehicle(const RRPackedScene *scene, Node *marker, float dir)
{
    Node    *node;
    Vehicle *v;
    if (!place_is_free(marker->scene, marker->world.m[9], along_of(marker), 10.0f)) {
        return NULL;
    }
    node = rr_instantiate(scene);
    if (node == NULL) {
        return NULL;
    }
    v = vehicle_of(node);
    if (v != NULL) {
        vehicle_place(node, v, marker->world.m[9], along_of(marker), dir);
        /* The world transform is worked out at the end of the frame; scans this frame
         * need it now. */
        memcpy(node->world.m, node->local.m, sizeof(node->world.m));
    }
    return node;
}

/* Traffic and rivals on a new segment (TrafficSpawn.Start and SpawnManager.Start). */
static void populate(HighwayRoad *road, Node *segment)
{
    Node *spawns[MAX_SPAWNS];
    bool  used[MAX_SPAWNS] = { false };
    int   count = segment_spawns(segment, spawns), i, round, rounds;

    for (i = 0; i < count; i++) {
        /* Facing along the road (+1) or against it: the marker's -Z points the way. */
        const float dir = -spawns[i]->world.m[8] >= 0.0f ? 1.0f : -1.0f;
        const int   pick = (int)frand(-20.0f, 2.0f);
        if (pick >= 0 && pick < 2) {
            if (spawn_vehicle(pick == 0 ? road->truck : road->car, spawns[i], dir) != NULL) {
                used[i] = true;
            }
        }
    }
    rounds = 2 + rand() % 3;
    for (round = 0; round < rounds; round++) {
        for (i = 0; i < count; i++) {
            if (!used[i] && (int)frand(-10.0f, 2.0f) >= 0) {
                /* Rivals always face along the road, also in the oncoming lanes. */
                if (spawn_vehicle(road->rival, spawns[i], 1.0f) != NULL) {
                    used[i] = true;
                }
            }
        }
    }
}

static bool add_segment(HighwayRoad *road)
{
    Node *seg;
    if (road->count >= MAX_SEGMENTS) {
        return false;
    }
    seg = rr_instantiate(road->road);
    if (seg == NULL) {
        return false;
    }
    seg->local.m[9]  = road->x;
    seg->local.m[10] = road->y;
    seg->local.m[11] = -road->next_along;
    world_now(seg, &seg->world);
    road->segments[road->count] = seg;
    road->starts[road->count]   = road->next_along;
    road->count++;
    road->next_along += road->segment_length;
    return true;
}

static void highway_road_update(Node *self, HighwayRoad *road, float dt)
{
    float along;
    int   i;
    (void)dt;

    if (road->player == NULL || road->first == NULL) {
        return;
    }
    if (!road->started) {
        road->started    = true;
        g_police.driving = false; /* until a police car says it drives (not in the title) */
        g_wrecks[0] = road->wreck_car;
        g_wrecks[1] = road->wreck_truck;
        g_wrecks[2] = road->wreck_rival;
        g_wrecks[3] = road->wreck_police;
        g_wrecks[4] = road->wreck_rival_red;
        road->x          = road->first->world.m[9];
        road->y          = road->first->world.m[10];
        road->next_along = along_of(road->first);
        for (i = 0; i < road->ahead + 1; i++) {
            if (add_segment(road)) {
                if (i == 0) {
                    learn_road(road->segments[0]);
                }
            }
        }
        /* The world transforms of the new nodes are needed for the spawn markers. */
        for (i = 0; i < road->count; i++) {
            Node *child = NULL;
            while ((child = rr_node_child(road->segments[i], child)) != NULL) {
                world_now(child, &child->world);
            }
        }
        for (i = 0; i < road->count; i++) {
            Node *spawns[MAX_SPAWNS];
            int   k, n = segment_spawns(road->segments[i], spawns);
            for (k = 0; k < n; k++) {
                world_now(spawns[k], &spawns[k]->world);
            }
            populate(road, road->segments[i]);
        }
        if (!g_road.known) {
            learn_road(road->segments[0]);
        }
        return;
    }
    along = along_of(road->player);
    /* A segment the player has left by 20 m goes; a new one comes in front. */
    if (road->count > 0 && along > road->starts[0] + road->segment_length + 20.0f) {
        Node *spawns[MAX_SPAWNS];
        int   k, n;
        rr_queue_free(road->segments[0]);
        for (i = 1; i < road->count; i++) {
            road->segments[i - 1] = road->segments[i];
            road->starts[i - 1]   = road->starts[i];
        }
        road->count--;
        if (add_segment(road)) {
            Node *seg = road->segments[road->count - 1];
            n = segment_spawns(seg, spawns);
            for (k = 0; k < n; k++) {
                world_now(spawns[k], &spawns[k]->world);
            }
            populate(road, seg);
        }
    }
    /* Traffic far behind the player, or left on a road that is gone, goes too. */
    {
        Node *n = NULL;
        while ((n = rr_scene_find_tagged(self->scene, "vehicle", n)) != NULL) {
            const Vehicle *v = vehicle_of(n);
            if (v != NULL && !v->is_player && v->placed && road->count > 0 &&
                (along_of(n) < road->starts[0] - 20.0f || along_of(n) > road->next_along + 50.0f)) {
                rr_queue_free(n);
            }
        }
    }
}

COMPONENT_BEGIN(HighwayRoad, "HighwayRoad")
    EXPORT_NODE(player)
    EXPORT_NODE(first)
    EXPORT_FLOAT(segment_length)
    EXPORT_INT(ahead)
    EXPORT_FLOAT(rail_half_width)
    EXPORT_PACKED_SCENE(road)
    EXPORT_INT(road_max)
    EXPORT_PACKED_SCENE(car)
    EXPORT_INT(car_max)
    EXPORT_PACKED_SCENE(truck)
    EXPORT_INT(truck_max)
    EXPORT_PACKED_SCENE(rival)
    EXPORT_INT(rival_max)
    EXPORT_PACKED_SCENE(wreck_car)
    EXPORT_INT(wreck_car_max)
    EXPORT_PACKED_SCENE(wreck_truck)
    EXPORT_INT(wreck_truck_max)
    EXPORT_PACKED_SCENE(wreck_rival)
    EXPORT_INT(wreck_rival_max)
    EXPORT_PACKED_SCENE(wreck_police)
    EXPORT_INT(wreck_police_max)
    EXPORT_PACKED_SCENE(wreck_rival_red)
    EXPORT_INT(wreck_rival_red_max)
    ON_UPDATE(highway_road_update)
COMPONENT_END()

/* --- TitleScreen ------------------------------------------------------------------------ */

typedef struct {
    Node       *prompt;     /* exported: blinks */
    const char *game_scene; /* exported */
    const char *menu_scene; /* exported: on ui_cancel */

    int   act_start, act_cancel;
    float time;
} TitleScreen;

static void title_screen_ready(Node *self, TitleScreen *t)
{
    t->act_start  = rr_action(self->scene, "start");
    t->act_cancel = rr_action(self->scene, "ui_cancel");
    t->time       = 0.0f;
}

static void title_screen_update(Node *self, TitleScreen *t, float dt)
{
    (void)self;
    t->time += dt;
    show(t->prompt, fmodf(t->time, 1.2f) < 0.8f);
    if (rr_action_pressed(t->act_start) && t->game_scene != NULL) {
        rr_change_scene(t->game_scene);
    } else if (rr_action_pressed(t->act_cancel) && t->menu_scene != NULL && t->menu_scene[0] != '\0') {
        rr_change_scene(t->menu_scene);
    }
}

COMPONENT_BEGIN(TitleScreen, "TitleScreen")
    EXPORT_NODE(prompt)
    EXPORT_SCENE(game_scene)
    EXPORT_SCENE(menu_scene)
    ON_READY(title_screen_ready)
    ON_UPDATE(title_screen_update)
COMPONENT_END()

/* --- ForwardDummy ----------------------------------------------------------------------- */

/* Drives straight on along its -Z: the camera car of the title. */
typedef struct {
    float speed; /* exported: metres per second */
} ForwardDummy;

static void forward_dummy_update(Node *self, ForwardDummy *d, float dt)
{
    float *m = self->local.m;
    float  len = sqrtf(m[6] * m[6] + m[7] * m[7] + m[8] * m[8]);
    int    i;
    if (len > 0.0f) {
        for (i = 0; i < 3; i++) {
            m[9 + i] -= m[6 + i] / len * d->speed * dt;
        }
    }
}

COMPONENT_BEGIN(ForwardDummy, "ForwardDummy")
    EXPORT_FLOAT(speed)
    ON_UPDATE(forward_dummy_update)
COMPONENT_END()

/* --- Flare ------------------------------------------------------------------------------ */

/* The Flare script of the police lights (Unity's CRCV2 pack): the brightness of a lens
 * flare swings between start_range and end_range with sin(time * speed); start_first
 * picks the phase, so a red and a blue one take turns. Godot has no lens flares: here
 * the node is a glow (a billboard Sprite3D) whose size follows the brightness, full size
 * at end_range. */
typedef struct {
    float start_range; /* exported */
    float end_range;   /* exported */
    float speed;       /* exported: radians per second */
    bool  start_first; /* exported */

    float time;
    float basis[9];
} Flare;

static void flare_ready(Node *self, Flare *f)
{
    memcpy(f->basis, self->local.m, sizeof(f->basis));
    f->time = 0.0f;
}

static void flare_update(Node *self, Flare *f, float dt)
{
    const float range  = (f->end_range - f->start_range) * 0.5f;
    const float offset = range + f->start_range;
    const float bright = offset + sinf(f->time * f->speed) * (f->start_first ? range : -range);
    const float size   = f->end_range != 0.0f ? bright / f->end_range : 0.0f;
    int         i;

    f->time += dt;
    for (i = 0; i < 9; i++) {
        self->local.m[i] = f->basis[i] * fmaxf(size, 0.01f);
    }
}

COMPONENT_BEGIN(Flare, "Flare")
    EXPORT_FLOAT(start_range)
    EXPORT_FLOAT(end_range)
    EXPORT_FLOAT(speed)
    EXPORT_BOOL(start_first)
    ON_READY(flare_ready)
    ON_UPDATE(flare_update)
COMPONENT_END()
