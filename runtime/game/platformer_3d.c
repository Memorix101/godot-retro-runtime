/* Components of the 3D platformer demo (scenes/platformer_3d.tscn).
 * Placeholder script: components/platform_player_3d.gd.
 *
 * The player is a CharacterBody3D and the ground is made of StaticBody3D nodes with box
 * shapes; the runtime's rr_body_move_and_slide() keeps the two apart. Coins, hazards
 * and the goal are areas in the groups "coin", "hazard" and "goal"; the player's own
 * hitbox area reports them through its `area_entered` signal. Areas in the group "enemy"
 * hurt like a hazard from the side and are removed when the player lands on them.
 *
 * A RayCast3D that points down from the player finds the ground below; a Sprite3D is
 * put there as a shadow, which is what makes a jump readable in 3D.
 *
 * More areas the player reacts to: "checkpoint" (where it starts again after a fall),
 * "spring" (throws it up). The player has lives, taken from the autoload "Game"; with
 * the last one gone the level ends. The time to the goal is measured, and the best one
 * is kept in a saved game.
 *
 * A coin may have an AnimationPlayer child with a clip named "collect", which is played
 * when the coin is taken. The spinning of the coins is a clip too, not code. */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "../core/rr_scene.h"
#include "game_state.h"

/* Replaces the basis of a node by a rotation around Y, so a scale on the node is lost. */
static void set_yaw(Node *node, float angle)
{
    const float s = sinf(angle), c = cosf(angle);
    float      *m = node->local.m;
    m[0] = c;    m[1] = 0.0f; m[2] = -s;
    m[3] = 0.0f; m[4] = 1.0f; m[5] = 0.0f;
    m[6] = s;    m[7] = 0.0f; m[8] = c;
}

/* --- PlatformPlayer3D ------------------------------------------------------------------- */

typedef struct {
    float       run_speed;  /* exported, metres per second */
    float       jump_speed; /* exported */
    float       gravity;    /* exported */
    float       fall_limit; /* exported: below this height the player starts again */
    Node       *model;      /* exported: turned to face the way the player walks */
    Node       *anim;       /* exported: AnimationPlayer with the clips idle, walk, jump, fall */
    Node       *camera;     /* exported: follows the player */
    Node       *coin_label; /* exported */
    Node       *coin_sound; /* exported */
    Node       *coin_burst; /* exported: particle system fired at every collected coin */
    Node       *shadow_ray; /* exported: RayCast3D that points down from the player */
    Node       *shadow;     /* exported: Sprite3D that is put on the ground below */
    Node       *status_label; /* exported: shows lives, time and best time */
    const char *menu_scene; /* exported */
    int32_t     level;      /* exported: which of the best times in the saved game is this level's */

    int   act_left, act_right, act_up, act_down, act_jump, act_cancel;
    float start[3];
    float camera_offset[3];
    float velocity[3];
    bool  hit_hazard;
    bool  thrown; /* going up from a spring or an enemy, not from a jump */
    int   coins, coin_total;
    char  coin_text[24];
    int   lives;
    float time, best; /* seconds; best is 0 before the first finished run */
    char  status_text[64];
} PlatformPlayer3D;

static void show_coins(PlatformPlayer3D *p)
{
    if (p->coin_label != NULL) {
        snprintf(p->coin_text, sizeof(p->coin_text), "COINS %d/%d", p->coins, p->coin_total);
        rr_node_set_text(p->coin_label, p->coin_text);
    }
}

/* What is kept between runs of the program: the best time of every level that uses this
 * player (its `level` field says which one is its own). */
#define SAVE_LEVELS 4

typedef struct {
    uint32_t magic;
    float    best[SAVE_LEVELS];
} PlatformSave;

#define SAVE_NAME  "RR_PLAT3D"
#define SAVE_MAGIC 0x50334432u

/* The saved game, or an empty one if there is none yet. */
static void read_save(PlatformSave *save)
{
    if (!rr_save_read(SAVE_NAME, save, sizeof(*save)) || save->magic != SAVE_MAGIC) {
        const PlatformSave empty = { SAVE_MAGIC, { 0.0f, 0.0f, 0.0f, 0.0f } };
        *save = empty;
    }
}

