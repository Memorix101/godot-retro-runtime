/* Components of the 2D test scene (scenes/sprites_2d.tscn).
 * Placeholder scripts: components/ship_2d.gd, bullet_2d.gd and spinner_2d.gd. */
#include <math.h>

#include "../core/rr_platform.h"
#include "../core/rr_scene.h"

/* --- Ship2D: moves left and right, and fires bullets at the targets ------------------ */

typedef struct {
    float                speed;            /* exported, pixels per second */
    const RRPackedScene *bullet_scene;     /* exported: what the fire button makes */
    int32_t              bullet_scene_max; /* exported: how many of them exist at most */
    const char          *menu_scene;       /* exported */

    int act_left, act_right, act_fire, act_cancel;
} Ship2D;

#define SCREEN_MARGIN 40.0f

static void ship_2d_ready(Node *self, Ship2D *ship)
{
    ship->act_left   = rr_action(self->scene, "steer_left");
    ship->act_right  = rr_action(self->scene, "steer_right");
    ship->act_fire   = rr_action(self->scene, "jump");
    ship->act_cancel = rr_action(self->scene, "ui_cancel");
}

static void ship_2d_update(Node *self, Ship2D *ship, float dt)
{
    const float steer = rr_action_strength(ship->act_right) - rr_action_strength(ship->act_left);
    float       x = self->local.m[9] + steer * ship->speed * dt;

    if (x < SCREEN_MARGIN) {
        x = SCREEN_MARGIN;
    } else if (x > (float)RR_SCREEN_WIDTH - SCREEN_MARGIN) {
        x = (float)RR_SCREEN_WIDTH - SCREEN_MARGIN;
    }
    self->local.m[9] = x;

    if (rr_action_pressed(ship->act_fire)) {
        /* NULL if all bullets are in the air. */
        Node *bullet = rr_instantiate(ship->bullet_scene);
        if (bullet != NULL) {
            bullet->local.m[9]  = x;
            bullet->local.m[10] = self->local.m[10] - 30.0f;
        }
    }

    /* Back to the menu on cancel, or when the last target is gone. */
    if (rr_action_pressed(ship->act_cancel) ||
        rr_scene_find_tagged(self->scene, "target", NULL) == NULL) {
        rr_change_scene(ship->menu_scene);
    }
}

COMPONENT_BEGIN(Ship2D, "Ship2D")
    EXPORT_FLOAT(speed)
    EXPORT_PACKED_SCENE(bullet_scene)
    EXPORT_INT(bullet_scene_max)
    EXPORT_SCENE(menu_scene)
    ON_READY(ship_2d_ready)
    ON_UPDATE(ship_2d_update)
COMPONENT_END()

/* --- Bullet2D: flies up, removes the target it hits and itself ----------------------- */

typedef struct {
    float speed; /* exported, pixels per second */
} Bullet2D;

static void bullet_2d_update(Node *self, Bullet2D *bullet, float dt)
{
    self->local.m[10] -= bullet->speed * dt;
    if (self->local.m[10] < -20.0f) {
        rr_queue_free(self);
    }
}

static void bullet_2d_on_area_entered(Node *self, Bullet2D *bullet, Node *other)
{
    (void)bullet;
    if (rr_node_has_tag(other, "target")) {
        rr_queue_free(other);
        rr_queue_free(self);
    }
}

COMPONENT_BEGIN(Bullet2D, "Bullet2D")
    EXPORT_FLOAT(speed)
    ON_UPDATE(bullet_2d_update)
    ON_SIGNAL_NODE("_on_area_entered", bullet_2d_on_area_entered)
COMPONENT_END()

/* --- Spinner2D: rotates its node, and with it all child sprites --------------------- */

typedef struct {
    float degrees_per_second; /* exported */
    float angle;
} Spinner2D;

static void spinner_2d_update(Node *self, Spinner2D *spinner, float dt)
{
    float s, c;

    spinner->angle += spinner->degrees_per_second * dt * (3.14159265f / 180.0f);
    s = sinf(spinner->angle);
    c = cosf(spinner->angle);
    /* 2D rotation in the XY plane: basis X = (c, s), basis Y = (-s, c). */
    self->local.m[0] = c;
    self->local.m[1] = s;
    self->local.m[3] = -s;
    self->local.m[4] = c;
}

COMPONENT_BEGIN(Spinner2D, "Spinner2D")
    EXPORT_FLOAT(degrees_per_second)
    ON_UPDATE(spinner_2d_update)
COMPONENT_END()
