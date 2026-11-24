/* Components of the airplane game (scenes/airplane.tscn), the collect mode of the Unity
 * prototype "Airplane Arcade" (2019), whose scene tools/make_airplane.gd converts.
 * Placeholder scripts: components/airplane_*.gd, collectable.gd, jukebox.gd.
 *
 * The game: at the title the camera circles the island. Start shows the plane, which
 * counts down and then flies on its own; the player steers, pitches, speeds up, boosts and
 * fires. Gates and coins are collected by flying through them, targets and pirate ships by
 * shooting them; each one adds time to the clock. When the clock runs out, the plane
 * touches the ground or the water, or everything is collected, the game is over and shows
 * the time flown and the best time of a finished run.
 *
 * AirplanePlayer  the plane: an Area3D that is moved by code. What it flies into reports
 *                 itself through the area's `area_entered` and `body_entered` signals:
 *                 areas in the group "world" (the water) and every body (ground, trees,
 *                 ships, targets) crash it, an area with a Collectable is collected.
 * AirplaneCamera  follows the plane like Unity's SmoothFollow, or circles the world's
 *                 origin like the prototype's title and game over screens.
 * AirplaneGame    title, counting what is left, game over, best time (saved game).
 * Collectable     data of a gate, coin, target or ship: the time it gives, whether it is
 *                 shot rather than flown through, the sound and the burst it starts.
 * AirplaneBullet  flies straight on; its RayCast3D finds what it hits on the way.
 * Jukebox         plays its AudioStreamPlayer children in random order; their names are
 *                 "Artist - Title" and are shown in two labels when a piece starts. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../core/rr_scene.h"

#define DEG2RAD 0.017453292f

static float clampf_(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static float lerpf_(float a, float b, float t)
{
    return a + (b - a) * clampf_(t, 0.0f, 1.0f);
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

/* Visible itself and all its parents, as Godot's is_visible_in_tree(). */
static bool visible_in_tree(const Node *node)
{
    for (; node != NULL; node = node->parent) {
        if (!(node->flags & RR_NODE_FLAG_VISIBLE)) {
            return false;
        }
    }
    return true;
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

/* Sets the basis of a matrix from Unity's Euler angles (degrees; Z, then X, then Y),
 * converted to Godot's axes: Unity's Z is negated, which turns the rotations about X and
 * Y around. `scale` is kept on every axis. */
static void set_unity_euler(float *m, float pitch, float yaw, float roll, const float *scale)
{
    const float a = -yaw * DEG2RAD, b = -pitch * DEG2RAD, c = roll * DEG2RAD;
    const float sa = sinf(a), ca = cosf(a), sb = sinf(b), cb = cosf(b), sc = sinf(c), cc = cosf(c);
    /* R = Ry(a) * Rx(b) * Rz(c); m holds the columns. */
    const float r[9] = {
        ca * cc + sa * sb * sc, cb * sc, -sa * cc + ca * sb * sc, /* X column */
        -ca * sc + sa * sb * cc, cb * cc, sa * sc + ca * sb * cc, /* Y column */
        sa * cb, -sb, ca * cb,                                    /* Z column */
    };
    int i;
    for (i = 0; i < 9; i++) {
        m[i] = r[i] * scale[i / 3];
    }
}

/* The length of the three basis columns: the scale of a matrix. */
static void get_scale(const float *m, float *scale)
{
    int i;
    for (i = 0; i < 3; i++) {
        scale[i] = sqrtf(m[i * 3] * m[i * 3] + m[i * 3 + 1] * m[i * 3 + 1] + m[i * 3 + 2] * m[i * 3 + 2]);
    }
}

/* Restarts every particle system of a burst: the node itself and its children. The node
 * is put at `at` first. */
static void fire_burst(Node *burst, const float *at)
{
    Node *child = NULL;
    if (burst == NULL) {
        return;
    }
    burst->local.m[9]  = at[0];
    burst->local.m[10] = at[1];
    burst->local.m[11] = at[2];
    /* The children's world places follow at the end of the frame; the particles start
     * from there. */
    if (burst->type == RR_NODE_PARTICLES) {
        rr_particles_restart(burst);
    }
    while ((child = rr_node_child(burst, child)) != NULL) {
        if (child->type == RR_NODE_PARTICLES) {
            rr_particles_restart(child);
        }
    }
}

/* Minutes, seconds and thousandths. */
static void format_time(char *out, size_t size, float seconds)
{
    const int ms = (int)(seconds * 1000.0f);
    snprintf(out, size, "%02d:%02d:%03d", ms / 60000, (ms / 1000) % 60, ms % 1000);
}

/* --- Collectable ------------------------------------------------------------------------ */

typedef struct {
    float extend_time; /* exported: seconds added to the clock */
    bool  destroyable; /* exported: shot by a bullet, not collected by flying through */
    Node *sound;       /* exported: AudioStreamPlayer played when it is collected */
    Node *burst;       /* exported: particle systems fired where it was */
} Collectable;

COMPONENT_BEGIN(Collectable, "Collectable")
    EXPORT_FLOAT(extend_time)
    EXPORT_BOOL(destroyable)
    EXPORT_NODE(sound)
    EXPORT_NODE(burst)
COMPONENT_END()

/* The Collectable of a node or of the nearest parent that has one, and that node. */
static Collectable *find_collectable(Node *node, Node **owner)
{
    for (; node != NULL; node = node->parent) {
        Collectable *c = (Collectable *)rr_node_component(node, "Collectable");
        if (c != NULL) {
            *owner = node;
            return c;
        }
    }
    return NULL;
}

/* --- AirplaneCamera --------------------------------------------------------------------- */

typedef struct {
    Node *target;           /* exported: the plane */
    float distance;         /* exported: metres behind the target */
    float height;           /* exported: metres above it */
    float height_damping;   /* exported */
    float rotation_damping; /* exported */
    float orbit_speed;      /* exported: degrees per second around the world's origin */
    bool  follow;           /* exported: follow the target from the start */

    float home[3]; /* where the circling starts */
    float yaw;     /* degrees, Godot's rotation about Y */
} AirplaneCamera;

static void airplane_camera_ready(Node *self, AirplaneCamera *c)
{
    int i;
    for (i = 0; i < 3; i++) {
        c->home[i] = self->local.m[9 + i];
    }
    /* The camera looks along its -Z. */
    c->yaw = atan2f(self->local.m[6], self->local.m[8]) / DEG2RAD;
}

/* Turns the camera to look at a point, with Y up. */
static void look_at(Node *self, const float *at)
{
    float *m = self->local.m;
    float  z[3] = { m[9] - at[0], m[10] - at[1], m[11] - at[2] }; /* backwards */
    float  x[3], y[3], len;

    len = sqrtf(z[0] * z[0] + z[1] * z[1] + z[2] * z[2]);
    if (len < 0.0001f) {
        return;
    }
    z[0] /= len; z[1] /= len; z[2] /= len;
    /* x = up x z */
    x[0] = z[2]; x[1] = 0.0f; x[2] = -z[0];
    len = sqrtf(x[0] * x[0] + x[2] * x[2]);
    if (len < 0.0001f) {
        return;
    }
    x[0] /= len; x[2] /= len;
    /* y = z x x */
    y[0] = z[1] * x[2] - z[2] * x[1];
    y[1] = z[2] * x[0] - z[0] * x[2];
    y[2] = z[0] * x[1] - z[1] * x[0];
    m[0] = x[0]; m[1] = x[1]; m[2] = x[2];
    m[3] = y[0]; m[4] = y[1]; m[5] = y[2];
    m[6] = z[0]; m[7] = z[1]; m[8] = z[2];
}

static float lerp_angle(float a, float b, float t)
{
    float d = fmodf(b - a, 360.0f);
    if (d > 180.0f) {
        d -= 360.0f;
    } else if (d < -180.0f) {
        d += 360.0f;
    }
    return a + d * clampf_(t, 0.0f, 1.0f);
}

static void airplane_camera_update(Node *self, AirplaneCamera *c, float dt)
{
    float *m = self->local.m;

    if (c->follow && c->target != NULL) {
        /* Unity's SmoothFollow: behind the target's heading, at a height that lags. */
        const float *t = c->target->world.m;
        /* The plane flies along its -Z; its heading is the angle of that on the ground. */
        const float wanted = atan2f(t[6], t[8]) / DEG2RAD;
        const float h      = lerpf_(m[10], t[10] + c->height, c->height_damping * dt);
        const float target_pos[3] = { t[9], t[10], t[11] };
        c->yaw = lerp_angle(c->yaw, wanted, c->rotation_damping * dt);
        m[9]  = t[9] + sinf(c->yaw * DEG2RAD) * c->distance;
        m[10] = h;
        m[11] = t[11] + cosf(c->yaw * DEG2RAD) * c->distance;
        look_at(self, target_pos);
    } else {
        /* Around the world's origin, about Y: position and orientation together. */
        const float a = -c->orbit_speed * DEG2RAD * dt;
        const float s = sinf(a), co = cosf(a);
        int         col;
        for (col = 0; col < 4; col++) {
            float *v = &m[col * 3];
            const float x = v[0], z = v[2];
            v[0] = co * x + s * z;
            v[2] = -s * x + co * z;
        }
    }
}

/* Back to circling, from where it started. */
static void airplane_camera_orbit(Node *camera)
{
    AirplaneCamera *c = camera != NULL ? (AirplaneCamera *)rr_node_component(camera, "AirplaneCamera") : NULL;
    if (c != NULL) {
        c->follow = false;
        camera->local.m[9]  = c->home[0];
        camera->local.m[10] = c->home[1];
        camera->local.m[11] = c->home[2];
    }
}

static void airplane_camera_follow(Node *camera)
{
    AirplaneCamera *c = camera != NULL ? (AirplaneCamera *)rr_node_component(camera, "AirplaneCamera") : NULL;
    if (c != NULL) {
        c->follow = true;
    }
}

COMPONENT_BEGIN(AirplaneCamera, "AirplaneCamera")
    EXPORT_NODE(target)
    EXPORT_FLOAT(distance)
    EXPORT_FLOAT(height)
    EXPORT_FLOAT(height_damping)
    EXPORT_FLOAT(rotation_damping)
    EXPORT_FLOAT(orbit_speed)
    EXPORT_BOOL(follow)
    ON_READY(airplane_camera_ready)
    ON_UPDATE(airplane_camera_update)
COMPONENT_END()

/* --- AirplanePlayer --------------------------------------------------------------------- */

typedef struct {
    float min_speed;   /* exported: metres per second */
    float max_speed;   /* exported */
    float boost_speed; /* exported: top speed while boosting */
    float turn_speed;  /* exported: degrees per second */
    float roll_speed;  /* exported */
    float max_roll;    /* exported: degrees */
    float pitch_speed; /* exported */
    float max_pitch;   /* exported */
    float start_time;  /* exported: seconds on the clock when the countdown ends */
    float countdown;   /* exported: seconds from the start to "GO!" */
    Node *game;        /* exported: the node with the AirplaneGame */
    Node *model;       /* exported: hidden when the plane crashes */
    Node *time_label;          /* exported: the seconds that are left */
    Node *clock_minutes;       /* exported: the time flown, three labels */
    Node *clock_seconds;       /* exported */
    Node *clock_millis;        /* exported */
    Node *countdown_label;     /* exported: 3, 2, 1, GO! */
    Node *mode_panel;          /* exported: shown during the countdown */
    Node *boost_label;         /* exported */
    Node *boost_bar;           /* exported: TextureProgressBar, 0..100 */
    Node *time_up_label;       /* exported: shown when the clock has run out */
    Node *engine_sound;        /* exported: looping; its pitch follows the speed */
    Node *crash_sound;         /* exported */
    Node *count_sound;         /* exported: every second of the countdown */
    Node *go_sound;            /* exported */
    Node *fire_sound;          /* exported */
    Node *crash_burst;         /* exported: particle systems fired at a crash */
    Node *bullet_spawn;        /* exported: where bullets start */
    const RRPackedScene *bullet; /* exported */
    int32_t bullet_max;          /* exported */

    int   act_left, act_right, act_up, act_down, act_faster, act_slower, act_boost, act_fire;
    bool  started;   /* the game has begun: the node was shown */
    bool  counting;  /* the countdown runs */
    bool  blocked;   /* the controls do nothing: countdown or time up */
    bool  crashed, over;
    float count_left;
    int   last_count;
    float yaw, pitch, roll, roll_back;
    float scale[3];
    float speed;
    float boost;
    bool  boost_ok, boost_was_down;
    float time_left, elapsed, crash_time;
    char  time_text[12], count_text[12], minutes_text[8], seconds_text[8], millis_text[8];
    const char *boost_text;
} AirplanePlayer;

static void airplane_player_ready(Node *self, AirplanePlayer *p)
{
    p->act_left   = rr_action(self->scene, "steer_left");
    p->act_right  = rr_action(self->scene, "steer_right");
    p->act_up     = rr_action(self->scene, "pitch_up");
    p->act_down   = rr_action(self->scene, "pitch_down");
    p->act_faster = rr_action(self->scene, "throttle_up");
    p->act_slower = rr_action(self->scene, "throttle_down");
    p->act_boost  = rr_action(self->scene, "boost");
    p->act_fire   = rr_action(self->scene, "fire");
    get_scale(self->local.m, p->scale);
    /* The heading the plane starts with; Unity's yaw counts the other way round. */
    p->yaw        = -atan2f(self->local.m[6], self->local.m[8]) / DEG2RAD;
    p->speed      = p->min_speed;
    p->count_left = p->countdown;
    p->last_count = -1;
    p->time_left  = p->start_time + p->countdown;
    p->counting   = true;
    p->blocked    = true;
    p->boost_text = "";
}

static void airplane_player_game_over(Node *self, AirplanePlayer *p)
{
    if (!p->over) {
        p->over = true;
        if (p->game != NULL) {
            rr_node_call(p->game, "_on_player_game_over", self);
        }
    }
}

static void airplane_player_crash(Node *self, AirplanePlayer *p)
{
    const float at[3] = { self->world.m[9], self->world.m[10], self->world.m[11] };
    if (p->crashed || p->over) {
        return;
    }
    p->crashed    = true;
    p->crash_time = 0.0f;
    show(p->model, false);
    fire_burst(p->crash_burst, at);
    if (p->engine_sound != NULL) {
        rr_audio_stop(p->engine_sound);
    }
    play(p->crash_sound);
}

static void airplane_player_collect(AirplanePlayer *p, Node *owner, Collectable *c)
{
    const float at[3] = { owner->world.m[9], owner->world.m[10], owner->world.m[11] };
    p->time_left += c->extend_time;
    play(c->sound);
    fire_burst(c->burst, at);
    rr_queue_free(owner);
}

/* The countdown at the start: three, two, one, GO!, then the controls are free. */
static void airplane_player_countdown(AirplanePlayer *p, float dt)
{
    const int count = (int)ceilf(p->count_left);
    p->count_left -= dt;
    if (p->count_left < 3.0f && p->count_left > 0.0f) {
        if (count != p->last_count) {
            snprintf(p->count_text, sizeof(p->count_text), "%d", count);
            set_text(p->countdown_label, p->count_text);
            show(p->countdown_label, true);
            play(p->count_sound);
        }
    } else if (p->count_left <= 0.0f && p->count_left > -2.0f) {
        if (p->blocked) {
            p->blocked = false;
            set_text(p->countdown_label, "GO!");
            play(p->go_sound);
        }
    } else if (p->count_left <= -2.0f) {
        show(p->countdown_label, false);
        show(p->mode_panel, false);
        p->counting = false;
    }
    p->last_count = count;
}

static void airplane_player_clock(AirplanePlayer *p, float dt)
{
    const int ms = (int)(p->elapsed * 1000.0f);
    if (!p->over) {
        p->time_left -= dt;
    }
    if (!p->counting && !p->over) {
        p->elapsed += dt;
    }
    snprintf(p->time_text, sizeof(p->time_text), "%d", p->time_left > 0.0f ? (int)ceilf(p->time_left) : 0);
    set_text(p->time_label, p->time_text);
    snprintf(p->minutes_text, sizeof(p->minutes_text), "%02d", (ms / 60000) % 100);
    snprintf(p->seconds_text, sizeof(p->seconds_text), "%02d", (ms / 1000) % 60);
    snprintf(p->millis_text, sizeof(p->millis_text), "%03d", ms % 1000);
    set_text(p->clock_minutes, p->minutes_text);
    set_text(p->clock_seconds, p->seconds_text);
    set_text(p->clock_millis, p->millis_text);
    if (p->time_left < -1.0f) {
        show(p->time_up_label, true);
        p->blocked = true;
    }
}

static void airplane_player_boost(AirplanePlayer *p, float dt)
{
    const bool down = rr_action_down(p->act_boost);
    const char *text;

    if (p->counting) {
        text = "BOOST DISABLED";
    } else if (p->boost >= 100.0f) {
        text = "BOOST OK!";
    } else if (!p->boost_ok) {
        text = "CHARGING";
    } else {
        text = "";
    }
    if (text != p->boost_text) {
        p->boost_text = text;
        set_text(p->boost_label, text);
    }
    if (p->boost <= 0.0f) {
        p->boost_ok = false;
    }
    p->boost = clampf_(p->boost, 0.0f, 100.0f);
    if (p->boost_bar != NULL) {
        p->boost_bar->value = p->boost;
    }

    /* Throttle. */
    if (rr_action_down(p->act_faster)) {
        if (!p->blocked) {
            p->speed += 20.0f * dt;
        }
    } else if (rr_action_down(p->act_slower)) {
        if (!p->blocked) {
            p->speed -= 20.0f * dt;
        }
    } else {
        p->speed -= 10.0f * dt;
    }

    if (down && p->boost_ok && !p->blocked) {
        p->speed += 100.0f * dt;
        p->boost -= 30.0f * dt;
        p->speed = clampf_(p->speed, p->min_speed, p->boost_speed);
    } else {
        if (p->boost_was_down && p->boost_ok && !p->blocked) {
            /* Letting go spends what is left. */
            p->boost    = 0.0f;
            p->boost_ok = false;
        } else {
            if (!p->boost_ok && !p->counting) {
                p->boost += 10.0f * dt; /* recharging */
            }
            if (p->boost >= 100.0f && !p->boost_ok) {
                p->boost_ok = true;
            }
        }
        if (p->speed > p->max_speed + 1.0f) {
            p->speed -= 100.0f * dt;
        } else {
            p->speed = clampf_(p->speed, p->min_speed, p->max_speed);
        }
    }
    p->boost_was_down = down;
    if (p->blocked && !p->counting) {
        p->speed -= 100.0f * dt; /* time is up: the plane runs out of speed */
        if (p->speed < 0.0f) {
            p->speed = 0.0f;
        }
    }
}

static void airplane_player_steer(Node *self, AirplanePlayer *p, float dt)
{
    float *m = self->local.m;
    int    i;

    if (!p->blocked && rr_action_down(p->act_right)) {
        p->roll_back = 0.0f;
        p->roll      = clampf_(p->roll - p->roll_speed * dt, -p->max_roll, p->max_roll);
        p->yaw += p->turn_speed * dt;
    } else if (!p->blocked && rr_action_down(p->act_left)) {
        p->roll_back = 0.0f;
        p->roll      = clampf_(p->roll + p->roll_speed * dt, -p->max_roll, p->max_roll);
        p->yaw -= p->turn_speed * dt;
    } else {
        /* Levels out, slowly at first. */
        p->roll_back += dt / 5.0f;
        p->roll = lerpf_(p->roll, 0.0f, p->roll_back);
    }
    /* As in the prototype, "up" on the stick puts the nose down, like a plane's stick. */
    if (!p->blocked && rr_action_down(p->act_down)) {
        p->pitch = clampf_(p->pitch + p->pitch_speed * dt, -p->max_pitch, p->max_pitch);
    } else if (!p->blocked && rr_action_down(p->act_up)) {
        p->pitch = clampf_(p->pitch - p->pitch_speed * dt, -p->max_pitch, p->max_pitch);
    }
    set_unity_euler(m, p->pitch, p->yaw, p->roll, p->scale);
    /* Forward is the plane's -Z. */
    for (i = 0; i < 3; i++) {
        m[9 + i] -= m[6 + i] / p->scale[2] * p->speed * dt;
    }
}

static void airplane_player_fire(Node *self, AirplanePlayer *p)
{
    Node       *bullet;
    const float one[3] = { 1.0f, 1.0f, 1.0f };
    int         i;

    if (p->blocked || !rr_action_pressed(p->act_fire)) {
        return;
    }
    bullet = rr_instantiate(p->bullet);
    if (bullet == NULL) {
        return;
    }
    play(p->fire_sound);
    set_unity_euler(bullet->local.m, p->pitch, p->yaw, p->roll, one);
    for (i = 0; i < 3; i++) {
        bullet->local.m[9 + i] = p->bullet_spawn != NULL ? p->bullet_spawn->world.m[9 + i] : self->world.m[9 + i];
    }
}

static void airplane_player_update(Node *self, AirplanePlayer *p, float dt)
{
    if (!visible_in_tree(self)) {
        return; /* the title screen: the game has not begun */
    }
    if (!p->started) {
        p->started = true;
        show(p->mode_panel, true);
        play(p->engine_sound);
    }
    if (p->counting) {
        airplane_player_countdown(p, dt);
    }
    if (!p->crashed) {
        airplane_player_boost(p, dt);
        airplane_player_fire(self, p);
        airplane_player_steer(self, p, dt);
        if (p->engine_sound != NULL) {
            rr_audio_set_pitch_scale(p->engine_sound, clampf_(p->speed / p->min_speed * 0.3f, 0.1f, 4.0f));
        }
    } else {
        p->crash_time += dt;
        if (p->crash_time >= 3.0f) {
            airplane_player_game_over(self, p);
        }
    }
    airplane_player_clock(p, dt);
    if (p->time_left < -5.0f) {
        airplane_player_game_over(self, p);
    }
}

/* Connected to `area_entered`: the water crashes the plane, gates and coins are taken. */
static void airplane_player_on_area(Node *self, AirplanePlayer *p, Node *area)
{
    Node        *owner = NULL;
    Collectable *c;

    if (area == NULL || !p->started || p->crashed || !visible_in_tree(area)) {
        return;
    }
    if (rr_node_has_tag(area, "world")) {
        airplane_player_crash(self, p);
        return;
    }
    c = find_collectable(area, &owner);
    if (c != NULL && !c->destroyable && !p->blocked) {
        airplane_player_collect(p, owner, c);
    }
}

/* Connected to `body_entered`: anything solid crashes the plane. */
static void airplane_player_on_body(Node *self, AirplanePlayer *p, Node *body)
{
    if (body != NULL && p->started && visible_in_tree(body)) {
        airplane_player_crash(self, p);
    }
}

/* Called by a bullet that hit something. */
static void airplane_player_on_shot(Node *self, AirplanePlayer *p, Node *hit)
{
    Node        *owner = NULL;
    Collectable *c     = find_collectable(hit, &owner);
    (void)self;
    if (c != NULL && c->destroyable && !p->blocked && visible_in_tree(owner)) {
        airplane_player_collect(p, owner, c);
    }
}

COMPONENT_BEGIN(AirplanePlayer, "AirplanePlayer")
    EXPORT_FLOAT(min_speed)
    EXPORT_FLOAT(max_speed)
    EXPORT_FLOAT(boost_speed)
    EXPORT_FLOAT(turn_speed)
    EXPORT_FLOAT(roll_speed)
    EXPORT_FLOAT(max_roll)
    EXPORT_FLOAT(pitch_speed)
    EXPORT_FLOAT(max_pitch)
    EXPORT_FLOAT(start_time)
    EXPORT_FLOAT(countdown)
    EXPORT_NODE(game)
    EXPORT_NODE(model)
    EXPORT_NODE(time_label)
    EXPORT_NODE(clock_minutes)
    EXPORT_NODE(clock_seconds)
    EXPORT_NODE(clock_millis)
    EXPORT_NODE(countdown_label)
    EXPORT_NODE(mode_panel)
    EXPORT_NODE(boost_label)
    EXPORT_NODE(boost_bar)
    EXPORT_NODE(time_up_label)
    EXPORT_NODE(engine_sound)
    EXPORT_NODE(crash_sound)
    EXPORT_NODE(count_sound)
    EXPORT_NODE(go_sound)
    EXPORT_NODE(fire_sound)
    EXPORT_NODE(crash_burst)
    EXPORT_NODE(bullet_spawn)
    EXPORT_PACKED_SCENE(bullet)
    EXPORT_INT(bullet_max)
    ON_READY(airplane_player_ready)
    ON_UPDATE(airplane_player_update)
    ON_SIGNAL_NODE("_on_area_entered", airplane_player_on_area)
    ON_SIGNAL_NODE("_on_body_entered", airplane_player_on_body)
    ON_SIGNAL_NODE("_on_shot", airplane_player_on_shot)
COMPONENT_END()

/* --- AirplaneBullet --------------------------------------------------------------------- */

typedef struct {
    float speed;    /* exported: metres per second */
    float lifetime; /* exported: seconds */
    Node *ray;      /* exported: RayCast3D along the bullet's -Z, a frame's flight long */

    float age;
} AirplaneBullet;

static void airplane_bullet_ready(Node *self, AirplaneBullet *b)
{
    (void)self;
    b->age = 0.0f;
}

static void airplane_bullet_update(Node *self, AirplaneBullet *b, float dt)
{
    float *m = self->local.m;
    int    i;

    b->age += dt;
    if (b->age > b->lifetime) {
        rr_queue_free(self);
        return;
    }
    /* What lies on the way of this frame's flight. */
    if (b->ray != NULL) {
        rr_raycast_force_update(b->ray);
        if (rr_raycast_is_colliding(b->ray)) {
            Node *hit    = rr_raycast_get_collider(b->ray);
            Node *player = rr_scene_find_tagged(self->scene, "player", NULL);
            if (hit != NULL && player != NULL) {
                rr_node_call(player, "_on_shot", hit);
            }
            rr_queue_free(self);
            return;
        }
    }
    for (i = 0; i < 3; i++) {
        m[9 + i] -= m[6 + i] * b->speed * dt;
    }
}

COMPONENT_BEGIN(AirplaneBullet, "AirplaneBullet")
    EXPORT_FLOAT(speed)
    EXPORT_FLOAT(lifetime)
    EXPORT_NODE(ray)
    ON_READY(airplane_bullet_ready)
    ON_UPDATE(airplane_bullet_update)
COMPONENT_END()

/* --- AirplaneGame ----------------------------------------------------------------------- */

typedef struct {
    Node *player;        /* exported: the node with the AirplanePlayer */
    Node *camera;        /* exported: the node with the AirplaneCamera */
    Node *title_ui;      /* exported: hidden when the game begins */
    Node *press_start;   /* exported: blinks on the title */
    Node *hud;           /* exported: the plane's HUD, shown while flying */
    Node *collect_ui;    /* exported: what is left to collect */
    Node *gates_label;   /* exported */
    Node *coins_label;   /* exported */
    Node *targets_label; /* exported */
    Node *ships_label;   /* exported */
    Node *gates;         /* exported: their children are what is to collect */
    Node *coins;         /* exported */
    Node *targets;       /* exported */
    Node *ships;         /* exported */
    Node *game_over_ui;  /* exported */
    Node *result_label;  /* exported: "GAME OVER!" or "FINISHED!" */
    Node *time_label;    /* exported: the time of this run */
    Node *best_label;    /* exported: the best finished run */
    Node *new_record;    /* exported: shown for a new best time */
    Node *retry_label;   /* exported: blinks on the game over screen */
    const char *restart_scene; /* exported */
    const char *menu_scene;    /* exported */

    int   act_start, act_leave;
    int   state; /* 0 title, 1 flying, 2 over */
    float blink;
    int   totals[4];
    int   left[4];
    float best;
    char  counts_text[4][24];
    char  time_text[16], best_text[16];
} AirplaneGame;

typedef struct {
    uint32_t magic;
    float    best; /* seconds, 0 before the first finished run */
} AirplaneSave;

#define AIRPLANE_SAVE_NAME  "RR_AIRPLANE"
#define AIRPLANE_SAVE_MAGIC 0x41495231u

static int count_children(Node *list)
{
    Node *child = NULL;
    int   n     = 0;
    if (list != NULL) {
        while ((child = rr_node_child(list, child)) != NULL) {
            n++;
        }
    }
    return n;
}

static void airplane_game_show_counts(AirplaneGame *g)
{
    static const char *const names[4] = { "GATES", "COINS", "TARGETS", "SHIPS" };
    Node *const lists[4]  = { g->gates, g->coins, g->targets, g->ships };
    Node *const labels[4] = { g->gates_label, g->coins_label, g->targets_label, g->ships_label };
    int         i;
    for (i = 0; i < 4; i++) {
        const int left = count_children(lists[i]);
        if (left != g->left[i]) {
            g->left[i] = left;
            snprintf(g->counts_text[i], sizeof(g->counts_text[i]), "%s %d/%d", names[i],
                     g->totals[i] - left, g->totals[i]);
            set_text(labels[i], g->counts_text[i]);
        }
    }
}

static void airplane_game_ready(Node *self, AirplaneGame *g)
{
    Node *const  lists[4] = { g->gates, g->coins, g->targets, g->ships };
    AirplaneSave save;
    int          i;

    g->act_start = rr_action(self->scene, "start");
    g->act_leave = rr_action(self->scene, "ui_cancel");
    for (i = 0; i < 4; i++) {
        g->totals[i] = count_children(lists[i]);
        g->left[i]   = -1;
    }
    g->best = 0.0f;
    if (rr_save_read(AIRPLANE_SAVE_NAME, &save, sizeof(save)) && save.magic == AIRPLANE_SAVE_MAGIC) {
        g->best = save.best;
    }
    airplane_game_show_counts(g);
}

static void airplane_game_update(Node *self, AirplaneGame *g, float dt)
{
    (void)self;
    if (rr_action_pressed(g->act_leave) && g->menu_scene != NULL && g->menu_scene[0] != '\0') {
        rr_change_scene(g->menu_scene);
        return;
    }
    g->blink += dt;
    if (g->state == 0) {
        show(g->press_start, fmodf(g->blink, 1.0f) < 0.6f);
        if (rr_action_pressed(g->act_start)) {
            g->state = 1;
            show(g->title_ui, false);
            show(g->player, true);
            show(g->hud, true);
            show(g->collect_ui, true);
            show(g->gates, true);
            show(g->coins, true);
            show(g->targets, true);
            show(g->ships, true);
            airplane_camera_follow(g->camera);
        }
    } else if (g->state == 1) {
        int i, left = 0;
        airplane_game_show_counts(g);
        for (i = 0; i < 4; i++) {
            left += g->left[i];
        }
        if (left == 0) {
            /* Everything collected. */
            set_text(g->result_label, "FINISHED!");
            if (g->player != NULL) {
                AirplanePlayer *p = (AirplanePlayer *)rr_node_component(g->player, "AirplanePlayer");
                if (p != NULL) {
                    airplane_player_game_over(g->player, p);
                }
            }
        }
    } else {
        show(g->retry_label, fmodf(g->blink, 1.0f) < 0.6f);
        if (rr_action_pressed(g->act_start) && g->restart_scene != NULL) {
            rr_change_scene(g->restart_scene);
        }
    }
}

/* Called by the player when the game is over. */
static void airplane_game_on_over(Node *self, AirplaneGame *g, Node *player)
{
    AirplanePlayer *p = player != NULL ? (AirplanePlayer *)rr_node_component(player, "AirplanePlayer") : NULL;
    int             i, left = 0;
    float           flown = p != NULL ? p->elapsed : 0.0f;

    (void)self;
    g->state = 2;
    g->blink = 0.0f;
    airplane_camera_orbit(g->camera);
    show(g->player, false);
    show(g->hud, false);
    show(g->collect_ui, false);
    show(g->game_over_ui, true);
    for (i = 0; i < 4; i++) {
        left += g->left[i];
    }
    if (left == 0 && (g->best <= 0.0f || flown < g->best)) {
        AirplaneSave save = { AIRPLANE_SAVE_MAGIC, flown };
        g->best = flown;
        rr_save_write(AIRPLANE_SAVE_NAME, &save, sizeof(save));
        show(g->new_record, true);
    }
    format_time(g->time_text, sizeof(g->time_text), flown);
    set_text(g->time_label, g->time_text);
    if (g->best > 0.0f) {
        format_time(g->best_text, sizeof(g->best_text), g->best);
    } else {
        snprintf(g->best_text, sizeof(g->best_text), "--:--:---");
    }
    set_text(g->best_label, g->best_text);
}

COMPONENT_BEGIN(AirplaneGame, "AirplaneGame")
    EXPORT_NODE(player)
    EXPORT_NODE(camera)
    EXPORT_NODE(title_ui)
    EXPORT_NODE(press_start)
    EXPORT_NODE(hud)
    EXPORT_NODE(collect_ui)
    EXPORT_NODE(gates_label)
    EXPORT_NODE(coins_label)
    EXPORT_NODE(targets_label)
    EXPORT_NODE(ships_label)
    EXPORT_NODE(gates)
    EXPORT_NODE(coins)
    EXPORT_NODE(targets)
    EXPORT_NODE(ships)
    EXPORT_NODE(game_over_ui)
    EXPORT_NODE(result_label)
    EXPORT_NODE(time_label)
    EXPORT_NODE(best_label)
    EXPORT_NODE(new_record)
    EXPORT_NODE(retry_label)
    EXPORT_SCENE(restart_scene)
    EXPORT_SCENE(menu_scene)
    ON_READY(airplane_game_ready)
    ON_UPDATE(airplane_game_update)
    ON_SIGNAL_NODE("_on_player_game_over", airplane_game_on_over)
COMPONENT_END()

/* --- Jukebox ---------------------------------------------------------------------------- */

#define JUKEBOX_MAX 16

typedef struct {
    Node *artist_label; /* exported */
    Node *title_label;  /* exported */
    Node *panel;        /* exported: shown for show_time after a piece starts */
    float show_time;    /* exported: seconds */

    int   act_next;
    Node *songs[JUKEBOX_MAX];
    int   count, current;
    bool  started;
    float shown;
    char  artist[48], title[48];
} Jukebox;

static void jukebox_ready(Node *self, Jukebox *j)
{
    Node *child = NULL;
    j->act_next = rr_action(self->scene, "next_song");
    j->count    = 0;
    j->current  = -1;
    while ((child = rr_node_child(self, child)) != NULL && j->count < JUKEBOX_MAX) {
        if (child->type == RR_NODE_AUDIO) {
            j->songs[j->count++] = child;
        }
    }
}

static void jukebox_play_next(Jukebox *j)
{
    const char *name, *dash;
    int         next;
    size_t      n;

    if (j->count == 0) {
        return;
    }
    next = (int)((uint32_t)rand() % (uint32_t)j->count);
    if (j->count > 1 && next == j->current) {
        next = (next + 1) % j->count;
    }
    if (j->current >= 0) {
        rr_audio_stop(j->songs[j->current]);
    }
    j->current = next;
    rr_audio_play(j->songs[next]);

    /* "Artist - Title" */
    name = j->songs[next]->name;
    dash = strstr(name, " - ");
    n    = dash != NULL ? (size_t)(dash - name) : strlen(name);
    if (n >= sizeof(j->artist)) {
        n = sizeof(j->artist) - 1;
    }
    memcpy(j->artist, name, n);
    j->artist[n] = '\0';
    snprintf(j->title, sizeof(j->title), "%s", dash != NULL ? dash + 3 : "");
    set_text(j->artist_label, j->artist);
    set_text(j->title_label, j->title);
    j->shown = 0.0f;
    show(j->panel, true);
}

static void jukebox_update(Node *self, Jukebox *j, float dt)
{
    if (!visible_in_tree(self)) {
        return; /* plays once the game has begun */
    }
    if (!j->started || rr_action_pressed(j->act_next) ||
        (j->current >= 0 && !rr_audio_is_playing(j->songs[j->current]))) {
        j->started = true;
        jukebox_play_next(j);
    }
    j->shown += dt;
    if (j->shown > j->show_time) {
        show(j->panel, false);
    }
}

COMPONENT_BEGIN(Jukebox, "Jukebox")
    EXPORT_NODE(artist_label)
    EXPORT_NODE(title_label)
    EXPORT_NODE(panel)
    EXPORT_FLOAT(show_time)
    ON_READY(jukebox_ready)
    ON_UPDATE(jukebox_update)
COMPONENT_END()