static void show_status(PlatformPlayer3D *p)
{
    if (p->status_label != NULL) {
        const int now = (int)(p->time * 10.0f), best = (int)(p->best * 10.0f);
        if (best > 0) {
            snprintf(p->status_text, sizeof(p->status_text), "LIVES %d  TIME %d.%d  BEST %d.%d",
                     p->lives, now / 10, now % 10, best / 10, best % 10);
        } else {
            snprintf(p->status_text, sizeof(p->status_text), "LIVES %d  TIME %d.%d", p->lives,
                     now / 10, now % 10);
        }
        rr_node_set_text(p->status_label, p->status_text);
    }
}

/* Gives the mesh of an area a size at once; a tween brings it back to 1 afterwards. */
static void pop(Node *area, float sx, float sy, float sz, int trans, float seconds)
{
    static const float one[3] = { 1.0f, 1.0f, 1.0f };
    Node              *mesh = rr_node_child(area, NULL);

    while (mesh != NULL && mesh->type != RR_NODE_MESH) {
        mesh = rr_node_child(area, mesh);
    }
    if (mesh != NULL) {
        mesh->local.m[0] = sx;   mesh->local.m[1] = 0.0f; mesh->local.m[2] = 0.0f;
        mesh->local.m[3] = 0.0f; mesh->local.m[4] = sy;   mesh->local.m[5] = 0.0f;
        mesh->local.m[6] = 0.0f; mesh->local.m[7] = 0.0f; mesh->local.m[8] = sz;
        rr_tween_property(mesh, RR_TWEEN_SCALE, one, seconds, 0.0f, trans, RR_EASE_OUT);
    }
}

static void platform_player_3d_ready(Node *self, PlatformPlayer3D *p)
{
    Node *n = NULL;
    int   i;

    p->act_left   = rr_action(self->scene, "move_left");
    p->act_right  = rr_action(self->scene, "move_right");
    p->act_up     = rr_action(self->scene, "move_up");
    p->act_down   = rr_action(self->scene, "move_down");
    p->act_jump   = rr_action(self->scene, "jump");
    p->act_cancel = rr_action(self->scene, "ui_cancel");
    /* The camera follows through the local transforms of both nodes, so the player and
     * the camera must have the same parent. */
    for (i = 0; i < 3; i++) {
        p->start[i] = self->local.m[9 + i];
        if (p->camera != NULL) {
            p->camera_offset[i] = p->camera->local.m[9 + i] - self->local.m[9 + i];
        }
    }
    while ((n = rr_scene_find_tagged(self->scene, "coin", n)) != NULL) {
        p->coin_total++;
    }
    show_coins(p);
    {
        GameState   *game = game_state(self->scene);
        PlatformSave save;
        p->lives = game != NULL && game->start_lives > 0 ? game->start_lives : 3;
        if (p->level < 0 || p->level >= SAVE_LEVELS) {
            p->level = 0;
        }
        read_save(&save);
        p->best = save.best[p->level];
    }
    show_status(p);
}

