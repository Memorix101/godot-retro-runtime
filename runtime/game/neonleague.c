/* Components of Neon League (scenes/neonleague.tscn and neonleague_title.tscn), a port of
 * the Unity game "Neon League" (Freak Mind Games, 2015-2024), whose scenes
 * tools/make_neonleague.gd converts. Placeholder scripts: components/neon_*.gd.
 *
 * A twin-stick arena shooter: the player moves with one hand (the analog stick, the d-pad,
 * WASD) and shoots with the other (the face buttons, the arrow keys). Enemies come out of ten
 * spawners and chase the player; lasers bounce off the walls and hit whatever they meet,
 * the player too. A bomb clears the area around the player. Five stages of 60 seconds with
 * 15 seconds of rest between them, then an endless one; the walls start moving in stage 4
 * and two cones rise in stage 5.
 *
 * NeonGame     the GameManager and StageBehaviour of Unity: stages, score, HUD, pause,
 *              game over; the effects and sounds that the other parts ask for; the walls
 *              and cones (their AnimationPlayers); the camera's background colour.
 * NeonPlayer   a CharacterBody3D: moves, aims its gun, fires lasers, throws bombs, dies.
 * NeonLaser    a RigidBody3D that bounces off the walls (physics material bounce 1); its
 *              Area3D child "Hit" reports enemies, the player and other lasers.
 * NeonEnemy    a CharacterBody3D that heads for the player (Unity's NavMesh was one flat
 *              rectangle, so its path was a straight line) and keeps off other enemies.
 * NeonBomb     an Area3D that grows and shrinks again; enemies it touches die.
 * NeonSpawner  shows its enemy fading in and asks the game for an enemy when it is full.
 * NeonDisco    a mesh whose colour changes: random colours, or a red pulse.
 * NeonFlicker  a control that blinks, or changes its colour every frame.
 * NeonCamera   follows the player from above and shakes, or turns on the spot (title).
 * NeonMenu     the title: play, options (volumes, rumble), credits, exit.
 *
 * The settings (music and sound volume, rumble) are kept in the saved game "NEONLEAGUE". */
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../core/rr_scene.h"

#define DEG2RAD 0.017453292f

#define NEON_SETTINGS_NAME  "NEONLEAGUE"
#define NEON_SETTINGS_MAGIC 0x4E454F31u /* "NEO1" */

