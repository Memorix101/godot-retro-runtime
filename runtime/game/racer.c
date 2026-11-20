/* Components of the racing demo (scenes/racing.tscn, built by tools/make_racing.gd).
 * Placeholder scripts: components/race_car.gd and components/ai_driver.gd.
 *
 * The player's car is a CharacterBody3D with arcade handling: it speeds up and slows down
 * along its heading, turns faster the faster it goes (up to a limit), and its velocity
 * follows the heading with some delay, so it slides a little in fast corners. It slides
 * along the barriers, which are StaticBody3D nodes, and loses speed when it hits one.
 *
 * The opponents ride on PathFollow3D nodes along the middle of the track; each carries a
 * StaticBody3D, so the player can bump into them. Checkpoints are Area3D gates on
 * PathFollow3D nodes of the same path, in race order; a lap counts when the player has
 * passed them all and comes back to the first, which is the finish line.
 *
 * Race positions are compared by the distance along the path each car has covered since
 * the start of the path: the opponents know it exactly, the player's is worked out from
 * the last checkpoint passed. Race events are printed, which shows them on the serial
 * output of a console. */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "../core/rr_scene.h"

#define MAX_CHECKPOINTS 16
#define MAX_OPPONENTS   7

static float clampf(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static void set_yaw(Node *node, float angle)
{
    const float s = sinf(angle), c = cosf(angle);
    float      *m = node->local.m;
    m[0] = c;    m[1] = 0.0f; m[2] = -s;
    m[3] = 0.0f; m[4] = 1.0f; m[5] = 0.0f;
    m[6] = s;    m[7] = 0.0f; m[8] = c;
}

/* --- AiDriver ----------------------------------------------------------------------------- */

typedef struct {
    float speed;     /* exported: metres per second on average */
    float character; /* exported: 0..1, sets how its speed rises and falls around the lap */

    Node *self;
    float start;     /* progress at the start of the race */
    float travelled;
    float phase;
    bool  racing;
    float push;      /* -1..1, from how far it is ahead of or behind the player */
} AiDriver;

static void ai_driver_ready(Node *self, AiDriver *ai)
{
    ai->self  = self;
    ai->start = self->value;
}

static void ai_driver_update(Node *self, AiDriver *ai, float dt)
{
    float speed;

    if (!ai->racing) {
        return;
    }
    /* A little faster and slower around the lap, a little faster behind the player and
     * slower ahead of it, so the race stays close. */
    ai->phase += dt * (0.3f + 0.4f * ai->character);
    speed = ai->speed * (1.0f + 0.06f * sinf(ai->phase) - 0.08f * ai->push);
    self->value   += speed * dt;
    ai->travelled += speed * dt;
}

static float ai_driver_distance(const AiDriver *ai)
{
    return ai->start + ai->travelled;
}

COMPONENT_BEGIN(AiDriver, "AiDriver")
    EXPORT_FLOAT(speed)
    EXPORT_FLOAT(character)
    ON_READY(ai_driver_ready)
    ON_UPDATE(ai_driver_update)
COMPONENT_END()

/* --- RaceCar ------------------------------------------------------------------------------ */

typedef struct {
    float       max_speed;   /* exported, metres per second */
    float       accel;       /* exported */
    float       brake;       /* exported */
    float       steer;       /* exported: turn rate at speed, radians per second */
    float       grip;        /* exported: how fast the velocity follows the heading */
    int32_t     laps;        /* exported */
    float       start_distance; /* exported: where on the path the car starts */
    float       model_scale; /* exported: the scale the model has in the scene */
    Node       *model;       /* exported: the car's mesh, leans in corners */
    Node       *camera;      /* exported: follows behind the car */
    Node       *path;        /* exported: the Path3D along the middle of the track */
    Node       *checkpoints; /* exported: parent of the checkpoint PathFollow3D nodes, in order */
    Node       *opponents;   /* exported: parent of the opponents' PathFollow3D nodes */
    Node       *countdown;   /* exported: Timer that ticks once a second before the start */
    Node       *finish_timer; /* exported: Timer from the finish to the menu */
    Node       *lap_label, *time_label, *best_label, *pos_label, *speed_label, *message_label; /* exported */
    const char *menu_scene;  /* exported */

    int   act_accel, act_brake, act_left, act_right, act_cancel;
    int   count, cp_count, opp_count;
    Node *cps[MAX_CHECKPOINTS];
    AiDriver *opps[MAX_OPPONENTS];
    bool  racing, finished, counting;
    int   next_cp, last_cp, laps_done, place;
    float yaw, speed, velocity[3], start_pos[3], cam[3];
    float time, lap_start, last_lap, best, distance, length;
    char  lap_text[32], time_text[24], best_text[32], pos_text[32], speed_text[24], message_text[32];
} RaceCar;

typedef struct {
    uint32_t magic;
    float    best;
} RaceSave;

#define SAVE_NAME  "RR_RACING"
#define SAVE_MAGIC 0x52414331u

static void format_time(char *out, size_t size, const char *prefix, float seconds)
{
    const int hundredths = (int)(seconds * 100.0f + 0.5f);
    snprintf(out, size, "%s%d:%02d.%02d", prefix, hundredths / 6000, hundredths / 100 % 60, hundredths % 100);
}

static void show(Node *label, char *buffer, size_t size, const char *text)
{
    if (label != NULL) {
        snprintf(buffer, size, "%s", text);
        rr_node_set_text(label, buffer);
    }
}

static void race_car_ready(Node *self, RaceCar *car)
{
    Node    *n;
    RaceSave save;
    int      i;

    car->act_accel  = rr_action(self->scene, "accelerate");
    car->act_brake  = rr_action(self->scene, "brake");
    car->act_left   = rr_action(self->scene, "steer_left");
    car->act_right  = rr_action(self->scene, "steer_right");
    car->act_cancel = rr_action(self->scene, "ui_cancel");

    for (n = rr_node_child(car->checkpoints, NULL); n != NULL && car->cp_count < MAX_CHECKPOINTS;
         n = rr_node_child(car->checkpoints, n)) {
        car->cps[car->cp_count++] = n;
    }
    for (n = rr_node_child(car->opponents, NULL); n != NULL && car->opp_count < MAX_OPPONENTS;
         n = rr_node_child(car->opponents, n)) {
        AiDriver *ai = (AiDriver *)rr_node_component(n, "AiDriver");
        if (ai != NULL) {
            car->opps[car->opp_count++] = ai;
        }
    }
    car->length = rr_path_get_length(car->path);
    car->yaw    = atan2f(self->local.m[6], self->local.m[8]);
    car->place  = car->opp_count + 1;
    for (i = 0; i < 3; i++) {
        car->start_pos[i] = self->local.m[9 + i];
        car->cam[i]       = car->camera != NULL ? car->camera->local.m[9 + i] : 0.0f;
    }
    if (rr_save_read(SAVE_NAME, &save, sizeof(save)) && save.magic == SAVE_MAGIC) {
        car->best = save.best;
    }
    if (car->best > 0.0f) {
        format_time(car->best_text, sizeof(car->best_text), "BEST ", car->best);
    } else {
        snprintf(car->best_text, sizeof(car->best_text), "BEST -:--.--");
    }
    rr_node_set_text(car->best_label, car->best_text);
    snprintf(car->lap_text, sizeof(car->lap_text), "LAP 1/%d", (int)car->laps);
    rr_node_set_text(car->lap_label, car->lap_text);
    show(car->message_label, car->message_text, sizeof(car->message_text), "3");
    car->count = 3;
}

/* How far along the path the car has come since the path's start, laps included. */
static float race_distance(const Node *self, const RaceCar *car)
{
    const float *from = car->counting ? &car->cps[car->last_cp]->world.m[9] : car->start_pos;
    const float  base = car->counting ? (float)car->laps_done * car->length + car->cps[car->last_cp]->value
                                      : car->start_distance;
    const float  dx = self->world.m[9] - from[0], dz = self->world.m[11] - from[2];
    return base + sqrtf(dx * dx + dz * dz);
}

static void follow_camera(Node *self, RaceCar *car, float dt)
{
    const float *p = &self->local.m[9];
    const float  hx = sinf(car->yaw), hz = cosf(car->yaw);
    const float  want[3] = { p[0] - hx * 8.0f, p[1] + 3.2f, p[2] - hz * 8.0f };
    const float  follow = 1.0f - expf(-6.0f * dt);
    float        f[3], x[3], y[3], length;
    float       *m = car->camera->local.m;
    int          i;

    for (i = 0; i < 3; i++) {
        car->cam[i] += (want[i] - car->cam[i]) * follow;
    }
    /* Look a little above the car: the camera's -Z points at it. */
    f[0] = p[0] + hx * 3.0f - car->cam[0];
    f[1] = p[1] + 1.0f - car->cam[1];
    f[2] = p[2] + hz * 3.0f - car->cam[2];
    length = sqrtf(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
    for (i = 0; i < 3; i++) {
        f[i] /= length;
    }
    /* x = up x z with z = -f; y = z x x. */
    x[0] = -f[2];
    x[1] = 0.0f;
    x[2] = f[0];
    length = sqrtf(x[0] * x[0] + x[2] * x[2]);
    x[0] /= length;
    x[2] /= length;
    y[0] = -f[1] * x[2];
    y[1] = f[0] * x[2] - f[2] * x[0];
    y[2] = f[1] * x[0];
    for (i = 0; i < 3; i++) {
        m[i]     = x[i];
        m[3 + i] = y[i];
        m[6 + i] = -f[i];
        m[9 + i] = car->cam[i];
    }
}

static void race_car_update(Node *self, RaceCar *car, float dt)
{
    const bool  driving = car->racing && !car->finished;
    const float throttle = driving ? rr_action_strength(car->act_accel) : 0.0f;
    const float braking = driving ? rr_action_strength(car->act_brake) : 0.0f;
    const float steer = driving ? rr_action_strength(car->act_right) - rr_action_strength(car->act_left) : 0.0f;
    const float follow = 1.0f - expf(-car->grip * dt);
    float       hx, hz, turn;
    int         i, ahead = 0;

    if (rr_action_pressed(car->act_cancel)) {
        rr_change_scene(car->menu_scene);
        return;
    }

    /* Speed along the heading: throttle, brake (and reverse), and rolling to a stop. */
    car->speed += throttle * car->accel * dt;
    if (braking > 0.0f) {
        car->speed -= braking * (car->speed > 0.0f ? car->brake : car->accel * 0.5f) * dt;
    } else if (throttle == 0.0f) {
        const float roll = 6.0f * dt;
        car->speed = fabsf(car->speed) < roll ? 0.0f : car->speed - (car->speed > 0.0f ? roll : -roll);
    }
    car->speed = clampf(car->speed, -car->max_speed * 0.3f, car->max_speed);

    /* Steering: right turns the heading clockwise seen from above. */
    turn = steer * car->steer * clampf(fabsf(car->speed) / 12.0f, 0.0f, 1.0f);
    car->yaw -= (car->speed < 0.0f ? -turn : turn) * dt;
    hx = sinf(car->yaw);
    hz = cosf(car->yaw);

    /* The velocity follows the heading with some delay: the car slides a little. */
    car->velocity[0] += (hx * car->speed - car->velocity[0]) * follow;
    car->velocity[1]  = 0.0f;
    car->velocity[2] += (hz * car->speed - car->velocity[2]) * follow;
    rr_body_move_and_slide(self, car->velocity, dt);
    if (rr_body_is_on_wall(self)) {
        /* The barrier took away the part of the velocity that went into it: what is left
         * along the heading is the speed now, a glancing touch costs little, a head-on
         * hit almost all of it. Scraping along also rubs a little off. */
        const float along = car->velocity[0] * hx + car->velocity[2] * hz;
        if (car->speed > 0.0f && along < car->speed) {
            car->speed = along > 0.0f ? along : 0.0f;
        }
        car->speed *= 1.0f - 0.6f * dt;
    }
    set_yaw(self, car->yaw);
    if (car->model != NULL) {
        /* The body leans out of the corner a little. The model looks along its -Z, the
         * car drives along its +Z: the model is turned half way round. */
        const float lean = clampf(turn * car->speed * 0.004f, -0.08f, 0.08f);
        float      *m = car->model->local.m;
        const float s = car->model_scale;
        m[0] = -cosf(lean) * s; m[1] = sinf(lean) * s;  m[2] = 0.0f;
        m[3] = sinf(lean) * s;  m[4] = cosf(lean) * s;  m[5] = 0.0f;
        m[6] = 0.0f;            m[7] = 0.0f;            m[8] = -s;
    }
    if (car->camera != NULL) {
        follow_camera(self, car, dt);
    }

    /* Race state and the HUD. */
    if (car->racing && !car->finished) {
        car->time += dt;
        format_time(car->time_text, sizeof(car->time_text), "", car->time);
        rr_node_set_text(car->time_label, car->time_text);
    }
    if (!car->finished) {
        car->distance = race_distance(self, car);
        for (i = 0; i < car->opp_count; i++) {
            const float d = ai_driver_distance(car->opps[i]);
            car->opps[i]->push = clampf((d - car->distance) / 60.0f, -1.0f, 1.0f);
            ahead += d > car->distance;
        }
        car->place = ahead + 1;
    }
    snprintf(car->pos_text, sizeof(car->pos_text), "POS %d/%d", car->place, car->opp_count + 1);
    rr_node_set_text(car->pos_label, car->pos_text);
    snprintf(car->speed_text, sizeof(car->speed_text), "%d KM/H", (int)(fabsf(car->speed) * 3.6f + 0.5f));
    rr_node_set_text(car->speed_label, car->speed_text);
}

/* The countdown Timer: 3, 2, 1, GO, then the message goes away. */
static void race_car_on_countdown(Node *self, RaceCar *car)
{
    int i;

    (void)self;
    car->count--;
    if (car->count > 0) {
        snprintf(car->message_text, sizeof(car->message_text), "%d", car->count);
    } else if (car->count == 0) {
        snprintf(car->message_text, sizeof(car->message_text), "GO!");
        car->racing = true;
        for (i = 0; i < car->opp_count; i++) {
            car->opps[i]->racing = true;
        }
        printf("race: go\n");
        fflush(stdout);
    } else {
        car->message_text[0] = '\0';
        rr_timer_stop(car->countdown);
    }
    rr_node_set_text(car->message_label, car->message_text);
}

/* Connected to `area_entered` of the car's hitbox: a checkpoint gate. */
static void race_car_on_gate(Node *self, RaceCar *car, Node *gate)
{
    int index;

    (void)self;
    if (car->finished || gate == NULL) {
        return;
    }
    for (index = 0; index < car->cp_count && car->cps[index] != gate->parent; index++) {
    }
    if (index != car->next_cp) {
        return; /* not the next one: going the wrong way, or cutting */
    }
    car->last_cp = index;
    car->next_cp = (index + 1) % car->cp_count;
    if (index != 0) {
        return;
    }
    if (car->counting) {
        car->last_lap = car->time - car->lap_start;
        car->laps_done++;
        printf("race: lap %d %.2f s\n", car->laps_done, (double)car->last_lap);
        if (car->best <= 0.0f || car->last_lap < car->best) {
            const RaceSave save = { SAVE_MAGIC, car->last_lap };
            car->best = car->last_lap;
            rr_save_write(SAVE_NAME, &save, sizeof(save));
            format_time(car->best_text, sizeof(car->best_text), "BEST ", car->best);
            rr_node_set_text(car->best_label, car->best_text);
        }
        if (car->laps_done >= car->laps) {
            car->finished = true;
            car->distance = (float)car->laps * car->length + car->cps[0]->value;
            car->place    = 1;
            for (index = 0; index < car->opp_count; index++) {
                car->place += ai_driver_distance(car->opps[index]) > car->distance;
            }
            snprintf(car->message_text, sizeof(car->message_text), "FINISH  P%d", car->place);
            rr_node_set_text(car->message_label, car->message_text);
            printf("race: finish P%d %.2f s\n", car->place, (double)car->time);
            rr_timer_start(car->finish_timer, 0.0f);
        } else {
            snprintf(car->lap_text, sizeof(car->lap_text), "LAP %d/%d", car->laps_done + 1, (int)car->laps);
            rr_node_set_text(car->lap_label, car->lap_text);
        }
        fflush(stdout);
    }
    car->counting  = true;
    car->lap_start = car->time;
}

static void race_car_on_finish_timer(Node *self, RaceCar *car)
{
    (void)self;
    rr_change_scene(car->menu_scene);
}

COMPONENT_BEGIN(RaceCar, "RaceCar")
    EXPORT_FLOAT(max_speed)
    EXPORT_FLOAT(accel)
    EXPORT_FLOAT(brake)
    EXPORT_FLOAT(steer)
    EXPORT_FLOAT(grip)
    EXPORT_INT(laps)
    EXPORT_FLOAT(start_distance)
    EXPORT_FLOAT(model_scale)
    EXPORT_NODE(model)
    EXPORT_NODE(camera)
    EXPORT_NODE(path)
    EXPORT_NODE(checkpoints)
    EXPORT_NODE(opponents)
    EXPORT_NODE(countdown)
    EXPORT_NODE(finish_timer)
    EXPORT_NODE(lap_label)
    EXPORT_NODE(time_label)
    EXPORT_NODE(best_label)
    EXPORT_NODE(pos_label)
    EXPORT_NODE(speed_label)
    EXPORT_NODE(message_label)
    EXPORT_SCENE(menu_scene)
    ON_READY(race_car_ready)
    ON_UPDATE(race_car_update)
    ON_SIGNAL("_on_countdown_timeout", race_car_on_countdown)
    ON_SIGNAL("_on_finish_timer_timeout", race_car_on_finish_timer)
    ON_SIGNAL_NODE("_on_hitbox_area_entered", race_car_on_gate)
COMPONENT_END()