static void platform_player_3d_update(Node *self, PlatformPlayer3D *p, float dt)
{
    const bool on_ground = rr_body_is_on_floor(self);
    float     *pos = &self->local.m[9];
    float      x, z, length;
    int        i;

    if (rr_action_pressed(p->act_cancel)) {
        rr_change_scene(p->menu_scene);
        return;
    }
    if (p->hit_hazard || pos[1] < p->fall_limit) {
        /* Back to the last checkpoint after a hazard or a fall, with one life less. */
        if (--p->lives <= 0) {
            rr_change_scene(p->menu_scene);
            return;
        }
        for (i = 0; i < 3; i++) {
            pos[i] = p->start[i];
        }
        p->velocity[1] = 0.0f;
        p->hit_hazard = false;
    }

    p->time += dt;
    show_status(p);

    /* The camera looks along -Z, so "up" on the stick walks away from it. */
    x = rr_action_strength(p->act_right) - rr_action_strength(p->act_left);
    z = rr_action_strength(p->act_down) - rr_action_strength(p->act_up);
    length = sqrtf(x * x + z * z);
    if (length > 1.0f) {
        x /= length;
        z /= length;
    }
    if (length > 0.01f && p->model != NULL) {
        /* The model looks along its +Z axis. */
        set_yaw(p->model, atan2f(x, z));
    }

    p->velocity[0] = x * p->run_speed;
    p->velocity[2] = z * p->run_speed;
    if (p->velocity[1] <= 0.0f) {
        p->thrown = false;
    }
    if (on_ground && rr_action_pressed(p->act_jump)) {
        p->velocity[1] = p->jump_speed;
    } else if (!p->thrown && p->velocity[1] > 0.4f * p->jump_speed && !rr_action_down(p->act_jump)) {
        p->velocity[1] = 0.4f * p->jump_speed; /* letting go of the button cuts a jump short */
    }
    p->velocity[1] -= p->gravity * dt;
    rr_body_move_and_slide(self, p->velocity, dt);

    if (p->anim != NULL) {
        /* The clip follows what the player does. Starting a clip rewinds it, so only
         * a change of clip is passed on. */
        const char *clip = !rr_body_is_on_floor(self) ? (p->velocity[1] > 0.0f ? "jump" : "fall")
                                                      : (length > 0.01f ? "walk" : "idle");
        const char *current = rr_anim_current(p->anim);
        if (current == NULL || strcmp(current, clip) != 0) {
            rr_anim_play(p->anim, clip);
        }
    }
    if (p->shadow_ray != NULL && p->shadow != NULL) {
        /* Cast from where the player is now, not from where it was a frame ago. */
        rr_raycast_force_update(p->shadow_ray);
        if (rr_raycast_is_colliding(p->shadow_ray)) {
            float ground[3];
            rr_raycast_get_collision_point(p->shadow_ray, ground);
            p->shadow->local.m[9]  = ground[0];
            p->shadow->local.m[10] = ground[1] + 0.03f; /* just above, so it does not flicker */
            p->shadow->local.m[11] = ground[2];
            p->shadow->flags |= RR_NODE_FLAG_VISIBLE;
        } else {
            p->shadow->flags &= (uint8_t)~RR_NODE_FLAG_VISIBLE;
        }
    }
    if (p->camera != NULL) {
        for (i = 0; i < 3; i++) {
            p->camera->local.m[9 + i] = pos[i] + p->camera_offset[i];
        }
    }
}

/* Connected to the `area_entered` signal of the player's hitbox area. */
static void platform_player_3d_on_area(Node *self, PlatformPlayer3D *p, Node *area)
{
    if (area == NULL || !(area->flags & RR_NODE_FLAG_VISIBLE)) {
        return; /* a coin that was collected before */
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
        if (p->coin_burst != NULL) {
            /* One particle system serves all coins: put it where the coin is and fire it. */
            p->coin_burst->local.m[9]  = area->world.m[9];
            p->coin_burst->local.m[10] = area->world.m[10] + 0.3f;
            p->coin_burst->local.m[11] = area->world.m[11];
            rr_particles_restart(p->coin_burst);
        }
        p->coins++;
        show_coins(p);
        if (p->coin_sound != NULL) {
            rr_audio_play(p->coin_sound);
        }
    } else if (rr_node_has_tag(area, "enemy")) {
        /* Coming down on it from above squashes it; any other touch hurts. */
        if (p->velocity[1] < 0.0f && self->world.m[10] > area->world.m[10] + 0.25f) {
            if (p->coin_burst != NULL) {
                p->coin_burst->local.m[9]  = area->world.m[9];
                p->coin_burst->local.m[10] = area->world.m[10] + 0.3f;
                p->coin_burst->local.m[11] = area->world.m[11];
                rr_particles_restart(p->coin_burst);
            }
            if (p->coin_sound != NULL) {
                rr_audio_play(p->coin_sound);
            }
            rr_queue_free(area);
            p->velocity[1] = 0.7f * p->jump_speed; /* and bounces the player up */
            p->thrown      = true;
        } else {
            p->hit_hazard = true;
        }
    } else if (rr_node_has_tag(area, "checkpoint")) {
        /* From now on a fall leads back here. */
        p->start[0] = area->world.m[9];
        p->start[1] = area->world.m[10] + 0.1f;
        p->start[2] = area->world.m[11];
        pop(area, 1.6f, 1.6f, 1.6f, RR_TRANS_BACK, 0.4f);
    } else if (rr_node_has_tag(area, "spring")) {
        /* Pressed flat, it throws the player up and swings back. */
        p->velocity[1] = 1.7f * p->jump_speed;
        p->thrown      = true;
        pop(area, 1.2f, 0.3f, 1.2f, RR_TRANS_ELASTIC, 0.6f);
    } else if (rr_node_has_tag(area, "hazard")) {
        p->hit_hazard = true;
    } else if (rr_node_has_tag(area, "goal")) {
        if (p->best <= 0.0f || p->time < p->best) {
            /* The times of the other levels stay as they are. */
            PlatformSave save;
            read_save(&save);
            save.best[p->level] = p->time;
            rr_save_write(SAVE_NAME, &save, sizeof(save));
        }
        rr_change_scene(p->menu_scene);
    }
}