static float clampf_(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static float frand(float lo, float hi)
{
    return lo + (hi - lo) * ((float)rand() / (float)RAND_MAX);
}

static float ping_pong(float t, float length)
{
    const float m = fmodf(t, length * 2.0f);
    return m < length ? m : length * 2.0f - m;
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

static void place(Node *node, const float *at)
{
    node->local.m[9]  = at[0];
    node->local.m[10] = at[1];
    node->local.m[11] = at[2];
}

/* A yaw basis that turns -Z to the direction (dx, dz), with a uniform scale. */
static void set_facing(Node *node, float dx, float dz, float scale)
{
    float *m = node->local.m;
    float  len = sqrtf(dx * dx + dz * dz);
    float  s, c;

    if (len < 1e-5f) {
        return;
    }
    /* Ry(t) * (0, 0, -1) = (-sin t, 0, -cos t) */
    s = -dx / len;
    c = -dz / len;
    m[0] = c * scale;  m[1] = 0.0f;   m[2] = -s * scale;
    m[3] = 0.0f;       m[4] = scale;  m[5] = 0.0f;
    m[6] = s * scale;  m[7] = 0.0f;   m[8] = c * scale;
}

static void set_scale(Node *node, float scale)
{
    float *m = node->local.m;
    memset(m, 0, 9 * sizeof(float));
    m[0] = m[4] = m[8] = scale;
}

/* The node of an area's owner: the area itself or the body it belongs to. */
static Node *owner_of(Node *area, const char *group)
{
    if (area == NULL) {
        return NULL;
    }
    if (rr_node_has_tag(area, group)) {
        return area;
    }
    if (area->parent != NULL && rr_node_has_tag(area->parent, group)) {
        return area->parent;
    }
    return NULL;
}

/* --- Settings ------------------------------------------------------------------------- */

typedef struct {
    uint32_t magic;
    float    music, sound;
    uint32_t rumble;
} NeonSettings;

static NeonSettings g_settings = { NEON_SETTINGS_MAGIC, 0.8f, 0.8f, 1u };
static bool         g_settings_read;

static void settings_apply(RRScene *scene)
{
    if (!g_settings_read) {
        NeonSettings s;
        g_settings_read = true;
        if (rr_save_read(NEON_SETTINGS_NAME, &s, sizeof(s)) && s.magic == NEON_SETTINGS_MAGIC) {
            g_settings = s;
        }
    }
    rr_audio_bus_set_volume(scene, rr_audio_bus_index(scene, "Music"), g_settings.music);
    rr_audio_bus_set_volume(scene, rr_audio_bus_index(scene, "SFX"), g_settings.sound);
}

static void settings_save(void)
{
    rr_save_write(NEON_SETTINGS_NAME, &g_settings, sizeof(g_settings));
}

/* The controls of the settings show what is set. */
static void settings_show(Node *music, Node *sound, Node *rumble)
{
    if (music != NULL) {
        music->value = g_settings.music;
    }
    if (sound != NULL) {
        sound->value = g_settings.sound;
    }
    if (rumble != NULL) {
        rr_button_set_pressed(rumble, g_settings.rumble != 0);
    }
}

static void rumble(float weak, float strong, float seconds)
{
    if (g_settings.rumble) {
        rr_input_vibrate(weak, strong, seconds);
    }
}

/* --- NeonGame ------------------------------------------------------------------------- */

#define BURST_KINDS  4
#define BURST_COPIES 8

enum { BURST_BOOM = 0, BURST_BLAST, BURST_EXPLOSION, BURST_SPAWN };

typedef struct {
    Node *player;          /* exported */
    Node *camera;          /* exported */
    Node *hud;             /* exported: hidden at the game over */
    Node *score_label;     /* exported */
    Node *lives_label;     /* exported */
    Node *bombs_label;     /* exported */
    Node *stage_label;     /* exported */
    Node *survive_label;   /* exported */
    Node *time_label;      /* exported */
    Node *ready_picture;   /* exported: the picture at the start */
    Node *win;             /* exported: shown after stage 5 */
    Node *panel;           /* exported: darkens the game at the game over */
    Node *game_over;       /* exported */
    Node *final_score;     /* exported */
    Node *pause_menu;      /* exported */
    Node *resume_button;   /* exported */
    Node *music_slider;    /* exported */
    Node *sound_slider;    /* exported */
    Node *rumble_toggle;   /* exported */
    Node *walls;           /* exported: their AnimationPlayers play "Move" */
    Node *cones;           /* exported: their AnimationPlayers play "Cone_Rising", "Cone_Fading" */
    Node *bursts;          /* exported: Boom, Blast, Explosion, Spawn, each with its copies */
    Node *enemy_sound;     /* exported: an enemy explodes */
    Node *blast_sound;     /* exported: a laser hits, a bomb kills */
    Node *death_sound;     /* exported: the player explodes */
    const RRPackedScene *enemy; /* exported */
    int32_t enemy_max;          /* exported */
    const char *restart_scene;  /* exported */
    const char *title_scene;    /* exported */
    float background_time;      /* exported: seconds between two background colours */

    int    act_pause, act_restart, act_back;
    int    stage;
    float  timer;
    float  spawn_interval;
    bool   intermission, move_walls;
    float  start_in, next_in;
    bool   paused, over, respawning;
    float  over_in;
    int32_t score, lives, bombs;
    float  alive_time;
    float  background_left;
    float  ready_time;
    Node  *bursts_of[BURST_KINDS][BURST_COPIES];
    int    burst_count[BURST_KINDS], burst_next[BURST_KINDS];
    char   score_text[16], lives_text[8], bombs_text[8], stage_text[24], time_text[8], final_text[16];
    const char *wall_state, *cone_state;
} NeonGame;

static NeonGame *g_game;

static NeonGame *game_of(const Node *node)
{
    /* The game of the scene the node is in; NULL in the title. */
    return g_game != NULL && g_game->player != NULL && g_game->player->scene == node->scene ? g_game : NULL;
}

static void game_burst(NeonGame *g, int kind, const float *at)
{
    Node *burst, *child = NULL;

    if (g == NULL || g->burst_count[kind] == 0) {
        return;
    }
    burst = g->bursts_of[kind][g->burst_next[kind]];
    g->burst_next[kind] = (g->burst_next[kind] + 1) % g->burst_count[kind];
    place(burst, at);
    if (burst->type == RR_NODE_PARTICLES) {
        rr_particles_restart(burst);
    }
    while ((child = rr_node_child(burst, child)) != NULL) {
        if (child->type == RR_NODE_PARTICLES) {
            rr_particles_restart(child);
        }
    }
}

static void game_show_numbers(NeonGame *g)
{
    snprintf(g->score_text, sizeof(g->score_text), "%06d", (int)g->score);
    snprintf(g->lives_text, sizeof(g->lives_text), "%d", (int)g->lives);
    snprintf(g->bombs_text, sizeof(g->bombs_text), "%d", (int)g->bombs);
    set_text(g->score_label, g->score_text);
    set_text(g->lives_label, g->lives_text);
    set_text(g->bombs_label, g->bombs_text);
}

static void game_add_score(NeonGame *g, int points)
{
    if (g != NULL && !g->over) {
        g->score += points;
        game_show_numbers(g);
    }
}

static void game_set_stage(NeonGame *g, int stage)
{
    g->stage = stage;
    g->timer = stage > 5 ? 800.0f : 60.0f;
    g->spawn_interval = stage == 1 ? 5.0f : (stage == 2 ? 3.0f : 2.0f);
    g->move_walls = stage >= 4;
    if (stage > 5) {
        snprintf(g->stage_text, sizeof(g->stage_text), "ENDLESS MODE");
    } else {
        snprintf(g->stage_text, sizeof(g->stage_text), "STAGE %d", stage);
    }
    set_text(g->stage_label, g->stage_text);
    printf("neonleague: stage %d\n", stage);
}

static void game_ready(Node *self, NeonGame *g)
{
    Node *kind = NULL;
    int   k = 0;

    g_game         = g;
    g->act_pause   = rr_action(self->scene, "nl_pause");
    g->act_restart = rr_action(self->scene, "nl_restart");
    g->act_back    = rr_action(self->scene, "nl_back");
    g->stage = 0;
    g->timer = 0.0f;
    g->intermission = true;
    g->move_walls = false;
    g->start_in = 2.0f;
    g->next_in = 0.0f;
    g->paused = g->over = g->respawning = false;
    g->over_in = 0.0f;
    g->score = 0;
    g->lives = 2;
    g->bombs = 3;
    g->alive_time = 0.0f;
    g->background_left = 1.0f;
    g->ready_time = 0.0f;
    g->wall_state = g->cone_state = "";
    rr_set_time_scale(1.0f);
    settings_apply(self->scene);
    memset(g->burst_count, 0, sizeof(g->burst_count));
    memset(g->burst_next, 0, sizeof(g->burst_next));
    while (g->bursts != NULL && (kind = rr_node_child(g->bursts, kind)) != NULL && k < BURST_KINDS) {
        Node *copy = NULL;
        while ((copy = rr_node_child(kind, copy)) != NULL && g->burst_count[k] < BURST_COPIES) {
            g->bursts_of[k][g->burst_count[k]++] = copy;
        }
        k++;
    }
    show(g->win, false);
    show(g->panel, false);
    show(g->game_over, false);
    show(g->pause_menu, false);
    set_text(g->stage_label, "");
    set_text(g->survive_label, "");
    game_show_numbers(g);
}

/* Plays a clip on every AnimationPlayer below `holder` (NULL clip: holds them where they
 * are). */
static void game_animate(Node *holder, const char *clip, float speed)
{
    Node *child = NULL;
    while (holder != NULL && (child = rr_node_child(holder, child)) != NULL) {
        if (child->type == RR_NODE_ANIMATION_PLAYER) {
            if (clip != NULL && (rr_anim_current(child) == NULL || strcmp(rr_anim_current(child), clip) != 0 ||
                                 !rr_anim_playing(child))) {
                rr_anim_play(child, clip);
            }
            rr_anim_set_speed_scale(child, speed);
        } else {
            game_animate(child, clip, speed);
        }
    }
}

static void game_walls(NeonGame *g)
{
    /* Unity's LerpMover: the walls move only while move_walls is on; the cones rise in
     * stage 5 and later while it is on and sink again when it goes off. */
    const char *cones = g->stage >= 5 ? (g->move_walls ? "Cone_Rising" : "Cone_Fading") : "";
    if (g->move_walls) {
        game_animate(g->walls, "Move", 1.0f);
    } else {
        game_animate(g->walls, NULL, 0.0f);
    }
    if (strcmp(cones, g->cone_state) != 0) {
        g->cone_state = cones;
        if (cones[0] != '\0') {
            game_animate(g->cones, cones, 1.0f);
        }
    }
}

static void game_pause(NeonGame *g, bool pause)
{
    g->paused = pause;
    show(g->pause_menu, pause);
    rr_set_time_scale(pause ? 0.0f : 1.0f);
    if (pause) {
        settings_show(g->music_slider, g->sound_slider, g->rumble_toggle);
        rr_control_grab_focus(g->resume_button);
    } else {
        settings_save();
    }
}

static void game_leave(const char *scene)
{
    rr_set_time_scale(1.0f);
    settings_save();
    rr_change_scene(scene);
}

static void game_update(Node *self, NeonGame *g, float dt)
{
    bool alive = g->player != NULL && !g->over;

    /* The camera's background: a dark random colour every second (DiscoBackground). */
    g->background_left -= dt;
    if (g->background_left <= 0.0f && g->background_time > 0.0f) {
        const float c[4] = { frand(0.0f, 0.25f), frand(0.0f, 0.25f), frand(0.0f, 0.25f), 1.0f };
        rr_environment_set_background_color(self->scene, c);
        g->background_left = g->background_time;
    }
    /* The picture at the start blinks for two seconds. */
    if (g->ready_picture != NULL && visible_in_tree(g->ready_picture)) {
        const float c[4] = { 1.0f, 1.0f, 1.0f, ping_pong(g->ready_time * 30.0f, 1.0f) };
        g->ready_time += dt;
        rr_canvas_item_set_modulate(g->ready_picture, c);
        if (g->ready_time >= 2.0f) {
            show(g->ready_picture, false);
        }
    }

    if (g->over) {
        if (g->over_in > 0.0f) {
            g->over_in -= dt;
            if (g->over_in <= 0.0f) {
                show(g->panel, true);
                show(g->game_over, true);
                show(g->hud, false);
                snprintf(g->final_text, sizeof(g->final_text), "%06d", (int)g->score);
                printf("neonleague: game over, score %d\n", (int)g->score);
                set_text(g->final_score, g->final_text);
            }
        } else if (rr_action_pressed(g->act_restart)) {
            game_leave(g->restart_scene);
        } else if (rr_action_pressed(g->act_back)) {
            game_leave(g->title_scene);
        }
        return;
    }
    if (rr_action_pressed(g->act_pause)) {
        game_pause(g, !g->paused);
    }
    if (g->paused) {
        return;
    }

    if (g->start_in > 0.0f) {
        g->start_in -= dt;
        if (g->start_in <= 0.0f) {
            game_set_stage(g, 1);
            g->intermission = false;
        }
    }
    if (g->next_in > 0.0f) {
        g->next_in -= dt;
        if (g->next_in <= 0.0f) {
            /* The next stage, with a full set of bombs. */
            g->bombs = 3;
            show(g->win, false);
            g->intermission = false;
            game_set_stage(g, g->stage + 1);
            game_show_numbers(g);
        }
    }
    if (alive) {
        /* Ten points for every second alive. */
        g->alive_time += dt;
        while (g->alive_time >= 1.0f) {
            g->alive_time -= 1.0f;
            game_add_score(g, 10);
        }
        g->timer -= dt;
    }
    if (g->timer < 0.0f && !g->intermission && g->stage > 0) {
        /* The stage is survived: 15 seconds of rest; after stage 5 the player has won. */
        if (g->stage == 5) {
            show(g->win, true);
        }
        g->intermission = true;
        g->timer = 15.0f;
        g->move_walls = false;
        g->next_in = 15.0f;
    }
    set_text(g->survive_label, g->stage == 0 ? "" : (g->intermission ? "intermission" : "time to survive"));
    if (g->stage > 0) {
        if (g->timer < 0.0f) {
            snprintf(g->time_text, sizeof(g->time_text), "---");
        } else {
            snprintf(g->time_text, sizeof(g->time_text), "%d", (int)g->timer);
        }
        set_text(g->time_label, g->time_text);
    }
    game_walls(g);
}

static void game_on_resume(Node *self, NeonGame *g)
{
    (void)self;
    game_pause(g, false);
}

static void game_on_menu(Node *self, NeonGame *g)
{
    (void)self;
    game_leave(g->title_scene);
}

static void game_on_music(Node *self, NeonGame *g, Node *slider)
{
    (void)g;
    g_settings.music = slider->value;
    settings_apply(self->scene);
}

static void game_on_sound(Node *self, NeonGame *g, Node *slider)
{
    (void)g;
    g_settings.sound = slider->value;
    settings_apply(self->scene);
}

static void game_on_rumble(Node *self, NeonGame *g, Node *toggle)
{
    (void)self;
    (void)g;
    g_settings.rumble = rr_button_is_pressed(toggle) ? 1u : 0u;
}

COMPONENT_BEGIN(NeonGame, "NeonGame")
    EXPORT_NODE(player)
    EXPORT_NODE(camera)
    EXPORT_NODE(hud)
    EXPORT_NODE(score_label)
    EXPORT_NODE(lives_label)
    EXPORT_NODE(bombs_label)
    EXPORT_NODE(stage_label)
    EXPORT_NODE(survive_label)
    EXPORT_NODE(time_label)
    EXPORT_NODE(ready_picture)
    EXPORT_NODE(win)
    EXPORT_NODE(panel)
    EXPORT_NODE(game_over)
    EXPORT_NODE(final_score)
    EXPORT_NODE(pause_menu)
    EXPORT_NODE(resume_button)
    EXPORT_NODE(music_slider)
    EXPORT_NODE(sound_slider)
    EXPORT_NODE(rumble_toggle)
    EXPORT_NODE(walls)
    EXPORT_NODE(cones)
    EXPORT_NODE(bursts)
    EXPORT_NODE(enemy_sound)
    EXPORT_NODE(blast_sound)
    EXPORT_NODE(death_sound)
    EXPORT_PACKED_SCENE(enemy)
    EXPORT_INT(enemy_max)
    EXPORT_SCENE(restart_scene)
    EXPORT_SCENE(title_scene)
    EXPORT_FLOAT(background_time)
    ON_READY(game_ready)
    ON_UPDATE(game_update)
    ON_SIGNAL("_on_resume_pressed", game_on_resume)
    ON_SIGNAL("_on_menu_pressed", game_on_menu)
    ON_SIGNAL_NODE("_on_music_value_changed", game_on_music)
    ON_SIGNAL_NODE("_on_sound_value_changed", game_on_sound)
    ON_SIGNAL_NODE("_on_rumble_toggled", game_on_rumble)
COMPONENT_END()

/* --- NeonCamera ----------------------------------------------------------------------- */

typedef struct {
    Node *target;       /* exported: followed along X and Z; none: the camera turns on the spot */
    float back;         /* exported: metres behind the target (+Z) */
    float shake_amount; /* exported: metres to the sides while it shakes */
    float spin_speed;   /* exported: degrees per second about the world's up axis, without a target */

    float shake_left;
    float side; /* the shake's offset this frame */
    float yaw;
    RRMat34 start;
} NeonCamera;

static void camera_ready(Node *self, NeonCamera *c)
{
    c->shake_left = 0.0f;
    c->side = 0.0f;
    c->yaw = 0.0f;
    c->start = self->local;
}

static void camera_update(Node *self, NeonCamera *c, float dt)
{
    float *m = self->local.m;

    if (c->target == NULL) {
        /* Unity's Rotater: turns about the world's up axis at its place. */
        const float a = c->yaw * DEG2RAD, s = sinf(a), co = cosf(a);
        const float *b = c->start.m;
        int          k;
        c->yaw += c->spin_speed * dt;
        for (k = 0; k < 3; k++) {
            /* Ry(a) * column k */
            const float x = b[k * 3], y = b[k * 3 + 1], z = b[k * 3 + 2];
            m[k * 3]     = co * x + s * z;
            m[k * 3 + 1] = y;
            m[k * 3 + 2] = -s * x + co * z;
        }
        return;
    }
    if (c->shake_left > 0.0f) {
        c->side = frand(-1.0f, 1.0f) * c->shake_amount;
        c->shake_left -= dt;
    } else {
        c->side = 0.0f;
    }
    if (visible_in_tree(c->target)) {
        m[9]  = c->target->local.m[9] + c->side;
        m[11] = c->target->local.m[11] + c->back;
    }
}

static void camera_shake(Node *camera, float seconds)
{
    NeonCamera *c = camera != NULL ? (NeonCamera *)rr_node_component(camera, "NeonCamera") : NULL;
    if (c != NULL && seconds > c->shake_left) {
        c->shake_left = seconds;
    }
}

COMPONENT_BEGIN(NeonCamera, "NeonCamera")
    EXPORT_NODE(target)
    EXPORT_FLOAT(back)
    EXPORT_FLOAT(shake_amount)
    EXPORT_FLOAT(spin_speed)
    ON_READY(camera_ready)
    ON_UPDATE(camera_update)
COMPONENT_END()

/* --- NeonPlayer ----------------------------------------------------------------------- */

typedef struct {
    float speed;           /* exported: metres per second */
    Node *gun;             /* exported: turned to where the player aims */
    float muzzle;          /* exported: metres from the middle where a laser starts */
    float laser_speed;     /* exported */
    float fire_interval;   /* exported: seconds between two lasers */
    const RRPackedScene *laser; /* exported */
    int32_t laser_max;          /* exported */
    const RRPackedScene *bomb;  /* exported */
    int32_t bomb_max;           /* exported */
    Node *spawn_sound;     /* exported: plays when the player appears */
    Node *fire_sound;      /* exported */
    Node *bomb_sound;      /* exported */

    int   act_left, act_right, act_up, act_down;
    int   aim_left, aim_right, aim_up, aim_down, act_bomb;
    float fire_left;
    float spawn[3];
    float respawn_in;
    bool  dead;
    bool  appeared; /* the spawn effect of the start has been shown */
} NeonPlayer;

static void player_ready(Node *self, NeonPlayer *p)
{
    RRScene *s = self->scene;
    p->act_left  = rr_action(s, "nl_move_left");
    p->act_right = rr_action(s, "nl_move_right");
    p->act_up    = rr_action(s, "nl_move_up");
    p->act_down  = rr_action(s, "nl_move_down");
    p->aim_left  = rr_action(s, "nl_aim_left");
    p->aim_right = rr_action(s, "nl_aim_right");
    p->aim_up    = rr_action(s, "nl_aim_up");
    p->aim_down  = rr_action(s, "nl_aim_down");
    p->act_bomb  = rr_action(s, "nl_bomb");
    p->fire_left = 0.0f;
    p->spawn[0] = self->local.m[9];
    p->spawn[1] = self->local.m[10];
    p->spawn[2] = self->local.m[11];
    p->respawn_in = 0.0f;
    p->dead = false;
    p->appeared = false;
}

static void player_appear(Node *self, NeonPlayer *p)
{
    NeonGame *g = game_of(self);
    place(self, p->spawn);
    show(self, true);
    p->dead = false;
    play(p->spawn_sound);
    game_burst(g, BURST_SPAWN, p->spawn);
    if (g != NULL) {
        g->respawning = false;
    }
}

static void player_die(Node *self, NeonPlayer *p)
{
    NeonGame   *g = game_of(self);
    const float at[3] = { self->local.m[9], self->local.m[10], self->local.m[11] };

    if (p->dead || g == NULL || g->over) {
        return;
    }
    p->dead = true;
    rumble(1.0f, 1.0f, 0.5f);
    show(self, false);
    game_burst(g, BURST_EXPLOSION, at);
    play(g->death_sound);
    printf("neonleague: died, %d lives left\n", (int)g->lives);
    if (g->lives <= 0) {
        /* The end: slow motion, and the game over a second (of game time) later. */
        g->over = true;
        g->over_in = 1.0f;
        rr_set_time_scale(0.25f);
    } else {
        g->lives--;
        g->respawning = true;
        p->respawn_in = 2.0f;
        game_show_numbers(g);
    }
}

static void player_fire(Node *self, NeonPlayer *p, float dx, float dz)
{
    Node       *laser = rr_instantiate(p->laser);
    const float len = sqrtf(dx * dx + dz * dz);
    float       at[3], v[3];

    if (laser == NULL || len < 1e-5f) {
        return;
    }
    dx /= len;
    dz /= len;
    at[0] = self->local.m[9] + dx * p->muzzle;
    at[1] = self->local.m[10];
    at[2] = self->local.m[11] + dz * p->muzzle;
    place(laser, at);
    v[0] = dx * p->laser_speed;
    v[1] = 0.0f;
    v[2] = dz * p->laser_speed;
    rr_rigid_body_set_linear_velocity(laser, v);
    play(p->fire_sound);
}

static void player_update(Node *self, NeonPlayer *p, float dt)
{
    NeonGame *g = game_of(self);
    float     v[3] = { 0.0f, 0.0f, 0.0f }, ax, az;

    if (g == NULL || g->paused || g->over) {
        return;
    }
    if (!p->appeared) {
        /* Unity's Player.Start: the spawn effect and the teleport sound. */
        p->appeared = true;
        player_appear(self, p);
    }
    if (p->dead) {
        if (p->respawn_in > 0.0f) {
            p->respawn_in -= dt;
            if (p->respawn_in <= 0.0f) {
                player_appear(self, p);
            }
        }
        return;
    }

    v[0] = (rr_action_strength(p->act_right) - rr_action_strength(p->act_left)) * p->speed;
    v[2] = (rr_action_strength(p->act_down) - rr_action_strength(p->act_up)) * p->speed;
    rr_body_move_and_slide(self, v, dt);
    self->local.m[10] = p->spawn[1];

    /* The gun turns to the aim; it fires while the stick is pushed (Unity: past 0.06). */
    ax = rr_action_strength(p->aim_right) - rr_action_strength(p->aim_left);
    az = rr_action_strength(p->aim_down) - rr_action_strength(p->aim_up);
    p->fire_left -= dt;
    if (ax * ax + az * az > 0.06f * 0.06f) {
        if (p->gun != NULL) {
            set_facing(p->gun, ax, az, 1.0f);
        }
        if (p->fire_left <= 0.0f) {
            p->fire_left = p->fire_interval;
            player_fire(self, p, ax, az);
        }
    }

    if (rr_action_pressed(p->act_bomb) && g->bombs > 0) {
        Node *bomb = rr_instantiate(p->bomb);
        if (bomb != NULL) {
            const float at[3] = { self->local.m[9], self->local.m[10], self->local.m[11] };
            place(bomb, at);
            g->bombs--;
            game_show_numbers(g);
            play(p->bomb_sound);
        }
    }
}

static void player_on_hit(Node *self, NeonPlayer *p, Node *area)
{
    if (owner_of(area, "enemy") != NULL || owner_of(area, "deadly") != NULL) {
        player_die(self, p);
    }
}

COMPONENT_BEGIN(NeonPlayer, "NeonPlayer")
    EXPORT_FLOAT(speed)
    EXPORT_NODE(gun)
    EXPORT_FLOAT(muzzle)
    EXPORT_FLOAT(laser_speed)
    EXPORT_FLOAT(fire_interval)
    EXPORT_PACKED_SCENE(laser)
    EXPORT_INT(laser_max)
    EXPORT_PACKED_SCENE(bomb)
    EXPORT_INT(bomb_max)
    EXPORT_NODE(spawn_sound)
    EXPORT_NODE(fire_sound)
    EXPORT_NODE(bomb_sound)
    ON_READY(player_ready)
    ON_UPDATE(player_update)
    ON_SIGNAL_NODE("_on_hitbox_area_entered", player_on_hit)
COMPONENT_END()

/* --- NeonEnemy ------------------------------------------------------------------------ */

typedef struct {
    float speed;        /* exported: metres per second */
    float acceleration; /* exported */
    float turn_speed;   /* exported: degrees per second the look turns to where it goes */
    float radius;       /* exported: how close other enemies may come */
    Node *look;         /* exported: the mesh, turned to where it goes */

    float vel[3];
    float yaw;
    bool  dead;
} NeonEnemy;

static void enemy_ready(Node *self, NeonEnemy *e)
{
    (void)self;
    memset(e->vel, 0, sizeof(e->vel));
    e->yaw = 0.0f;
    e->dead = false;
}

/* Unity's Die (killed: points, a shake) and Suicide (the stage ended, the player was hit). */
static void enemy_die(Node *self, NeonEnemy *e, bool killed)
{
    NeonGame   *g = game_of(self);
    const float at[3] = { self->local.m[9], self->local.m[10], self->local.m[11] };

    if (e->dead) {
        return;
    }
    e->dead = true;
    game_burst(g, BURST_BOOM, at);
    if (g != NULL) {
        play(g->enemy_sound);
    }
    if (killed) {
        game_add_score(g, 100);
        if (g != NULL) {
            camera_shake(g->camera, 0.2f);
        }
        rumble(0.5f, 0.5f, 0.2f);
    }
    rr_queue_free(self);
}

static void enemy_update(Node *self, NeonEnemy *e, float dt)
{
    NeonGame *g = game_of(self);
    Node     *other = NULL;
    float     want[3] = { 0.0f, 0.0f, 0.0f }, dx, dz, len, step, target, d;

    if (g == NULL || e->dead) {
        return;
    }
    if (g->intermission || g->respawning) {
        enemy_die(self, e, false);
        return;
    }
    if (g->paused || dt <= 0.0f) {
        return;
    }
    if (visible_in_tree(g->player)) {
        dx = g->player->local.m[9] - self->local.m[9];
        dz = g->player->local.m[11] - self->local.m[11];
        len = sqrtf(dx * dx + dz * dz);
        if (len > 1e-4f) {
            want[0] = dx / len * e->speed;
            want[2] = dz / len * e->speed;
        }
    }
    /* Keep off the others, as Unity's agents avoided each other. */
    while ((other = rr_scene_find_tagged(self->scene, "enemy", other)) != NULL) {
        float ox, oz, dist;
        if (other == self || (other->flags & RR_NODE_FLAG_FREE)) {
            continue;
        }
        ox = self->local.m[9] - other->local.m[9];
        oz = self->local.m[11] - other->local.m[11];
        dist = sqrtf(ox * ox + oz * oz);
        if (dist < e->radius * 2.0f && dist > 1e-4f) {
            const float push = (e->radius * 2.0f - dist) / (e->radius * 2.0f) * e->speed * 1.5f;
            want[0] += ox / dist * push;
            want[2] += oz / dist * push;
        }
    }
    /* Towards the wanted velocity at the agent's acceleration. */
    dx = want[0] - e->vel[0];
    dz = want[2] - e->vel[2];
    len = sqrtf(dx * dx + dz * dz);
    step = e->acceleration * dt;
    if (len > step) {
        dx *= step / len;
        dz *= step / len;
    }
    e->vel[0] += dx;
    e->vel[2] += dz;
    e->vel[1] = 0.0f;
    rr_body_move_and_slide(self, e->vel, dt);

    if (e->look != NULL && (e->vel[0] != 0.0f || e->vel[2] != 0.0f)) {
        /* The look turns to where it goes at the agent's angular speed. */
        target = atan2f(-e->vel[0], -e->vel[2]) / DEG2RAD;
        d = fmodf(target - e->yaw + 540.0f, 360.0f) - 180.0f;
        e->yaw += clampf_(d, -e->turn_speed * dt, e->turn_speed * dt);
        set_facing(e->look, -sinf(e->yaw * DEG2RAD), -cosf(e->yaw * DEG2RAD), e->look->local.m[4]);
    }
}

static NeonEnemy *enemy_of(Node *area, Node **owner)
{
    Node *node = owner_of(area, "enemy");
    if (owner != NULL) {
        *owner = node;
    }
    return node != NULL ? (NeonEnemy *)rr_node_component(node, "NeonEnemy") : NULL;
}

COMPONENT_BEGIN(NeonEnemy, "NeonEnemy")
    EXPORT_FLOAT(speed)
    EXPORT_FLOAT(acceleration)
    EXPORT_FLOAT(turn_speed)
    EXPORT_FLOAT(radius)
    EXPORT_NODE(look)
    ON_READY(enemy_ready)
    ON_UPDATE(enemy_update)
COMPONENT_END()

/* --- NeonLaser ------------------------------------------------------------------------ */

#define TRAIL_POINTS 16 /* bounce points remembered for the trail */
#define TRAIL_PIECES 32
#define TRAIL_FADE   0.5f /* seconds a trail takes to go once its laser has ended */

typedef struct {
    float speed;       /* exported: kept after every bounce */
    float lifetime;    /* exported: seconds */
    Node *ball;        /* exported: hidden when the laser ends, while its trail fades */
    Node *trail;       /* exported: its children are the pieces of the trail */
    float trail_time;  /* exported: seconds of flight the trail shows */
    float trail_width; /* exported: metres at the laser; nothing at the end */

    float age;
    bool  done;    /* ended: no more hits; the trail shrinks away */
    float fade;    /* seconds of trail left after the end */
    bool  anchored;
    float dir[2];                  /* the direction of the flight before this frame */
    float points[TRAIL_POINTS][2]; /* X and Z of the start and the bounces, newest first */
    int   point_count;
    Node *pieces[TRAIL_PIECES];
    int   piece_count;
} NeonLaser;

static void laser_ready(Node *self, NeonLaser *l)
{
    Node *child = NULL;
    (void)self;
    l->age = 0.0f;
    l->done = false;
    l->fade = 0.0f;
    l->anchored = false;
    l->point_count = 0;
    l->piece_count = 0;
    show(l->ball, true);
    while (l->trail != NULL && (child = rr_node_child(l->trail, child)) != NULL && l->piece_count < TRAIL_PIECES) {
        if (child->type == RR_NODE_MESH) {
            show(child, false);
            l->pieces[l->piece_count++] = child;
        }
    }
}

/* A new point of the way when the laser starts or bounces. */
static void laser_track(Node *self, NeonLaser *l, const float *v)
{
    const float len = sqrtf(v[0] * v[0] + v[2] * v[2]);

    if (len < 1e-3f) {
        return;
    }
    if (!l->anchored || (v[0] * l->dir[0] + v[2] * l->dir[1]) / len < 0.999f) {
        memmove(l->points[1], l->points[0], sizeof(l->points[0]) * (TRAIL_POINTS - 1));
        l->points[0][0] = self->local.m[9];
        l->points[0][1] = self->local.m[11];
        if (l->point_count < TRAIL_POINTS) {
            l->point_count++;
        }
        l->anchored = true;
    }
    l->dir[0] = v[0] / len;
    l->dir[1] = v[2] / len;
}

/* Unity's TrailRenderer: the way the laser came in the last `trail_time` seconds, through
 * its bounces, narrowing to a point; once the laser has ended, it shrinks to where it
 * ended (Unity set the trail's time to 0.5 s then). Each piece is an equal part of that
 * way: a ribbon one metre long along +Z and one metre wide at its front, put from one
 * point of the way to the next, coloured by how far back it is. */
static void laser_trail(Node *self, NeonLaser *l, float length)
{
    const float x = self->local.m[9], z = self->local.m[11];
    float       from[2], step;
    int         seg = 0, k;

    step = length / (float)(l->piece_count > 0 ? l->piece_count : 1);
    from[0] = x;
    from[1] = z;
    for (k = 0; k < l->piece_count; k++) {
        Node *piece = l->pieces[k];
        float to[2], want = step, dx, dz, d;

        /* The end of this piece: `step` further back along the way. */
        to[0] = from[0];
        to[1] = from[1];
        while (want > 0.0f && seg < l->point_count) {
            const float *p = l->points[seg];
            const float  rest = sqrtf((p[0] - to[0]) * (p[0] - to[0]) + (p[1] - to[1]) * (p[1] - to[1]));
            if (rest > want) {
                to[0] += (p[0] - to[0]) * want / rest;
                to[1] += (p[1] - to[1]) * want / rest;
                want = 0.0f;
            } else {
                to[0] = p[0];
                to[1] = p[1];
                want -= rest;
                seg++;
            }
        }
        dx = from[0] - to[0];
        dz = from[1] - to[1];
        d = sqrtf(dx * dx + dz * dz);
        if (d < 0.02f) {
            show(piece, false);
            continue;
        }
        {
            /* The piece's front: 0 at the laser, 1 at the end. Its mesh narrows on to
             * the width of the next piece, so the trail ends in a point. */
            const float t = (float)k / (float)l->piece_count;
            const float color[4] = { 0.41f - 0.13f * t, 1.0f, 0.92f + 0.08f * t, 0.9f - 0.5f * t };
            const float width = l->trail_width * (1.0f - t);
            float      *m = piece->local.m;
            show(piece, true);
            /* At `from`, -Z towards the laser (its +Z runs back along the way). */
            set_facing(piece, dx, dz, 1.0f);
            m[0] *= width;
            m[1] *= width;
            m[2] *= width;
            m[6] *= d;
            m[7] *= d;
            m[8] *= d;
            m[9]  = from[0] - x;
            m[10] = 0.0f;
            m[11] = from[1] - z;
            rr_mesh_set_albedo_color(piece, color);
        }
        from[0] = to[0];
        from[1] = to[1];
    }
}

/* The laser stops where it is and hides its ball; its trail shrinks away, then it is
 * freed. */
static void laser_end(Node *self, NeonLaser *l, bool burst)
{
    const float still[3] = { 0.0f, 0.0f, 0.0f };

    if (l->done) {
        return;
    }
    l->done = true;
    l->fade = l->age < TRAIL_FADE ? l->age : TRAIL_FADE;
    show(l->ball, false);
    rr_rigid_body_set_linear_velocity(self, still);
    if (burst) {
        NeonGame   *g = game_of(self);
        const float at[3] = { self->local.m[9], self->local.m[10], self->local.m[11] };
        game_burst(g, BURST_BLAST, at);
        if (g != NULL) {
            play(g->blast_sound);
        }
    }
}

static void laser_update(Node *self, NeonLaser *l, float dt)
{
    NeonGame *g = game_of(self);
    float     v[3], len, seconds;

    if (l->done) {
        const float still[3] = { 0.0f, 0.0f, 0.0f };
        l->fade -= dt;
        if (l->fade <= 0.0f) {
            rr_queue_free(self);
            return;
        }
        rr_rigid_body_set_linear_velocity(self, still);
        laser_trail(self, l, l->speed * l->fade);
        return;
    }
    l->age += dt;
    if (l->age > l->lifetime || (g != NULL && (g->respawning || g->over))) {
        laser_end(self, l, false);
        return;
    }
    /* Flat and at full speed: Unity froze its height, and its bounce kept the speed. */
    rr_rigid_body_get_linear_velocity(self, v);
    len = sqrtf(v[0] * v[0] + v[2] * v[2]);
    if (len > 1e-3f) {
        v[0] *= l->speed / len;
        v[2] *= l->speed / len;
        v[1] = 0.0f;
        rr_rigid_body_set_linear_velocity(self, v);
        laser_track(self, l, v);
    }
    seconds = l->age < l->trail_time ? l->age : l->trail_time;
    laser_trail(self, l, l->speed * seconds);
}

static void laser_on_hit(Node *self, NeonLaser *l, Node *area)
{
    Node      *enemy_node, *player, *other;
    NeonEnemy *enemy = enemy_of(area, &enemy_node);

    if (l->done) {
        return;
    }
    if (enemy != NULL) {
        enemy_die(enemy_node, enemy, true);
        laser_end(self, l, true);
    } else if ((player = owner_of(area, "player")) != NULL) {
        NeonPlayer *p = (NeonPlayer *)rr_node_component(player, "NeonPlayer");
        if (p != NULL && !p->dead) {
            player_die(player, p);
            laser_end(self, l, true);
        }
    } else if ((other = owner_of(area, "laser")) != NULL) {
        /* Two lasers end each other, unless the other one has already ended. */
        NeonLaser *o = (NeonLaser *)rr_node_component(other, "NeonLaser");
        if (o != NULL && !o->done) {
            laser_end(self, l, true);
        }
    }
}

COMPONENT_BEGIN(NeonLaser, "NeonLaser")
    EXPORT_FLOAT(speed)
    EXPORT_FLOAT(lifetime)
    EXPORT_NODE(ball)
    EXPORT_NODE(trail)
    EXPORT_FLOAT(trail_time)
    EXPORT_FLOAT(trail_width)
    ON_READY(laser_ready)
    ON_UPDATE(laser_update)
    ON_SIGNAL_NODE("_on_hit_area_entered", laser_on_hit)
COMPONENT_END()

/* --- NeonBomb ------------------------------------------------------------------------- */

typedef struct {
    float size;      /* exported: the scale it grows to */
    float grow_time; /* exported: seconds to full size */
    float hold_time; /* exported: seconds until it shrinks again */

    float t, dir;
} NeonBomb;

static void bomb_ready(Node *self, NeonBomb *b)
{
    b->t = 0.0f;
    b->dir = 1.0f;
    set_scale(self, 0.001f);
}

static void bomb_update(Node *self, NeonBomb *b, float dt)
{
    /* Unity: the scale goes from 0 to `size` in the first second, stays, and goes back to
     * 0 after `hold_time`, as fast as it came. */
    b->t += b->dir * dt / b->grow_time;
    if (b->dir > 0.0f && b->t * b->grow_time >= b->hold_time) {
        b->dir = -1.0f;
        b->t = 1.0f;
    }
    if (b->t <= 0.0f) {
        rr_queue_free(self);
        return;
    }
    set_scale(self, clampf_(b->t, 0.001f, 1.0f) * b->size);
}

static void bomb_on_area(Node *self, NeonBomb *b, Node *area)
{
    Node      *enemy_node;
    NeonEnemy *enemy = enemy_of(area, &enemy_node);
    (void)b;
    if (enemy != NULL && !enemy->dead) {
        const float at[3] = { enemy_node->local.m[9], enemy_node->local.m[10], enemy_node->local.m[11] };
        game_burst(game_of(self), BURST_BLAST, at);
        enemy_die(enemy_node, enemy, true);
    }
}

COMPONENT_BEGIN(NeonBomb, "NeonBomb")
    EXPORT_FLOAT(size)
    EXPORT_FLOAT(grow_time)
    EXPORT_FLOAT(hold_time)
    ON_READY(bomb_ready)
    ON_UPDATE(bomb_update)
    ON_SIGNAL_NODE("_on_area_entered", bomb_on_area)
COMPONENT_END()

/* --- NeonSpawner ---------------------------------------------------------------------- */

typedef struct {
    Node *preview; /* exported: a mesh with a colour that changes; fades in until the enemy comes */

    float time;
} NeonSpawner;

static void spawner_ready(Node *self, NeonSpawner *s)
{
    (void)self;
    s->time = 0.0f;
}

static void spawner_update(Node *self, NeonSpawner *s, float dt)
{
    NeonGame *g = game_of(self);
    float     c[4] = { 1.0f, 1.0f, 1.0f, 0.0f };

    if (g == NULL || g->paused || g->over) {
        return;
    }
    if (g->intermission) {
        s->time = 0.0f;
    } else {
        s->time += dt;
    }
    c[3] = clampf_(s->time / g->spawn_interval, 0.0f, 1.0f);
    rr_mesh_set_albedo_color(s->preview, c);
    if (s->time >= g->spawn_interval && !g->intermission) {
        Node *enemy = rr_instantiate(g->enemy);
        if (enemy != NULL) {
            const float at[3] = { self->world.m[9], g->player != NULL ? g->player->local.m[10] : 0.0f,
                                  self->world.m[11] };
            place(enemy, at);
        }
        s->time = 0.0f;
    }
}

COMPONENT_BEGIN(NeonSpawner, "NeonSpawner")
    EXPORT_NODE(preview)
    ON_READY(spawner_ready)
    ON_UPDATE(spawner_update)
COMPONENT_END()

/* --- NeonDisco ------------------------------------------------------------------------ */

typedef struct {
    int32_t mode;       /* exported: 0 a random colour every `interval` (DiscoFloor), 1 a red pulse */
    float   interval;   /* exported: seconds */
    float   brightness; /* exported: the colours are multiplied with it */

    float left;
} NeonDisco;

static void disco_ready(Node *self, NeonDisco *d)
{
    (void)self;
    d->left = 0.0f;
}

static void disco_update(Node *self, NeonDisco *d, float dt)
{
    float c[4];

    if (d->mode == 1) {
        /* Unity's DeadlyObstacle and PingPongTexture: red going up and down once a second. */
        c[0] = ping_pong(self->scene->time, 1.0f) * d->brightness;
        c[1] = c[2] = 0.0f;
        c[3] = 1.0f;
        rr_mesh_set_albedo_color(self, c);
        return;
    }
    d->left -= dt;
    if (d->left > 0.0f) {
        return;
    }
    d->left = d->interval;
    c[0] = frand(0.0f, 1.0f);
    c[1] = frand(0.0f, 1.0f);
    c[2] = frand(0.0f, 1.0f);
    c[3] = c[0]; /* Unity's DiscoFloor put the red into the alpha too */
    c[0] *= d->brightness;
    c[1] *= d->brightness;
    c[2] *= d->brightness;
    rr_mesh_set_albedo_color(self, c);
}

COMPONENT_BEGIN(NeonDisco, "NeonDisco")
    EXPORT_INT(mode)
    EXPORT_FLOAT(interval)
    EXPORT_FLOAT(brightness)
    ON_READY(disco_ready)
    ON_UPDATE(disco_update)
COMPONENT_END()

/* --- NeonFlicker ---------------------------------------------------------------------- */

typedef struct {
    bool  blink;    /* exported: the alpha goes up and down 15 times a second (UI_Flicker) */
    bool  colors;   /* exported: a random colour every frame (UI_ColorFlicker) */
    float duration; /* exported: seconds of blinking after it is shown, then it hides; 0: always */

    float shown;
} NeonFlicker;

static void flicker_ready(Node *self, NeonFlicker *f)
{
    (void)self;
    f->shown = 0.0f;
}

static void flicker_update(Node *self, NeonFlicker *f, float dt)
{
    float c[4] = { 1.0f, 1.0f, 1.0f, 1.0f };

    if (!visible_in_tree(self)) {
        f->shown = 0.0f;
        return;
    }
    /* Unscaled: the game over shows while the game runs slowly. */
    f->shown += rr_get_time_scale() > 0.0f ? dt / rr_get_time_scale() : 0.0f;
    if (f->colors) {
        c[0] = frand(0.0f, 1.0f);
        c[1] = frand(0.0f, 1.0f);
        c[2] = frand(0.0f, 1.0f);
    }
    if (f->blink) {
        c[3] = ping_pong(f->shown * 30.0f, 1.0f);
        if (f->duration > 0.0f && f->shown >= f->duration) {
            show(self, false);
        }
    }
    rr_canvas_item_set_modulate(self, c);
}

COMPONENT_BEGIN(NeonFlicker, "NeonFlicker")
    EXPORT_BOOL(blink)
    EXPORT_BOOL(colors)
    EXPORT_FLOAT(duration)
    ON_READY(flicker_ready)
    ON_UPDATE(flicker_update)
COMPONENT_END()

/* --- NeonMenu ------------------------------------------------------------------------- */

typedef struct {
    Node *main_menu;     /* exported */
    Node *options;       /* exported */
    Node *credits;       /* exported */
    Node *play_button;   /* exported: has the focus on the main page */
    Node *options_back;  /* exported: has the focus on the options page */
    Node *credits_back;  /* exported */
    Node *music_slider;  /* exported */
    Node *sound_slider;  /* exported */
    Node *rumble_toggle; /* exported */
    const char *game_scene; /* exported */
    const char *exit_scene; /* exported: the runtime's own menu */
    float background_time;  /* exported */

    float background_left;
} NeonMenu;

static void menu_page(NeonMenu *m, Node *page, Node *focus)
{
    show(m->main_menu, page == m->main_menu);
    show(m->options, page == m->options);
    show(m->credits, page == m->credits);
    rr_control_grab_focus(focus);
}

static void menu_ready(Node *self, NeonMenu *m)
{
    g_game = NULL;
    rr_set_time_scale(1.0f);
    settings_apply(self->scene);
    settings_show(m->music_slider, m->sound_slider, m->rumble_toggle);
    m->background_left = 0.0f;
    menu_page(m, m->main_menu, m->play_button);
}

static void menu_update(Node *self, NeonMenu *m, float dt)
{
    m->background_left -= dt;
    if (m->background_left <= 0.0f && m->background_time > 0.0f) {
        const float c[4] = { frand(0.0f, 0.25f), frand(0.0f, 0.25f), frand(0.0f, 0.25f), 1.0f };
        rr_environment_set_background_color(self->scene, c);
        m->background_left = m->background_time;
    }
}

static void menu_on_play(Node *self, NeonMenu *m)
{
    (void)self;
    settings_save();
    rr_change_scene(m->game_scene);
}

static void menu_on_options(Node *self, NeonMenu *m)
{
    (void)self;
    settings_show(m->music_slider, m->sound_slider, m->rumble_toggle);
    menu_page(m, m->options, m->options_back);
}

static void menu_on_credits(Node *self, NeonMenu *m)
{
    (void)self;
    menu_page(m, m->credits, m->credits_back);
}

static void menu_on_back(Node *self, NeonMenu *m)
{
    (void)self;
    settings_save();
    menu_page(m, m->main_menu, m->play_button);
}

static void menu_on_exit(Node *self, NeonMenu *m)
{
    (void)self;
    settings_save();
    rr_change_scene(m->exit_scene);
}

static void menu_on_music(Node *self, NeonMenu *m, Node *slider)
{
    (void)m;
    g_settings.music = slider->value;
    settings_apply(self->scene);
}

static void menu_on_sound(Node *self, NeonMenu *m, Node *slider)
{
    (void)m;
    g_settings.sound = slider->value;
    settings_apply(self->scene);
}

static void menu_on_rumble(Node *self, NeonMenu *m, Node *toggle)
{
    (void)self;
    (void)m;
    g_settings.rumble = rr_button_is_pressed(toggle) ? 1u : 0u;
    rumble(0.5f, 0.5f, 0.2f);
}

COMPONENT_BEGIN(NeonMenu, "NeonMenu")
    EXPORT_NODE(main_menu)
    EXPORT_NODE(options)
    EXPORT_NODE(credits)
    EXPORT_NODE(play_button)
    EXPORT_NODE(options_back)
    EXPORT_NODE(credits_back)
    EXPORT_NODE(music_slider)
    EXPORT_NODE(sound_slider)
    EXPORT_NODE(rumble_toggle)
    EXPORT_SCENE(game_scene)
    EXPORT_SCENE(exit_scene)
    EXPORT_FLOAT(background_time)
    ON_READY(menu_ready)
    ON_UPDATE(menu_update)
    ON_SIGNAL("_on_play_pressed", menu_on_play)
    ON_SIGNAL("_on_options_pressed", menu_on_options)
    ON_SIGNAL("_on_credits_pressed", menu_on_credits)
    ON_SIGNAL("_on_back_pressed", menu_on_back)
    ON_SIGNAL("_on_exit_pressed", menu_on_exit)
    ON_SIGNAL_NODE("_on_music_value_changed", menu_on_music)
    ON_SIGNAL_NODE("_on_sound_value_changed", menu_on_sound)
    ON_SIGNAL_NODE("_on_rumble_toggled", menu_on_rumble)
COMPONENT_END()
