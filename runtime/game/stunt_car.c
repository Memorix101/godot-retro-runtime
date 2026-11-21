/* Component of the stunt demo (scenes/stunt_track.tscn).
 * Placeholder script: components/stunt_car.gd.
 *
 * A car after the raycast vehicle that Godot's own VehicleBody3D is (which came from the
 * Bullet physics library): a body on four springs whose tyres each push at the place
 * where they touch the road. The node is a RigidBody3D with a box shape, which the
 * runtime's physics moves: it lets the body fall and turn, and keeps it out of the road,
 * the rails and other bodies. What the wheels and the air do to the car is worked out
 * here, every frame, and given to the body as its new velocity and spin.
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
    float       fall_limit;   /* exported: below this height the car is put back */
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
    float hit_ago;                 /* seconds since the body last met a rail or a wall */
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
 * `push` is a change of velocity for a car of mass 1. A push that is not to `steer` does
 * not turn the car around its up: that is for the springs, which push along a road that
 * may twist under the car (in a loop that goes to the side, the load lies on two
 * wheels across the car, and the springs would turn it out of the loop). Only the tyres
 * say where the car turns to. */
static void kick(StuntCar *c, float *v, const float *right, const float *arm, const float *push, bool steer)
{
    float twist[3], pitch, yaw, roll;
    int   k;

    cross(arm, push, twist);
    pitch = dot(twist, right) / INERTIA_PITCH;
    yaw   = steer ? dot(twist, c->up) / INERTIA_YAW : 0.0f;
    roll  = dot(twist, c->forward) / INERTIA_ROLL;
    for (k = 0; k < 3; k++) {
        v[k]       += push[k];
        c->spin[k] += right[k] * pitch + c->up[k] * yaw + c->forward[k] * roll;
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
    c->hit_ago     = 100.0f;
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

/* Takes from the body where the car is, how it stands, how it moves and how it turns. The
 * node is a RigidBody3D below a parent that does not move, so the way the node stands is
 * the way the car stands in the world: X to its right, Y up, -Z forward. It also notes
 * whether the body met something other than the road in the last step. */
static void body_read(StuntCar *c, Node *self, float *v, float *right, float dt)
{
    const float *m = self->local.m;
    float        normal[3];
    int          i, k;

    for (k = 0; k < 3; k++) {
        right[k]      = m[k];
        c->up[k]      = m[3 + k];
        c->forward[k] = -m[6 + k];
    }
    rr_rigid_body_get_linear_velocity(self, v);
    rr_rigid_body_get_angular_velocity(self, c->spin);
    /* A contact that pushes along the car's up is the road (or the roof on it); one that
     * pushes across is a rail or a wall. */
    c->hit_ago += dt;
    for (i = 0; i < rr_rigid_body_get_contact_count(self); i++) {
        float along;
        rr_rigid_body_get_contact_normal(self, i, normal);
        along = dot(normal, c->up);
        if (along < 0.7f && along > -0.7f) {
            c->hit_ago = 0.0f;
        }
    }
}

/* Puts the car back at the last checkpoint, standing still. The clock runs on. The node
 * is moved, and its body follows. */
static void put_back(Node *self, StuntCar *c)
{
    static const float still[3] = { 0.0f, 0.0f, 0.0f };
    float              right[3];
    int                k;

    cross(c->home_forward, c->home_up, right);
    for (k = 0; k < 3; k++) {
        self->local.m[k]     = right[k];
        self->local.m[3 + k] = c->home_up[k];
        self->local.m[6 + k] = -c->home_forward[k];
        self->local.m[9 + k] = c->home[k];
        c->cam_forward[k]    = c->home_forward[k];
        c->cam_up[k]         = c->home_up[k];
    }
    rr_rigid_body_set_linear_velocity(self, still);
    rr_rigid_body_set_angular_velocity(self, still);
    c->speed = c->slip = c->steering = c->stuck = c->loose = 0.0f;
    c->hit_ago  = 100.0f;
    c->drifting = false;
}

static void stunt_car_update(Node *self, StuntCar *c, float frame_dt)
{
    /* A long frame must not blow up the springs. */
    const float dt = frame_dt < 1.0f / 30.0f ? frame_dt : 1.0f / 30.0f;
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
    if (pos[1] < c->fall_limit || c->stuck > 1.3f || (rr_action_pressed(c->act_reset) && c->done <= 0.0f)) {
        put_back(self, c);
    }
    body_read(c, self, v, right, dt);

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
        kick(c, v, right, mount, at, false);
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

        /* The front wheels turn less the faster the car goes. In a drift they also turn a
         * part of the way towards where the car is going by themselves, as the wheels of
         * a real car do: that is steering against the slide. Outside of a drift they do
         * not, or the nose would follow every little slide and the car would wander (out
         * of the loop, for one). */
        lock = c->steering * (c->steer + (c->steer_fast - c->steer) * fast) + CASTER * going * c->loose;
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
            kick(c, v, right, lever, push, true);
        }
        if (c->hit_ago < 0.4f && along > 5.0f) {
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

    /* What the wheels and the air did goes to the body. The runtime's physics takes it
     * from there after this update: gravity, the move, the turn, and whatever the body
     * meets on its way, a rail, a wall, a crate, or the road when the car comes down on
     * its side or roof. */
    rr_rigid_body_set_linear_velocity(self, v);
    rr_rigid_body_set_angular_velocity(self, c->spin);
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
    EXPORT_FLOAT(fall_limit)
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