COMPONENT_BEGIN(PlatformPlayer3D, "PlatformPlayer3D")
    EXPORT_FLOAT(run_speed)
    EXPORT_FLOAT(jump_speed)
    EXPORT_FLOAT(gravity)
    EXPORT_FLOAT(fall_limit)
    EXPORT_NODE(model)
    EXPORT_NODE(anim)
    EXPORT_NODE(camera)
    EXPORT_NODE(coin_label)
    EXPORT_NODE(coin_sound)
    EXPORT_NODE(coin_burst)
    EXPORT_NODE(shadow_ray)
    EXPORT_NODE(shadow)
    EXPORT_NODE(status_label)
    EXPORT_SCENE(menu_scene)
    EXPORT_INT(level)
    ON_READY(platform_player_3d_ready)
    ON_UPDATE(platform_player_3d_update)
    ON_SIGNAL_NODE("_on_hitbox_area_entered", platform_player_3d_on_area)
COMPONENT_END()

/* --- CrumblePlatform ---------------------------------------------------------------------
 * A StaticBody3D that gives way a moment after the player steps on it, falls, and is back
 * in its place a few seconds later. An Area3D child above its top reports the player
 * through `body_entered`; two Timer children time the fall and the return, and a tween
 * does the falling. Placeholder script: components/crumble_platform.gd. */

typedef struct {
    Node *fall_timer;   /* exported: Timer from the first step to the fall */
    Node *return_timer; /* exported: Timer from the fall to being back */
    float drop;         /* exported: how far it falls */

    float home[3];
    bool  falling;
} CrumblePlatform;

static void crumble_platform_ready(Node *self, CrumblePlatform *c)
{
    int i;
    for (i = 0; i < 3; i++) {
        c->home[i] = self->local.m[9 + i];
    }
}

static void crumble_platform_on_body(Node *self, CrumblePlatform *c, Node *body)
{
    /* The area also sees the platform itself, which is a body too. */
    if (body != NULL && body->type == RR_NODE_CHARACTER_BODY && !c->falling &&
        rr_timer_is_stopped(c->fall_timer)) {
        /* It sags a little first, as a warning. */
        const float sag[3] = { c->home[0], c->home[1] - 0.12f, c->home[2] };
        rr_tween_property(self, RR_TWEEN_POSITION, sag, 0.3f, 0.0f, RR_TRANS_BOUNCE, RR_EASE_OUT);
        rr_timer_start(c->fall_timer, 0.0f);
    }
}

static void crumble_platform_on_fall(Node *self, CrumblePlatform *c)
{
    const float down[3] = { c->home[0], c->home[1] - c->drop, c->home[2] };
    c->falling = true;
    rr_tween_property(self, RR_TWEEN_POSITION, down, 1.0f, 0.0f, RR_TRANS_QUAD, RR_EASE_IN);
    rr_timer_start(c->return_timer, 0.0f);
}

static void crumble_platform_on_return(Node *self, CrumblePlatform *c)
{
    int i;
    rr_tween_kill(self);
    for (i = 0; i < 3; i++) {
        self->local.m[9 + i] = c->home[i];
    }
    c->falling = false;
}

COMPONENT_BEGIN(CrumblePlatform, "CrumblePlatform")
    EXPORT_NODE(fall_timer)
    EXPORT_NODE(return_timer)
    EXPORT_FLOAT(drop)
    ON_READY(crumble_platform_ready)
    ON_SIGNAL_NODE("_on_sense_body_entered", crumble_platform_on_body)
    ON_SIGNAL("_on_fall_timeout", crumble_platform_on_fall)
    ON_SIGNAL("_on_return_timeout", crumble_platform_on_return)
COMPONENT_END()
