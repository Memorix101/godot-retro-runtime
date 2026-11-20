/* Component of the 2D platformer demo (scenes/platformer_2d.tscn).
 * Placeholder script: components/platform_player_2d.gd.
 *
 * The player is a CharacterBody2D and the ground is made of StaticBody2D nodes; the
 * runtime's rr_body_move_and_slide() keeps the two apart. Coins, hazards and the goal
 * are Area2D nodes in the groups "coin", "hazard" and "goal"; the player's own hitbox
 * area reports them through its `area_entered` signal. The view follows through a
 * Camera2D below the player, and the pictures are an AnimatedSprite2D with the
 * animations "idle", "walk" and "jump". */
#include <stdio.h>

#include "../core/rr_scene.h"

typedef struct {
    float       run_speed;  /* exported, pixels per second */
    float       jump_speed; /* exported */
    float       gravity;    /* exported */
    float       fall_limit; /* exported: below this Y the player starts again */
    Node       *sprite;     /* exported: AnimatedSprite2D; the art looks to the left */
    Node       *coin_label; /* exported */
    Node       *coin_sound; /* exported */
    const char *menu_scene; /* exported */

    int   act_left, act_right, act_jump, act_cancel;
    float start_x, start_y;
    float velocity[3];
    bool  hit_hazard;
    int   coins, coin_total;
    char  coin_text[24];
} PlatformPlayer2D;

static void show_coins(PlatformPlayer2D *p)
{
    if (p->coin_label != NULL) {
        snprintf(p->coin_text, sizeof(p->coin_text), "COINS %d/%d", p->coins, p->coin_total);
        rr_node_set_text(p->coin_label, p->coin_text);
    }
}

static void platform_player_2d_ready(Node *self, PlatformPlayer2D *p)
{
    Node *n = NULL;

    p->act_left   = rr_action(self->scene, "move_left");
    p->act_right  = rr_action(self->scene, "move_right");
    p->act_jump   = rr_action(self->scene, "jump");
    p->act_cancel = rr_action(self->scene, "ui_cancel");
    p->start_x = self->local.m[9];
    p->start_y = self->local.m[10];
    while ((n = rr_scene_find_tagged(self->scene, "coin", n)) != NULL) {
        p->coin_total++;
    }
    show_coins(p);
}

static void platform_player_2d_update(Node *self, PlatformPlayer2D *p, float dt)
{
    const float move = rr_action_strength(p->act_right) - rr_action_strength(p->act_left);
    const bool  on_ground = rr_body_is_on_floor(self);

    if (rr_action_pressed(p->act_cancel)) {
        rr_change_scene(p->menu_scene);
        return;
    }
    if (p->hit_hazard || self->local.m[10] > p->fall_limit) {
        /* Back to the start after a hazard or a fall. */
        self->local.m[9]  = p->start_x;
        self->local.m[10] = p->start_y;
        p->velocity[1] = 0.0f;
        p->hit_hazard = false;
    }

    /* Y points down the screen. Letting go of the button cuts a jump short. */
    p->velocity[0] = move * p->run_speed;
    if (on_ground && rr_action_pressed(p->act_jump)) {
        p->velocity[1] = -p->jump_speed;
    } else if (p->velocity[1] < -0.4f * p->jump_speed && !rr_action_down(p->act_jump)) {
        p->velocity[1] = -0.4f * p->jump_speed;
    }
    p->velocity[1] += p->gravity * dt;
    rr_body_move_and_slide(self, p->velocity, dt);

    if (p->sprite != NULL) {
        if (move != 0.0f) {
            rr_sprite_set_flip_h(p->sprite, move > 0.0f);
        }
        rr_sprite_play(p->sprite, !rr_body_is_on_floor(self) ? "jump" : (move != 0.0f ? "walk" : "idle"));
    }
}

/* Connected to the `area_entered` signal of the player's hitbox area. */
static void platform_player_2d_on_area(Node *self, PlatformPlayer2D *p, Node *area)
{
    (void)self;
    if (area == NULL || !(area->flags & RR_NODE_FLAG_VISIBLE)) {
        return; /* a coin that was collected before */
    }
    if (rr_node_has_tag(area, "coin")) {
        area->flags &= (uint8_t)~RR_NODE_FLAG_VISIBLE;
        p->coins++;
        show_coins(p);
        if (p->coin_sound != NULL) {
            rr_audio_play(p->coin_sound);
        }
    } else if (rr_node_has_tag(area, "hazard")) {
        p->hit_hazard = true;
    } else if (rr_node_has_tag(area, "goal")) {
        rr_change_scene(p->menu_scene);
    }
}

COMPONENT_BEGIN(PlatformPlayer2D, "PlatformPlayer2D")
    EXPORT_FLOAT(run_speed)
    EXPORT_FLOAT(jump_speed)
    EXPORT_FLOAT(gravity)
    EXPORT_FLOAT(fall_limit)
    EXPORT_NODE(sprite)
    EXPORT_NODE(coin_label)
    EXPORT_NODE(coin_sound)
    EXPORT_SCENE(menu_scene)
    ON_READY(platform_player_2d_ready)
    ON_UPDATE(platform_player_2d_update)
    ON_SIGNAL_NODE("_on_hitbox_area_entered", platform_player_2d_on_area)
COMPONENT_END()
